`timescale 1ns / 1ps

// tb_rm_mmio: rm_mmio_bd against a behavioural single-beat AXI slave with
// random ready/response delays. Checks the register block, the window and
// parameter guards (no AXI traffic on a refused command), the AXI attributes
// PG347 asks for (AxLEN 0, AxSIZE 2, AxCACHE 0000, all strobes), write and
// read data, error responses, the timeout flag and clear.
module tb_rm_mmio;
    reg clk = 0, resetn = 0;
    always #2 clk = ~clk;

    // AXI-Lite
    reg  [15:0] awaddr = 0, araddr = 0;
    reg         awvalid = 0, wvalid = 0, bready = 1, arvalid = 0, rready = 1;
    reg  [31:0] wdata = 0;
    reg  [3:0]  wstrb = 4'hF;
    wire        awready, wready, bvalid, arready, rvalid;
    wire [1:0]  bresp, rresp;
    wire [31:0] rdata;

    // AXI master side
    wire [63:0] m_awaddr, m_araddr;
    wire [7:0]  m_awlen, m_arlen;
    wire [2:0]  m_awsize, m_arsize, m_awprot, m_arprot;
    wire [1:0]  m_awburst, m_arburst;
    wire [3:0]  m_awcache, m_arcache, m_wstrb;
    wire        m_awvalid, m_wvalid, m_wlast, m_bready, m_arvalid, m_rready;
    wire [31:0] m_wdata;
    reg         m_awready = 0, m_wready = 0, m_bvalid = 0, m_arready = 0, m_rvalid = 0;
    reg  [1:0]  m_bresp = 0, m_rresp = 0;
    reg  [31:0] m_rdata = 0;

    rm_mmio_bd #(.BUILD(7)) dut (
        .clk(clk), .resetn(resetn),
        .s_axil_awaddr(awaddr), .s_axil_awvalid(awvalid), .s_axil_awready(awready),
        .s_axil_wdata(wdata), .s_axil_wstrb(wstrb), .s_axil_wvalid(wvalid), .s_axil_wready(wready),
        .s_axil_bresp(bresp), .s_axil_bvalid(bvalid), .s_axil_bready(bready),
        .s_axil_araddr(araddr), .s_axil_arvalid(arvalid), .s_axil_arready(arready),
        .s_axil_rdata(rdata), .s_axil_rresp(rresp), .s_axil_rvalid(rvalid), .s_axil_rready(rready),
        .m_axi_awaddr(m_awaddr), .m_axi_awlen(m_awlen), .m_axi_awsize(m_awsize), .m_axi_awburst(m_awburst),
        .m_axi_awcache(m_awcache), .m_axi_awprot(m_awprot), .m_axi_awvalid(m_awvalid), .m_axi_awready(m_awready),
        .m_axi_wdata(m_wdata), .m_axi_wstrb(m_wstrb), .m_axi_wlast(m_wlast), .m_axi_wvalid(m_wvalid),
        .m_axi_wready(m_wready), .m_axi_bresp(m_bresp), .m_axi_bvalid(m_bvalid), .m_axi_bready(m_bready),
        .m_axi_araddr(m_araddr), .m_axi_arlen(m_arlen), .m_axi_arsize(m_arsize), .m_axi_arburst(m_arburst),
        .m_axi_arcache(m_arcache), .m_axi_arprot(m_arprot), .m_axi_arvalid(m_arvalid), .m_axi_arready(m_arready),
        .m_axi_rdata(m_rdata), .m_axi_rresp(m_rresp), .m_axi_rlast(1'b1), .m_axi_rvalid(m_rvalid),
        .m_axi_rready(m_rready)
    );

    integer errors = 0;
    task automatic check(input bit cond, input string what);
        if (!cond) begin errors++; $display("FAIL: %s", what); end
    endtask

    // ------------------------------------------------------------ AXI-Lite BFM
    task automatic lw(input [15:0] a, input [31:0] d);
        @(posedge clk);
        awaddr <= a; wdata <= d; awvalid <= 1; wvalid <= 1;
        do @(posedge clk); while (!(awready && wready));
        awvalid <= 0; wvalid <= 0;
        while (!bvalid) @(posedge clk);
        @(posedge clk);
    endtask
    task automatic lr(input [15:0] a, output [31:0] d);
        @(posedge clk);
        araddr <= a; arvalid <= 1;
        do @(posedge clk); while (!arready);
        arvalid <= 0;
        while (!rvalid) @(posedge clk);
        d = rdata;
        @(posedge clk);
    endtask

    // ------------------------------------------------------------ AXI slave model
    reg  [31:0] mem [0:255];
    reg  [1:0]  next_resp = 2'b00;
    integer     b_hold = 0;          // extra cycles before B (timeout test)
    integer     n_aw = 0, n_w = 0, n_ar = 0;
    reg  [63:0] last_awaddr, last_araddr;
    reg  [31:0] last_wdata;
    reg  [3:0]  last_wstrb;
    reg         attr_ok = 1;

    always @(posedge clk) begin
        m_awready <= ($urandom % 3) == 0;
        m_wready  <= ($urandom % 3) == 0;
        m_arready <= ($urandom % 3) == 0;
    end
    reg aw_got = 0, w_got = 0;
    integer b_wait = 0;
    always @(posedge clk) begin
        if (m_awvalid && m_awready) begin
            n_aw++; aw_got = 1; last_awaddr = m_awaddr;
            if (m_awlen != 0 || m_awsize != 2 || m_awburst != 1 || m_awcache[1] || m_awprot != 3'b010) attr_ok = 0;
        end
        if (m_wvalid && m_wready) begin
            n_w++; w_got = 1; last_wdata = m_wdata; last_wstrb = m_wstrb;
            if (!m_wlast) attr_ok = 0;
        end
        if (m_bvalid && m_bready) m_bvalid <= 0;
        if (aw_got && w_got && !m_bvalid) begin
            if (b_wait >= b_hold + ($urandom % 5)) begin
                mem[last_awaddr[9:2]] <= last_wdata;
                m_bvalid <= 1; m_bresp <= next_resp;
                aw_got = 0; w_got = 0; b_wait = 0;
            end else b_wait++;
        end
        if (m_rvalid && m_rready) m_rvalid <= 0;
        if (m_arvalid && m_arready) begin
            n_ar++; last_araddr = m_araddr;
            if (m_arlen != 0 || m_arsize != 2 || m_arcache[1]) attr_ok = 0;
            m_rvalid <= 1; m_rresp <= next_resp; m_rdata <= mem[m_araddr[9:2]];
        end
    end

    task automatic wait_idle(output [31:0] st);
        integer n = 0;
        do begin lr(16'h014, st); n++; end while (st[0] && n < 200);
    endtask

    reg [31:0] v, st;
    initial begin
        for (int i = 0; i < 256; i++) mem[i] = 32'hDEAD_0000 + i;
        repeat (5) @(posedge clk);
        resetn <= 1;
        repeat (3) @(posedge clk);

        lr(16'h000, v); check(v == 32'h524D_494F, "ID");
        lr(16'h008, v); check(v == 7, "BUILD");
        lr(16'h034, v); check(v == 32'h0000_0002, "AXI_ATTR reset (CACHE 0000, PROT 010)");
        lr(16'h048, v); check(v == 32'h1000_0000, "WIN_BASE_LO");
        lr(16'h04C, v); check(v == 32'h0000_0007, "WIN_BASE_HI");
        lr(16'h050, v); check(v == 32'h0100_0000, "WIN_SIZE");
        lw(16'h018, 32'hCAFE_F00D); lr(16'h018, v); check(v == 32'hCAFE_F00D, "SCRATCH0");
        lw(16'h01C, 32'h1234_5678); lr(16'h01C, v); check(v == 32'h1234_5678, "SCRATCH1");

        // refused commands: no AXI traffic
        lw(16'h020, 32'h0FFF_FFFC); lw(16'h024, 32'h7);        // below the window
        lw(16'h010, 1); wait_idle(st);
        check(st[4] && st[1] && !st[0], "range_err below window");
        lw(16'h020, 32'h1100_0000);                              // at the window end
        lw(16'h010, 2); wait_idle(st);
        check(st[4], "range_err at window end");
        lw(16'h020, 32'h1000_0002);                              // misaligned
        lw(16'h010, 1); wait_idle(st);
        check(st[4], "range_err misaligned");
        lw(16'h020, 32'h1000_0000);
        lw(16'h010, 3); wait_idle(st);
        check(st[5] && !st[4], "param_err for write+read");
        check(n_aw == 0 && n_w == 0 && n_ar == 0, "no AXI traffic for refused commands");

        // write in the window (an H2C PIDX doorbell offset of fn 1, queue 2)
        lw(16'h020, 32'h1001_8024); lw(16'h024, 32'h7); lw(16'h028, 32'h0000_0040);
        lw(16'h010, 1); wait_idle(st);
        check(st[1] && !st[2] && !st[4] && st[9:8] == 0, "write done OKAY");
        check(n_aw == 1 && n_w == 1, "one AW, one W");
        check(last_awaddr == 64'h7_1001_8024, "AWADDR");
        check(last_wdata == 32'h40 && last_wstrb == 4'hF, "WDATA/WSTRB");
        lr(16'h038, v); check(v == 1, "N_WRITES 1");
        lr(16'h044, v); check(v > 0, "LAT_LAST set");

        // read it back
        lw(16'h010, 2); wait_idle(st);
        lr(16'h02C, v); check(v == 32'h40, "RDATA");
        lr(16'h03C, v); check(v == 1, "N_READS 1");
        check(n_ar == 1 && last_araddr == 64'h7_1001_8024, "ARADDR");

        // error response
        next_resp = 2'b10;
        lw(16'h010, 1); wait_idle(st);
        check(st[2] && st[9:8] == 2'b10, "resp_err SLVERR on write");
        lw(16'h010, 2); wait_idle(st);
        check(st[2], "resp_err SLVERR on read");
        lr(16'h040, v); check(v == 2, "N_ERRS 2");
        next_resp = 2'b00;

        // timeout flag while busy, then completion
        lw(16'h030, 20);
        b_hold = 60;
        lw(16'h010, 1);
        repeat (40) @(posedge clk);
        lr(16'h014, st); check(st[0] && st[3], "timeout while busy");
        lw(16'h020, 32'h1000_0100);                               // held while busy
        lr(16'h020, v); check(v == 32'h1001_8024, "ADDR held while busy");
        wait_idle(st);
        check(!st[0] && st[1] && st[3], "completes after timeout, flag kept");
        b_hold = 0;

        // clear
        lw(16'h010, 32'h8000_0000);
        lr(16'h014, st); check(st == 0, "clear status");
        lr(16'h038, v); check(v == 0, "clear N_WRITES");
        lr(16'h054, v); check(v > 20, "N_LITE_WR counts");

        check(attr_ok, "AXI attributes (LEN 0, SIZE 2, INCR, CACHE[1] 0, PROT 010, WLAST)");
        if (errors == 0) $display("PASS: tb_rm_mmio");
        else             $display("FAIL: tb_rm_mmio, %0d errors", errors);
        $finish;
    end
    initial begin #2000000; $display("FAIL: tb_rm_mmio timeout"); $finish; end
endmodule
