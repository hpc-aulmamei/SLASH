`timescale 1ns / 1ps

// host_probe: AXI4 master that measures the card -> host path of the SLASH
// service layer (M_QDMA_SLV_BRIDGE -> CPM5 slave bridge -> PCIe).
//
// The write engine writes LEN bytes of a pattern starting at ADDR, the read
// engine reads them back and compares. Both issue INCR bursts of BURST_BEATS
// beats with up to OUTSTANDING bursts in flight (single ID, in order).
// Pattern, per 32-bit lane at AXI byte address a: a[31:0] ^ SEED.
//
// Safety: a start is refused (STATUS.range_err) unless [ADDR, ADDR+LEN) lies
// inside [WIN_BASE, WIN_BASE+WIN_SIZE). WIN_SIZE resets to 0, so nothing runs
// before the host programs the window. bready/rready are always 1, and a
// no-progress timeout (TIMEOUT cycles without any handshake while requests are
// outstanding) ends the run with STATUS.timeout instead of waiting forever.
//
// Registers (byte offsets, 32-bit, single-beat AXI-Lite, no read side effects):
//   0x000 ID          0x48505242 "HPRB"
//   0x004 VERSION     0x0001_0000
//   0x008 BUILD
//   0x00C CAPS        [15:0] beat bytes, [23:16] max outstanding, [31:24] max burst beats
//   0x010 CMD     W   [0] write pass, [1] read pass, [2] concurrent (both at once),
//                     [3] no verify, [30] abort (stop issuing), [31] clear status/counters (idle only)
//                     a write with [1:0] != 0 while idle starts a run; reads return the last value
//   0x014 STATUS      [0] busy [1] done [2] bresp_err [3] rresp_err [4] mismatch [5] timeout
//                     [6] range_err [7] aborted [8] param_err [9] write active [10] read active
//                     [21:16] writes outstanding [29:24] reads outstanding
//   0x020 ADDR_LO  0x024 ADDR_HI   start address (multiple of the burst size)
//   0x028 LEN         bytes (> 0, multiple of the burst size)
//   0x02C BURST_BEATS 1, 2, 4 .. MAX_BEATS (power of two; a burst never crosses 4 KiB)
//   0x030 OUTSTANDING 1 .. MAX_OUT
//   0x034 SEED
//   0x038 TIMEOUT     cycles
//   0x03C AXI_ATTR    [2:0] AxPROT, [7:4] AxCACHE  (reset: PROT 010 non-secure, CACHE 0011)
//   0x040 WIN_BASE_LO 0x044 WIN_BASE_HI 0x048 WIN_SIZE_LO 0x04C WIN_SIZE_HI
//   0x060 W_CYCLES_LO 0x064 W_CYCLES_HI   write engine start to last B
//   0x068 R_CYCLES_LO 0x06C R_CYCLES_HI   read engine start to last R
//   0x070 W_BURSTS    B responses received
//   0x074 R_BEATS     R beats received
//   0x078 BRESP_ERRS  0x07C RRESP_ERRS
//   0x080 MISMATCHES  beats that differed from the pattern
//   0x084 FIRST_RESP  [1:0] first non-OKAY BRESP, [9:8] first non-OKAY RRESP
//   0x088 MM_ADDR_LO  0x08C MM_ADDR_HI    address of the first mismatching beat
//   0x090 MM_EXP      0x094 MM_GOT        lane 0 of that beat
//   0x0A0 RD_LAT_MIN  0x0A4 RD_LAT_MAX  0x0A8 RD_LAT_SUM_LO 0x0AC RD_LAT_SUM_HI  (AR handshake -> first R beat)
//   0x0B0 WR_LAT_MIN  0x0B4 WR_LAT_MAX  0x0B8 WR_LAT_SUM_LO 0x0BC WR_LAT_SUM_HI  (AW handshake -> B)
//   0x0C0 CYCLES_LO   0x0C4 CYCLES_HI   free-running clock counter
module host_probe #(
    parameter integer DATA_W    = 512,
    parameter integer MAX_OUT   = 32,
    parameter [31:0]  BUILD     = 32'd14
) (
    input  wire                 clk,
    input  wire                 resetn,

    input  wire [15:0]          s_axil_awaddr,
    input  wire                 s_axil_awvalid,
    output reg                  s_axil_awready,
    input  wire [31:0]          s_axil_wdata,
    input  wire [3:0]           s_axil_wstrb,
    input  wire                 s_axil_wvalid,
    output reg                  s_axil_wready,
    output wire [1:0]           s_axil_bresp,
    output reg                  s_axil_bvalid,
    input  wire                 s_axil_bready,
    input  wire [15:0]          s_axil_araddr,
    input  wire                 s_axil_arvalid,
    output reg                  s_axil_arready,
    output reg  [31:0]          s_axil_rdata,
    output wire [1:0]           s_axil_rresp,
    output reg                  s_axil_rvalid,
    input  wire                 s_axil_rready,

    output wire [63:0]          m_axi_awaddr,
    output wire [7:0]           m_axi_awlen,
    output wire [2:0]           m_axi_awsize,
    output wire [1:0]           m_axi_awburst,
    output wire [3:0]           m_axi_awcache,
    output wire [2:0]           m_axi_awprot,
    output reg                  m_axi_awvalid,
    input  wire                 m_axi_awready,
    output wire [DATA_W-1:0]    m_axi_wdata,
    output wire [DATA_W/8-1:0]  m_axi_wstrb,
    output wire                 m_axi_wlast,
    output reg                  m_axi_wvalid,
    input  wire                 m_axi_wready,
    input  wire [1:0]           m_axi_bresp,
    input  wire                 m_axi_bvalid,
    output wire                 m_axi_bready,
    output wire [63:0]          m_axi_araddr,
    output wire [7:0]           m_axi_arlen,
    output wire [2:0]           m_axi_arsize,
    output wire [1:0]           m_axi_arburst,
    output wire [3:0]           m_axi_arcache,
    output wire [2:0]           m_axi_arprot,
    output reg                  m_axi_arvalid,
    input  wire                 m_axi_arready,
    input  wire [DATA_W-1:0]    m_axi_rdata,
    input  wire [1:0]           m_axi_rresp,
    input  wire                 m_axi_rlast,
    input  wire                 m_axi_rvalid,
    output wire                 m_axi_rready
);
    localparam integer BB        = DATA_W / 8;              // beat bytes
    localparam integer BB_LOG    = $clog2(BB);
    localparam integer LANES     = DATA_W / 32;
    localparam integer MAX_BEATS = 4096 / BB;               // burst <= 4 KiB
    localparam integer OW        = $clog2(MAX_OUT + 1);     // outstanding counter width
    localparam [31:0]  ID        = 32'h48505242;
    localparam [31:0]  VERSION   = 32'h0001_0000;

    // ------------------------------------------------------------ registers
    reg  [31:0] cmd_r;
    reg  [63:0] addr_r;
    reg  [31:0] len_r;
    reg  [8:0]  beats_r;
    reg  [OW-1:0] outst_r;
    reg  [31:0] seed_r;
    reg  [31:0] timeout_r;
    reg  [2:0]  prot_r;
    reg  [3:0]  cache_r;
    reg  [63:0] win_base_r;
    reg  [63:0] win_size_r;

    // status
    reg  busy, done, bresp_err, rresp_err, mismatch, timed_out, range_err, aborted, param_err;
    reg  w_act, r_act;          // engine running (issuing or draining)
    reg  w_pend, r_pend;        // engine still to run in this command (sequential mode)
    reg  no_verify, abort_req;

    // counters
    reg  [63:0] w_cycles, r_cycles, cycles;
    reg  [31:0] w_bursts_rx, r_beats_rx, bresp_errs, rresp_errs, mismatches;
    reg  [1:0]  first_bresp, first_rresp;
    reg  [63:0] mm_addr;
    reg  [31:0] mm_exp, mm_got;
    reg  [31:0] rd_lat_min, rd_lat_max, wr_lat_min, wr_lat_max;
    reg  [63:0] rd_lat_sum, wr_lat_sum;

    // ------------------------------------------------------------ start checks
    reg  [3:0]  beats_log;
    always @* begin
        beats_log = 4'd0;
        for (int i = 0; i <= 8; i++)
            if (beats_r == (9'd1 << i)) beats_log = 4'(i);
    end
    wire        beats_ok  = (beats_r != 0) && ((beats_r & (beats_r - 9'd1)) == 0) && (beats_r <= MAX_BEATS);
    wire [4:0]  burst_log = 5'(beats_log) + 5'(BB_LOG);      // log2(burst bytes)
    wire [31:0] burst_msk = (32'd1 << burst_log) - 32'd1;
    wire        align_ok  = ((addr_r[31:0] & burst_msk) == 0) && ((len_r & burst_msk) == 0) && (len_r != 0);
    wire        outst_ok  = (outst_r != 0) && (outst_r <= MAX_OUT);
    wire [64:0] run_end   = {1'b0, addr_r} + {33'd0, len_r};
    wire [64:0] win_end   = {1'b0, win_base_r} + {1'b0, win_size_r};
    wire        range_ok  = (addr_r >= win_base_r) && (run_end <= win_end) && (win_size_r != 0);
    wire [31:0] n_bursts  = len_r >> burst_log;

    // ------------------------------------------------------------ write engine
    reg  [63:0] aw_addr;
    reg  [31:0] aw_left;                 // bursts still to issue
    reg  [31:0] w_left;                  // bursts whose data is still to send
    reg  [31:0] b_left;                  // B responses still expected
    reg  [OW-1:0] w_out;                 // AW issued, B not yet received
    reg  [31:0] aw_ahead;                // AW issued minus W bursts sent
    reg  [63:0] wd_addr;                 // address of the next W beat
    reg  [8:0]  wd_beat;
    wire        w_issue = w_act && !abort_req && !timed_out;

    wire aw_fire = m_axi_awvalid && m_axi_awready;
    wire w_fire  = m_axi_wvalid && m_axi_wready;
    wire b_fire  = m_axi_bvalid;          // bready = 1

    // ------------------------------------------------------------ read engine
    reg  [63:0] ar_addr;
    reg  [31:0] ar_left;
    reg  [31:0] r_left;                  // R bursts still expected
    reg  [OW-1:0] r_out;
    reg  [63:0] rd_addr;                 // address of the next R beat
    reg  [8:0]  rd_beat;
    wire        r_issue = r_act && !abort_req && !timed_out;

    wire ar_fire = m_axi_arvalid && m_axi_arready;
    wire r_fire  = m_axi_rvalid;          // rready = 1

    // ------------------------------------------------------------ AXI outputs
    assign m_axi_awaddr  = aw_addr;
    assign m_axi_awlen   = 8'(beats_r - 9'd1);
    assign m_axi_awsize  = 3'(BB_LOG);
    assign m_axi_awburst = 2'b01;
    assign m_axi_awcache = cache_r;
    assign m_axi_awprot  = prot_r;
    assign m_axi_wstrb   = {BB{1'b1}};
    assign m_axi_wlast   = (wd_beat == beats_r - 9'd1);
    assign m_axi_bready  = 1'b1;
    assign m_axi_araddr  = ar_addr;
    assign m_axi_arlen   = 8'(beats_r - 9'd1);
    assign m_axi_arsize  = 3'(BB_LOG);
    assign m_axi_arburst = 2'b01;
    assign m_axi_arcache = cache_r;
    assign m_axi_arprot  = prot_r;
    assign m_axi_rready  = 1'b1;

    function automatic [DATA_W-1:0] pattern(input [63:0] a, input [31:0] seed);
        for (int i = 0; i < LANES; i++)
            pattern[32*i +: 32] = (a[31:0] + 32'(4 * i)) ^ seed;
    endfunction

    assign m_axi_wdata = pattern(wd_addr, seed_r);
    wire [DATA_W-1:0] r_exp = pattern(rd_addr, seed_r);
    wire              r_bad = (m_axi_rdata != r_exp);

    // ------------------------------------------------------------ latency timestamp FIFOs
    reg  [31:0] aw_ts [0:MAX_OUT-1];
    reg  [31:0] ar_ts [0:MAX_OUT-1];
    reg  [$clog2(MAX_OUT)-1:0] aw_ts_wp, aw_ts_rp, ar_ts_wp, ar_ts_rp;
    wire [31:0] wr_lat = cycles[31:0] - aw_ts[aw_ts_rp];
    wire [31:0] rd_lat = cycles[31:0] - ar_ts[ar_ts_rp];

    // ------------------------------------------------------------ progress timeout
    reg  [31:0] idle_cnt;
    wire        waiting  = (w_out != 0) || (r_out != 0) || m_axi_awvalid || m_axi_arvalid || m_axi_wvalid;
    wire        progress = aw_fire || w_fire || b_fire || ar_fire || r_fire;

    // ------------------------------------------------------------ AXI-Lite write decode
    wire        lw_fire  = s_axil_awvalid && s_axil_wvalid && !s_axil_awready && !s_axil_bvalid;
    wire [15:0] lw_addr  = {s_axil_awaddr[15:2], 2'b00};
    wire        lw_full  = (s_axil_wstrb == 4'hF);
    wire        cmd_wr   = lw_fire && lw_full && (lw_addr == 16'h010);
    wire        start    = cmd_wr && !busy && (s_axil_wdata[1:0] != 2'b00);
    wire        clear    = cmd_wr && !busy && s_axil_wdata[31];
    wire        start_ok = beats_ok && align_ok && outst_ok && range_ok;

    assign s_axil_bresp = 2'b00;
    assign s_axil_rresp = 2'b00;

    // a run is finished when no engine is active and none is pending
    wire w_drained = (aw_left == 0 || abort_req || timed_out) && (w_out == 0) && !m_axi_awvalid && !m_axi_wvalid
                     && (aw_ahead == 0);
    wire r_drained = (ar_left == 0 || abort_req || timed_out) && (r_out == 0) && !m_axi_arvalid;

    always @(posedge clk) begin
        if (!resetn) begin
            s_axil_awready <= 1'b0;
            s_axil_wready  <= 1'b0;
            s_axil_bvalid  <= 1'b0;
            cmd_r      <= 32'd0;
            addr_r     <= 64'd0;
            len_r      <= 32'd0;
            beats_r    <= 9'd16;
            outst_r    <= OW'(8);
            seed_r     <= 32'd0;
            timeout_r  <= 32'd250_000_000;
            prot_r     <= 3'b010;
            cache_r    <= 4'b0011;
            win_base_r <= 64'd0;
            win_size_r <= 64'd0;
        end else begin
            s_axil_awready <= 1'b0;
            s_axil_wready  <= 1'b0;
            if (s_axil_bvalid && s_axil_bready)
                s_axil_bvalid <= 1'b0;
            if (lw_fire) begin
                s_axil_awready <= 1'b1;
                s_axil_wready  <= 1'b1;
                s_axil_bvalid  <= 1'b1;
                if (lw_full) begin
                    case (lw_addr)
                        16'h010: cmd_r <= s_axil_wdata;
                        default: ;
                    endcase
                    if (!busy) begin    // run parameters are frozen while busy
                        case (lw_addr)
                            16'h020: addr_r[31:0]     <= s_axil_wdata;
                            16'h024: addr_r[63:32]    <= s_axil_wdata;
                            16'h028: len_r            <= s_axil_wdata;
                            16'h02C: beats_r          <= s_axil_wdata[8:0];
                            16'h030: outst_r          <= OW'(s_axil_wdata);
                            16'h034: seed_r           <= s_axil_wdata;
                            16'h038: timeout_r        <= s_axil_wdata;
                            16'h03C: begin prot_r <= s_axil_wdata[2:0]; cache_r <= s_axil_wdata[7:4]; end
                            16'h040: win_base_r[31:0] <= s_axil_wdata;
                            16'h044: win_base_r[63:32]<= s_axil_wdata;
                            16'h048: win_size_r[31:0] <= s_axil_wdata;
                            16'h04C: win_size_r[63:32]<= s_axil_wdata;
                            default: ;
                        endcase
                    end
                end
            end
        end
    end

    // ------------------------------------------------------------ engines and status
    always @(posedge clk) begin
        if (!resetn) begin
            busy <= 1'b0; done <= 1'b0; bresp_err <= 1'b0; rresp_err <= 1'b0; mismatch <= 1'b0;
            timed_out <= 1'b0; range_err <= 1'b0; aborted <= 1'b0; param_err <= 1'b0;
            w_act <= 1'b0; r_act <= 1'b0; w_pend <= 1'b0; r_pend <= 1'b0;
            no_verify <= 1'b0; abort_req <= 1'b0;
            m_axi_awvalid <= 1'b0; m_axi_wvalid <= 1'b0; m_axi_arvalid <= 1'b0;
            aw_addr <= 64'd0; aw_left <= 32'd0; w_left <= 32'd0; b_left <= 32'd0; w_out <= '0; aw_ahead <= 32'd0;
            wd_addr <= 64'd0; wd_beat <= 9'd0;
            ar_addr <= 64'd0; ar_left <= 32'd0; r_left <= 32'd0; r_out <= '0;
            rd_addr <= 64'd0; rd_beat <= 9'd0;
            w_cycles <= 64'd0; r_cycles <= 64'd0; cycles <= 64'd0;
            w_bursts_rx <= 32'd0; r_beats_rx <= 32'd0; bresp_errs <= 32'd0; rresp_errs <= 32'd0; mismatches <= 32'd0;
            first_bresp <= 2'b00; first_rresp <= 2'b00;
            mm_addr <= 64'd0; mm_exp <= 32'd0; mm_got <= 32'd0;
            rd_lat_min <= 32'hFFFF_FFFF; rd_lat_max <= 32'd0; rd_lat_sum <= 64'd0;
            wr_lat_min <= 32'hFFFF_FFFF; wr_lat_max <= 32'd0; wr_lat_sum <= 64'd0;
            aw_ts_wp <= '0; aw_ts_rp <= '0; ar_ts_wp <= '0; ar_ts_rp <= '0;
            idle_cnt <= 32'd0;
        end else begin
            cycles <= cycles + 64'd1;

            if (cmd_wr && busy && s_axil_wdata[30])
                abort_req <= 1'b1;

            // ---------------- start / clear
            if (clear || start) begin
                done <= 1'b0; bresp_err <= 1'b0; rresp_err <= 1'b0; mismatch <= 1'b0;
                timed_out <= 1'b0; range_err <= 1'b0; aborted <= 1'b0; param_err <= 1'b0;
                w_cycles <= 64'd0; r_cycles <= 64'd0;
                w_bursts_rx <= 32'd0; r_beats_rx <= 32'd0; bresp_errs <= 32'd0; rresp_errs <= 32'd0; mismatches <= 32'd0;
                first_bresp <= 2'b00; first_rresp <= 2'b00;
                mm_addr <= 64'd0; mm_exp <= 32'd0; mm_got <= 32'd0;
                rd_lat_min <= 32'hFFFF_FFFF; rd_lat_max <= 32'd0; rd_lat_sum <= 64'd0;
                wr_lat_min <= 32'hFFFF_FFFF; wr_lat_max <= 32'd0; wr_lat_sum <= 64'd0;
                idle_cnt <= 32'd0;
            end
            if (start) begin
                if (!(beats_ok && align_ok && outst_ok)) begin
                    param_err <= 1'b1; done <= 1'b1;
                end else if (!range_ok) begin
                    range_err <= 1'b1; done <= 1'b1;
                end else begin
                    busy      <= 1'b1;
                    abort_req <= 1'b0;
                    no_verify <= s_axil_wdata[3];
                    aw_addr <= addr_r; aw_left <= n_bursts; w_left <= n_bursts; b_left <= n_bursts;
                    wd_addr <= addr_r; wd_beat <= 9'd0; aw_ahead <= 32'd0;
                    ar_addr <= addr_r; ar_left <= n_bursts; r_left <= n_bursts;
                    rd_addr <= addr_r; rd_beat <= 9'd0;
                    aw_ts_wp <= '0; aw_ts_rp <= '0; ar_ts_wp <= '0; ar_ts_rp <= '0;
                    if (s_axil_wdata[2] || !s_axil_wdata[0]) begin
                        // concurrent, or a single engine
                        w_act  <= s_axil_wdata[0];
                        r_act  <= s_axil_wdata[1];
                        w_pend <= 1'b0; r_pend <= 1'b0;
                    end else begin
                        // write pass first, read pass after it
                        w_act  <= 1'b1;
                        r_act  <= 1'b0;
                        r_pend <= s_axil_wdata[1];
                    end
                end
            end

            // ---------------- write engine
            if (w_act) w_cycles <= w_cycles + 64'd1;
            if (aw_fire) m_axi_awvalid <= 1'b0;
            if (w_issue && aw_left != 0 && (!m_axi_awvalid || aw_fire)
                && ((w_out + (aw_fire ? 1 : 0)) < outst_r) && !(aw_fire && aw_left == 1)) begin
                // next AW (aw_addr/aw_left advance on its handshake)
                m_axi_awvalid <= 1'b1;
            end
            if (aw_fire) begin
                aw_addr  <= aw_addr + (64'd1 << burst_log);
                aw_left  <= aw_left - 32'd1;
                aw_ts[aw_ts_wp] <= cycles[31:0];
                aw_ts_wp <= aw_ts_wp + 1'b1;
            end
            // W follows issued AWs
            if (w_fire) begin
                wd_addr <= wd_addr + 64'(BB);
                wd_beat <= m_axi_wlast ? 9'd0 : wd_beat + 9'd1;
                if (m_axi_wlast) w_left <= w_left - 32'd1;
            end
            begin : w_valid_ctl
                reg [31:0] ahead_n;
                ahead_n = aw_ahead + (aw_fire ? 32'd1 : 32'd0) - ((w_fire && m_axi_wlast) ? 32'd1 : 32'd0);
                aw_ahead <= ahead_n;
                // keep wvalid within a burst; start a burst only when its AW is out
                if (w_fire && m_axi_wlast)
                    m_axi_wvalid <= (ahead_n != 0);
                else if (!m_axi_wvalid)
                    m_axi_wvalid <= (ahead_n != 0);
            end
            if (b_fire) begin
                w_bursts_rx <= w_bursts_rx + 32'd1;
                b_left <= b_left - 32'd1;
                aw_ts_rp <= aw_ts_rp + 1'b1;
                if (wr_lat < wr_lat_min) wr_lat_min <= wr_lat;
                if (wr_lat > wr_lat_max) wr_lat_max <= wr_lat;
                wr_lat_sum <= wr_lat_sum + {32'd0, wr_lat};
                if (m_axi_bresp != 2'b00) begin
                    bresp_errs <= bresp_errs + 32'd1;
                    if (!bresp_err) first_bresp <= m_axi_bresp;
                    bresp_err <= 1'b1;
                end
            end
            w_out <= w_out + (aw_fire ? 1 : 0) - (b_fire ? 1 : 0);

            // ---------------- read engine
            if (r_act) r_cycles <= r_cycles + 64'd1;
            if (ar_fire) m_axi_arvalid <= 1'b0;
            if (r_issue && ar_left != 0 && (!m_axi_arvalid || ar_fire)
                && ((r_out + (ar_fire ? 1 : 0)) < outst_r) && !(ar_fire && ar_left == 1)) begin
                m_axi_arvalid <= 1'b1;
            end
            if (ar_fire) begin
                ar_addr  <= ar_addr + (64'd1 << burst_log);
                ar_left  <= ar_left - 32'd1;
                ar_ts[ar_ts_wp] <= cycles[31:0];
                ar_ts_wp <= ar_ts_wp + 1'b1;
            end
            if (r_fire) begin
                r_beats_rx <= r_beats_rx + 32'd1;
                rd_addr <= rd_addr + 64'(BB);
                rd_beat <= m_axi_rlast ? 9'd0 : rd_beat + 9'd1;
                if (rd_beat == 0) begin
                    ar_ts_rp <= ar_ts_rp + 1'b1;
                    if (rd_lat < rd_lat_min) rd_lat_min <= rd_lat;
                    if (rd_lat > rd_lat_max) rd_lat_max <= rd_lat;
                    rd_lat_sum <= rd_lat_sum + {32'd0, rd_lat};
                end
                if (m_axi_rlast) r_left <= r_left - 32'd1;
                if (m_axi_rresp != 2'b00) begin
                    rresp_errs <= rresp_errs + 32'd1;
                    if (!rresp_err) first_rresp <= m_axi_rresp;
                    rresp_err <= 1'b1;
                end else if (!no_verify && r_bad) begin
                    mismatches <= mismatches + 32'd1;
                    if (!mismatch) begin
                        mm_addr <= rd_addr;
                        mm_exp  <= r_exp[31:0];
                        mm_got  <= m_axi_rdata[31:0];
                    end
                    mismatch <= 1'b1;
                end
            end
            r_out <= r_out + (ar_fire ? 1 : 0) - ((r_fire && m_axi_rlast) ? 1 : 0);

            // ---------------- progress timeout
            if (!busy || progress || !waiting)
                idle_cnt <= 32'd0;
            else if (idle_cnt != 32'hFFFF_FFFF)
                idle_cnt <= idle_cnt + 32'd1;
            if (busy && waiting && idle_cnt >= timeout_r && timeout_r != 0)
                timed_out <= 1'b1;

            // ---------------- completion
            // A timeout only stops new requests: an engine ends when everything
            // it issued has completed, so a wedged bus shows as busy + timeout
            // (valids are never dropped without a handshake).
            if (busy && !start) begin
                if (w_act && w_drained) begin
                    w_act <= 1'b0;
                    if (r_pend && !abort_req && !timed_out) begin
                        r_act <= 1'b1; r_pend <= 1'b0;
                    end else begin
                        r_pend <= 1'b0;
                    end
                end
                if (r_act && r_drained)
                    r_act <= 1'b0;
                if (!w_act && !r_act && !r_pend) begin
                    busy    <= 1'b0;
                    done    <= 1'b1;
                    aborted <= abort_req;
                end
            end
        end
    end

    // ------------------------------------------------------------ AXI-Lite read
    reg        lr_pend;
    reg [15:0] lr_addr;
    always @(posedge clk) begin
        if (!resetn) begin
            s_axil_arready <= 1'b0;
            s_axil_rvalid  <= 1'b0;
            s_axil_rdata   <= 32'd0;
            lr_pend        <= 1'b0;
            lr_addr        <= 16'd0;
        end else begin
            s_axil_arready <= 1'b0;
            if (s_axil_rvalid && s_axil_rready)
                s_axil_rvalid <= 1'b0;
            if (s_axil_arvalid && !s_axil_arready && !lr_pend && !s_axil_rvalid) begin
                s_axil_arready <= 1'b1;
                lr_addr <= {s_axil_araddr[15:2], 2'b00};
                lr_pend <= 1'b1;
            end
            if (lr_pend) begin
                lr_pend <= 1'b0;
                s_axil_rvalid <= 1'b1;
                case (lr_addr)
                    16'h000: s_axil_rdata <= ID;
                    16'h004: s_axil_rdata <= VERSION;
                    16'h008: s_axil_rdata <= BUILD;
                    16'h00C: s_axil_rdata <= {8'(MAX_BEATS > 255 ? 255 : MAX_BEATS), 8'(MAX_OUT), 16'(BB)};
                    16'h010: s_axil_rdata <= cmd_r;
                    16'h014: s_axil_rdata <= {2'b00, 6'(r_out), 2'b00, 6'(w_out), 5'd0, r_act, w_act,
                                              param_err, aborted, range_err, timed_out, mismatch,
                                              rresp_err, bresp_err, done, busy};
                    16'h020: s_axil_rdata <= addr_r[31:0];
                    16'h024: s_axil_rdata <= addr_r[63:32];
                    16'h028: s_axil_rdata <= len_r;
                    16'h02C: s_axil_rdata <= {23'd0, beats_r};
                    16'h030: s_axil_rdata <= 32'(outst_r);
                    16'h034: s_axil_rdata <= seed_r;
                    16'h038: s_axil_rdata <= timeout_r;
                    16'h03C: s_axil_rdata <= {24'd0, cache_r, 1'b0, prot_r};
                    16'h040: s_axil_rdata <= win_base_r[31:0];
                    16'h044: s_axil_rdata <= win_base_r[63:32];
                    16'h048: s_axil_rdata <= win_size_r[31:0];
                    16'h04C: s_axil_rdata <= win_size_r[63:32];
                    16'h060: s_axil_rdata <= w_cycles[31:0];
                    16'h064: s_axil_rdata <= w_cycles[63:32];
                    16'h068: s_axil_rdata <= r_cycles[31:0];
                    16'h06C: s_axil_rdata <= r_cycles[63:32];
                    16'h070: s_axil_rdata <= w_bursts_rx;
                    16'h074: s_axil_rdata <= r_beats_rx;
                    16'h078: s_axil_rdata <= bresp_errs;
                    16'h07C: s_axil_rdata <= rresp_errs;
                    16'h080: s_axil_rdata <= mismatches;
                    16'h084: s_axil_rdata <= {22'd0, first_rresp, 6'd0, first_bresp};
                    16'h088: s_axil_rdata <= mm_addr[31:0];
                    16'h08C: s_axil_rdata <= mm_addr[63:32];
                    16'h090: s_axil_rdata <= mm_exp;
                    16'h094: s_axil_rdata <= mm_got;
                    16'h0A0: s_axil_rdata <= rd_lat_min;
                    16'h0A4: s_axil_rdata <= rd_lat_max;
                    16'h0A8: s_axil_rdata <= rd_lat_sum[31:0];
                    16'h0AC: s_axil_rdata <= rd_lat_sum[63:32];
                    16'h0B0: s_axil_rdata <= wr_lat_min;
                    16'h0B4: s_axil_rdata <= wr_lat_max;
                    16'h0B8: s_axil_rdata <= wr_lat_sum[31:0];
                    16'h0BC: s_axil_rdata <= wr_lat_sum[63:32];
                    16'h0C0: s_axil_rdata <= cycles[31:0];
                    16'h0C4: s_axil_rdata <= cycles[63:32];
                    default: s_axil_rdata <= 32'd0;
                endcase
            end
        end
    end
endmodule
