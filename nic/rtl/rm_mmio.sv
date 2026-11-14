`timescale 1ns / 1ps

// rm_mmio: single 32-bit register accesses from the service-layer RM into the
// CPM5 QDMA queue space, and a register block the R5 can reach (smart NIC
// static, shell/patch_smart.py).
//
// The smart static routes M_QDMA_SLV_BRIDGE to 0x7_1000_0000 + 16 MiB, the CPM5
// DMA1 "MULTQ" decode: QDMA CSRs and the doorbells of every function (H2C PIDX
// at fn*0x10000 + 0x18004 + 0x10*qidx, as rung from the R5 on the rpuq
// static). PG347: register accesses through the AXI slave bridge must be 4
// bytes with AxCACHE[1] = 0, so the master issues one 32-bit beat per command
// (AxLEN 0, AxSIZE 2) and its AxCACHE resets to 0000.
//
// Safety: a command is refused (STATUS.range_err) unless the 4-byte access
// lies inside [WIN_BASE, WIN_BASE + WIN_SIZE) (parameters, the queue space by
// default) and is 4-byte aligned. bready/rready are always 1. STATUS.timeout
// is set when no response arrives within TIMEOUT cycles; the command keeps
// waiting (AXI does not allow withdrawing a request), busy stays 1.
//
// Registers are placed at service-layer offset 0x0 (PF2 BAR2 0x0, host
// 0x203_0000_0000), one of the slots the smart static decodes for the R5 too
// (R5 0x9C00_0000).
//
// Registers (byte offsets, 32-bit, single-beat AXI-Lite, no read side effects):
//   0x000 ID          0x524D_494F "RMIO"
//   0x004 VERSION     0x0001_0000
//   0x008 BUILD
//   0x00C CAPS        [0] write [1] read, [15:8] data bytes (4)
//   0x010 CMD     W   [0] write WDATA to ADDR, [1] read ADDR into RDATA (one of
//                     the two), [31] clear status and counters (idle only);
//                     reads return the last value written
//   0x014 STATUS      [0] busy [1] done [2] resp_err [3] timeout [4] range_err
//                     [5] param_err [9:8] last BRESP/RRESP
//   0x018 SCRATCH0    0x01C SCRATCH1   read/write, no function (mailbox)
//   0x020 ADDR_LO     0x024 ADDR_HI
//   0x028 WDATA
//   0x02C RDATA       data of the last read
//   0x030 TIMEOUT     cycles (reset 250_000_000)
//   0x034 AXI_ATTR    [2:0] AxPROT (reset 010), [7:4] AxCACHE (reset 0000)
//   0x038 N_WRITES    0x03C N_READS    accesses completed with OKAY
//   0x040 N_ERRS      accesses completed with a non-OKAY response
//   0x044 LAT_LAST    cycles from the command to its response
//   0x048 WIN_BASE_LO 0x04C WIN_BASE_HI 0x050 WIN_SIZE   (read only)
//   0x054 N_LITE_WR   AXI-Lite writes received (any register)
//   0x058 CYCLES      free-running clock counter (low 32 bits)
module rm_mmio #(
    parameter [63:0] WIN_BASE = 64'h7_1000_0000,
    parameter [31:0] WIN_SIZE = 32'h0100_0000,
    parameter [31:0] BUILD    = 32'd1
) (
    input  wire          clk,
    input  wire          resetn,

    input  wire [15:0]   s_axil_awaddr,
    input  wire          s_axil_awvalid,
    output reg           s_axil_awready,
    input  wire [31:0]   s_axil_wdata,
    input  wire [3:0]    s_axil_wstrb,
    input  wire          s_axil_wvalid,
    output reg           s_axil_wready,
    output wire [1:0]    s_axil_bresp,
    output reg           s_axil_bvalid,
    input  wire          s_axil_bready,
    input  wire [15:0]   s_axil_araddr,
    input  wire          s_axil_arvalid,
    output reg           s_axil_arready,
    output reg  [31:0]   s_axil_rdata,
    output wire [1:0]    s_axil_rresp,
    output reg           s_axil_rvalid,
    input  wire          s_axil_rready,

    output wire [63:0]   m_axi_awaddr,
    output wire [7:0]    m_axi_awlen,
    output wire [2:0]    m_axi_awsize,
    output wire [1:0]    m_axi_awburst,
    output wire [3:0]    m_axi_awcache,
    output wire [2:0]    m_axi_awprot,
    output reg           m_axi_awvalid,
    input  wire          m_axi_awready,
    output wire [31:0]   m_axi_wdata,
    output wire [3:0]    m_axi_wstrb,
    output wire          m_axi_wlast,
    output reg           m_axi_wvalid,
    input  wire          m_axi_wready,
    input  wire [1:0]    m_axi_bresp,
    input  wire          m_axi_bvalid,
    output wire          m_axi_bready,
    output wire [63:0]   m_axi_araddr,
    output wire [7:0]    m_axi_arlen,
    output wire [2:0]    m_axi_arsize,
    output wire [1:0]    m_axi_arburst,
    output wire [3:0]    m_axi_arcache,
    output wire [2:0]    m_axi_arprot,
    output reg           m_axi_arvalid,
    input  wire          m_axi_arready,
    input  wire [31:0]   m_axi_rdata,
    input  wire [1:0]    m_axi_rresp,
    input  wire          m_axi_rlast,
    input  wire          m_axi_rvalid,
    output wire          m_axi_rready
);
    localparam [31:0] ID      = 32'h524D_494F;
    localparam [31:0] VERSION = 32'h0001_0000;

    // ------------------------------------------------------------ registers
    reg  [31:0] cmd_r, scratch0, scratch1, wdata_r, rdata_r, timeout_r;
    reg  [63:0] addr_r;
    reg  [2:0]  prot_r;
    reg  [3:0]  cache_r;
    reg  [31:0] n_wr, n_rd, n_err, lat_last, n_lite_wr, cycles;

    // status
    reg         busy, done, resp_err, timed_out, range_err, param_err;
    reg  [1:0]  last_resp;
    reg         is_read;          // current command
    reg         aw_done, w_done;  // write: AW / W handshakes seen
    reg  [31:0] run_cnt;

    // ------------------------------------------------------------ AXI outputs
    assign m_axi_awaddr  = addr_r;
    assign m_axi_awlen   = 8'd0;
    assign m_axi_awsize  = 3'd2;
    assign m_axi_awburst = 2'b01;
    assign m_axi_awcache = cache_r;
    assign m_axi_awprot  = prot_r;
    assign m_axi_wdata   = wdata_r;
    assign m_axi_wstrb   = 4'hF;
    assign m_axi_wlast   = 1'b1;
    assign m_axi_bready  = 1'b1;
    assign m_axi_araddr  = addr_r;
    assign m_axi_arlen   = 8'd0;
    assign m_axi_arsize  = 3'd2;
    assign m_axi_arburst = 2'b01;
    assign m_axi_arcache = cache_r;
    assign m_axi_arprot  = prot_r;
    assign m_axi_rready  = 1'b1;

    // ------------------------------------------------------------ AXI-Lite write decode
    wire        lw_fire  = s_axil_awvalid && s_axil_wvalid && !s_axil_awready && !s_axil_bvalid;
    wire [15:0] lw_addr  = {s_axil_awaddr[15:2], 2'b00};
    wire        lw_full  = (s_axil_wstrb == 4'hF);
    wire        cmd_wr   = lw_fire && lw_full && (lw_addr == 16'h010);
    wire        start    = cmd_wr && !busy && (s_axil_wdata[1:0] != 2'b00);
    wire        clear    = cmd_wr && !busy && s_axil_wdata[31];

    wire [64:0] acc_end  = {1'b0, addr_r} + 65'd4;
    wire [64:0] win_end  = {1'b0, WIN_BASE} + {33'd0, WIN_SIZE};
    wire        range_ok = (addr_r >= WIN_BASE) && (acc_end <= win_end) && (addr_r[1:0] == 2'b00);

    assign s_axil_bresp = 2'b00;
    assign s_axil_rresp = 2'b00;

    always @(posedge clk) begin
        if (!resetn) begin
            s_axil_awready <= 1'b0;
            s_axil_wready  <= 1'b0;
            s_axil_bvalid  <= 1'b0;
            cmd_r     <= 32'd0;
            scratch0  <= 32'd0;
            scratch1  <= 32'd0;
            addr_r    <= 64'd0;
            wdata_r   <= 32'd0;
            timeout_r <= 32'd250_000_000;
            prot_r    <= 3'b010;
            cache_r   <= 4'b0000;
            n_lite_wr <= 32'd0;
        end else begin
            s_axil_awready <= 1'b0;
            s_axil_wready  <= 1'b0;
            if (s_axil_bvalid && s_axil_bready)
                s_axil_bvalid <= 1'b0;
            if (lw_fire) begin
                s_axil_awready <= 1'b1;
                s_axil_wready  <= 1'b1;
                s_axil_bvalid  <= 1'b1;
                n_lite_wr      <= n_lite_wr + 32'd1;
                if (lw_full) begin
                    case (lw_addr)
                        16'h010: cmd_r    <= s_axil_wdata;
                        16'h018: scratch0 <= s_axil_wdata;
                        16'h01C: scratch1 <= s_axil_wdata;
                        default: ;
                    endcase
                    // command parameters are held while a command runs
                    if (!busy) begin
                        case (lw_addr)
                            16'h020: addr_r[31:0]  <= s_axil_wdata;
                            16'h024: addr_r[63:32] <= s_axil_wdata;
                            16'h028: wdata_r       <= s_axil_wdata;
                            16'h030: timeout_r     <= s_axil_wdata;
                            16'h034: begin prot_r <= s_axil_wdata[2:0]; cache_r <= s_axil_wdata[7:4]; end
                            default: ;
                        endcase
                    end
                end
            end
        end
    end

    // ------------------------------------------------------------ command engine
    wire aw_fire = m_axi_awvalid && m_axi_awready;
    wire w_fire  = m_axi_wvalid && m_axi_wready;
    wire ar_fire = m_axi_arvalid && m_axi_arready;

    always @(posedge clk) begin
        if (!resetn) begin
            busy <= 1'b0; done <= 1'b0; resp_err <= 1'b0; timed_out <= 1'b0;
            range_err <= 1'b0; param_err <= 1'b0; last_resp <= 2'b00;
            is_read <= 1'b0; aw_done <= 1'b0; w_done <= 1'b0;
            m_axi_awvalid <= 1'b0; m_axi_wvalid <= 1'b0; m_axi_arvalid <= 1'b0;
            rdata_r <= 32'd0; run_cnt <= 32'd0;
            n_wr <= 32'd0; n_rd <= 32'd0; n_err <= 32'd0; lat_last <= 32'd0;
            cycles <= 32'd0;
        end else begin
            cycles <= cycles + 32'd1;
            if (clear) begin
                done <= 1'b0; resp_err <= 1'b0; timed_out <= 1'b0;
                range_err <= 1'b0; param_err <= 1'b0; last_resp <= 2'b00;
                n_wr <= 32'd0; n_rd <= 32'd0; n_err <= 32'd0; lat_last <= 32'd0;
            end else if (start) begin
                done <= 1'b0; resp_err <= 1'b0; timed_out <= 1'b0;
                range_err <= 1'b0; param_err <= 1'b0;
                if (s_axil_wdata[1:0] == 2'b11) begin
                    param_err <= 1'b1;
                    done      <= 1'b1;
                end else if (!range_ok) begin
                    range_err <= 1'b1;
                    done      <= 1'b1;
                end else begin
                    busy    <= 1'b1;
                    run_cnt <= 32'd0;
                    is_read <= s_axil_wdata[1];
                    aw_done <= 1'b0;
                    w_done  <= 1'b0;
                    if (s_axil_wdata[1]) begin
                        m_axi_arvalid <= 1'b1;
                    end else begin
                        m_axi_awvalid <= 1'b1;
                        m_axi_wvalid  <= 1'b1;
                    end
                end
            end else if (busy) begin
                run_cnt <= run_cnt + 32'd1;
                if (run_cnt == timeout_r)
                    timed_out <= 1'b1;
                if (aw_fire) begin m_axi_awvalid <= 1'b0; aw_done <= 1'b1; end
                if (w_fire)  begin m_axi_wvalid  <= 1'b0; w_done  <= 1'b1; end
                if (ar_fire) m_axi_arvalid <= 1'b0;
                // B can only follow AW and W (AXI); R only follows AR
                if (!is_read && m_axi_bvalid) begin
                    busy      <= 1'b0;
                    done      <= 1'b1;
                    last_resp <= m_axi_bresp;
                    lat_last  <= run_cnt + 32'd1;
                    if (m_axi_bresp == 2'b00) n_wr <= n_wr + 32'd1;
                    else begin n_err <= n_err + 32'd1; resp_err <= 1'b1; end
                end
                if (is_read && m_axi_rvalid) begin
                    busy      <= 1'b0;
                    done      <= 1'b1;
                    last_resp <= m_axi_rresp;
                    lat_last  <= run_cnt + 32'd1;
                    rdata_r   <= m_axi_rdata;
                    if (m_axi_rresp == 2'b00) n_rd <= n_rd + 32'd1;
                    else begin n_err <= n_err + 32'd1; resp_err <= 1'b1; end
                end
            end
        end
    end

    wire [31:0] status = {22'd0, last_resp, 2'b00, param_err, range_err, timed_out, resp_err, done, busy};

    // ------------------------------------------------------------ AXI-Lite read
    always @(posedge clk) begin
        if (!resetn) begin
            s_axil_arready <= 1'b0;
            s_axil_rvalid  <= 1'b0;
            s_axil_rdata   <= 32'd0;
        end else begin
            s_axil_arready <= 1'b0;
            if (s_axil_rvalid && s_axil_rready)
                s_axil_rvalid <= 1'b0;
            if (s_axil_arvalid && !s_axil_arready && !s_axil_rvalid) begin
                s_axil_arready <= 1'b1;
                s_axil_rvalid  <= 1'b1;
                case ({s_axil_araddr[15:2], 2'b00})
                    16'h000: s_axil_rdata <= ID;
                    16'h004: s_axil_rdata <= VERSION;
                    16'h008: s_axil_rdata <= BUILD;
                    16'h00C: s_axil_rdata <= 32'h0000_0403;
                    16'h010: s_axil_rdata <= cmd_r;
                    16'h014: s_axil_rdata <= status;
                    16'h018: s_axil_rdata <= scratch0;
                    16'h01C: s_axil_rdata <= scratch1;
                    16'h020: s_axil_rdata <= addr_r[31:0];
                    16'h024: s_axil_rdata <= addr_r[63:32];
                    16'h028: s_axil_rdata <= wdata_r;
                    16'h02C: s_axil_rdata <= rdata_r;
                    16'h030: s_axil_rdata <= timeout_r;
                    16'h034: s_axil_rdata <= {24'd0, cache_r, 1'b0, prot_r};
                    16'h038: s_axil_rdata <= n_wr;
                    16'h03C: s_axil_rdata <= n_rd;
                    16'h040: s_axil_rdata <= n_err;
                    16'h044: s_axil_rdata <= lat_last;
                    16'h048: s_axil_rdata <= WIN_BASE[31:0];
                    16'h04C: s_axil_rdata <= WIN_BASE[63:32];
                    16'h050: s_axil_rdata <= WIN_SIZE;
                    16'h054: s_axil_rdata <= n_lite_wr;
                    16'h058: s_axil_rdata <= cycles;
                    default: s_axil_rdata <= 32'd0;
                endcase
            end
        end
    end
endmodule
