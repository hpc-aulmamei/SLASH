`timescale 1ns / 1ps

// Testbench of host_probe: behavioural AXI4 slave memory (random ready and
// response latency, an SLVERR address range, a stuck mode without B/R) and
// protocol checks on the probe's master port.
module tb_host_probe;
    localparam integer DW = 512, BB = DW / 8, LANES = DW / 32;
    localparam integer MAX_OUT = 32;

    reg clk = 0, resetn = 0;
    always #2 clk = ~clk;                       // 250 MHz

    // AXI-Lite
    reg  [15:0] awaddr, araddr; reg awvalid = 0, wvalid = 0, arvalid = 0, bready = 1, rready = 1;
    reg  [31:0] wdata; wire awready, wready, bvalid, arready, rvalid; wire [1:0] bresp, rresp; wire [31:0] rdata;
    // AXI4
    wire [63:0] m_awaddr, m_araddr; wire [7:0] m_awlen, m_arlen; wire [2:0] m_awsize, m_arsize, m_awprot, m_arprot;
    wire [1:0] m_awburst, m_arburst; wire [3:0] m_awcache, m_arcache;
    wire m_awvalid, m_wvalid, m_wlast, m_arvalid, m_bready, m_rready;
    wire [DW-1:0] m_wdata; wire [BB-1:0] m_wstrb;
    reg  m_awready = 0, m_wready = 0, m_arready = 0, m_bvalid = 0, m_rvalid = 0, m_rlast = 0;
    reg  [1:0] m_bresp = 0, m_rresp = 0; reg [DW-1:0] m_rdata = 0;

    host_probe #(.DATA_W(DW), .MAX_OUT(MAX_OUT), .BUILD(32'd14)) dut (
        .clk(clk), .resetn(resetn),
        .s_axil_awaddr(awaddr), .s_axil_awvalid(awvalid), .s_axil_awready(awready),
        .s_axil_wdata(wdata), .s_axil_wstrb(4'hF), .s_axil_wvalid(wvalid), .s_axil_wready(wready),
        .s_axil_bresp(bresp), .s_axil_bvalid(bvalid), .s_axil_bready(bready),
        .s_axil_araddr(araddr), .s_axil_arvalid(arvalid), .s_axil_arready(arready),
        .s_axil_rdata(rdata), .s_axil_rresp(rresp), .s_axil_rvalid(rvalid), .s_axil_rready(rready),
        .m_axi_awaddr(m_awaddr), .m_axi_awlen(m_awlen), .m_axi_awsize(m_awsize), .m_axi_awburst(m_awburst),
        .m_axi_awcache(m_awcache), .m_axi_awprot(m_awprot), .m_axi_awvalid(m_awvalid), .m_axi_awready(m_awready),
        .m_axi_wdata(m_wdata), .m_axi_wstrb(m_wstrb), .m_axi_wlast(m_wlast), .m_axi_wvalid(m_wvalid), .m_axi_wready(m_wready),
        .m_axi_bresp(m_bresp), .m_axi_bvalid(m_bvalid), .m_axi_bready(m_bready),
        .m_axi_araddr(m_araddr), .m_axi_arlen(m_arlen), .m_axi_arsize(m_arsize), .m_axi_arburst(m_arburst),
        .m_axi_arcache(m_arcache), .m_axi_arprot(m_arprot), .m_axi_arvalid(m_arvalid), .m_axi_arready(m_arready),
        .m_axi_rdata(m_rdata), .m_axi_rresp(m_rresp), .m_axi_rlast(m_rlast), .m_axi_rvalid(m_rvalid), .m_axi_rready(m_rready));

    integer errors = 0;
    task automatic fail(input string msg); begin errors++; $display("ERROR @%0t: %s", $time, msg); end endtask

    // ------------------------------------------------------------ AXI-Lite master
    task automatic lw(input [15:0] a, input [31:0] d);
        @(posedge clk); awaddr <= a; wdata <= d; awvalid <= 1; wvalid <= 1;
        do @(posedge clk); while (!(awready && wready));
        awvalid <= 0; wvalid <= 0;
        while (!bvalid) @(posedge clk);
    endtask
    task automatic lr(input [15:0] a, output [31:0] d);
        @(posedge clk); araddr <= a; arvalid <= 1;
        do @(posedge clk); while (!arready);
        arvalid <= 0;
        while (!rvalid) @(posedge clk);
        d = rdata;
    endtask
    function automatic [31:0] pat(input [63:0] a, input [31:0] seed); pat = a[31:0] ^ seed; endfunction

    // ------------------------------------------------------------ AXI slave model
    bit [DW-1:0] mem [longint];
    longint err_lo = -1, err_hi = -1;          // [err_lo, err_hi) -> SLVERR
    bit stuck = 0;                             // stop sending B and R
    int rdy_pct = 70, lat_min = 10, lat_max = 120;
    int out_w = 0, out_r = 0, max_out_w = 0, max_out_r = 0;

    // write side: AW queue, W beats into memory, B after a latency
    typedef struct { longint addr; int len; } burst_t;
    burst_t awq[$], arq[$];
    longint b_due[$]; bit [1:0] b_code[$];

    always @(posedge clk) begin
        m_awready <= ($urandom_range(99) < rdy_pct);
        m_wready  <= ($urandom_range(99) < rdy_pct);
        m_arready <= ($urandom_range(99) < rdy_pct);
    end

    // protocol checks: payload stable while valid and not ready
    reg [63:0] p_awaddr, p_araddr; reg p_awv = 0, p_arv = 0, p_wv = 0; reg [DW-1:0] p_wdata;
    always @(posedge clk) if (resetn) begin
        if (p_awv && !(m_awvalid && m_awaddr == p_awaddr)) fail("AWVALID/AWADDR changed before handshake");
        if (p_arv && !(m_arvalid && m_araddr == p_araddr)) fail("ARVALID/ARADDR changed before handshake");
        if (p_wv  && !(m_wvalid && m_wdata == p_wdata))   fail("WVALID/WDATA changed before handshake");
        p_awv <= m_awvalid && !m_awready; p_awaddr <= m_awaddr;
        p_arv <= m_arvalid && !m_arready; p_araddr <= m_araddr;
        p_wv  <= m_wvalid && !m_wready;   p_wdata  <= m_wdata;
        if (!m_bready || !m_rready) fail("bready/rready low");
    end

    task automatic check_burst(input longint a, input int len, input [2:0] size, input [1:0] burst, input string ch);
        if (size != 3'd6) fail($sformatf("%s size %0d", ch, size));
        if (burst != 2'b01) fail($sformatf("%s burst type %0d", ch, burst));
        if ((a & (BB - 1)) != 0) fail($sformatf("%s unaligned 0x%0h", ch, a));
        if ((a >> 12) != ((a + (len + 1) * BB - 1) >> 12)) fail($sformatf("%s crosses 4K at 0x%0h len %0d", ch, a, len));
    endtask

    longint w_addr; int w_beat = 0; bit w_have = 0; burst_t w_cur;
    always @(posedge clk) if (resetn) begin
        if (m_awvalid && m_awready) begin
            check_burst(m_awaddr, m_awlen, m_awsize, m_awburst, "AW");
            awq.push_back('{m_awaddr, m_awlen});
            out_w++; if (out_w > max_out_w) max_out_w = out_w;
        end
        if (m_wvalid && m_wready) begin
            if (!w_have) begin
                if (awq.size() == 0) fail("W beat before its AW");
                else begin w_cur = awq.pop_front(); w_have = 1; w_beat = 0; w_addr = w_cur.addr; end
            end
            if (m_wstrb != {BB{1'b1}}) fail("partial WSTRB");
            if (m_wlast != (w_beat == w_cur.len)) fail($sformatf("WLAST wrong at beat %0d of %0d", w_beat, w_cur.len));
            mem[w_addr] = m_wdata;
            w_addr += BB; w_beat++;
            if (m_wlast) begin
                w_have = 0;
                b_due.push_back($time + 4 * $urandom_range(lat_max, lat_min));
                b_code.push_back((w_cur.addr >= err_lo && w_cur.addr < err_hi) ? 2'b10 : 2'b00);
            end
        end
        if (m_bvalid) begin           // bready is always 1
            m_bvalid <= 0; out_w--;
        end
        if (!stuck && b_due.size() != 0 && $time >= b_due[0] && !(m_bvalid)) begin
            void'(b_due.pop_front()); m_bresp <= b_code.pop_front(); m_bvalid <= 1;
        end
    end

    // read side
    longint r_due[$]; longint r_addr; int r_beat = 0; bit r_have = 0; burst_t r_cur;
    always @(posedge clk) if (resetn) begin
        if (m_arvalid && m_arready) begin
            check_burst(m_araddr, m_arlen, m_arsize, m_arburst, "AR");
            arq.push_back('{m_araddr, m_arlen});
            r_due.push_back($time + 4 * $urandom_range(lat_max, lat_min));
            out_r++; if (out_r > max_out_r) max_out_r = out_r;
        end
        m_rvalid <= 0;
        if (!stuck) begin
            if (!r_have && arq.size() != 0 && $time >= r_due[0]) begin
                r_cur = arq.pop_front(); void'(r_due.pop_front()); r_have = 1; r_beat = 0; r_addr = r_cur.addr;
            end
            if (r_have && $urandom_range(99) < 80) begin
                m_rvalid <= 1;
                m_rdata  <= mem.exists(r_addr) ? mem[r_addr] : '0;
                m_rresp  <= (r_cur.addr >= err_lo && r_cur.addr < err_hi) ? 2'b10 : 2'b00;
                m_rlast  <= (r_beat == r_cur.len);
                if (r_beat == r_cur.len) begin r_have = 0; out_r--; end
                r_addr += BB; r_beat++;
            end
        end
    end

    // ------------------------------------------------------------ helpers
    localparam [15:0] CMD = 16'h010, STATUS = 16'h014;
    task automatic setup(input longint a, input int len, input int beats, input int outst, input [31:0] seed);
        lw(16'h020, a[31:0]); lw(16'h024, a[63:32]); lw(16'h028, len);
        lw(16'h02C, beats); lw(16'h030, outst); lw(16'h034, seed);
    endtask
    task automatic window(input longint base, input longint size);
        lw(16'h040, base[31:0]); lw(16'h044, base[63:32]); lw(16'h048, size[31:0]); lw(16'h04C, size[63:32]);
    endtask
    task automatic wait_done(output [31:0] st, input int max_cycles = 2_000_000);
        int n = 0;
        do begin lr(STATUS, st); n++; end while (st[0] && n < max_cycles / 8);
    endtask
    task automatic rd(input [15:0] a, output [31:0] d); lr(a, d); endtask
    function automatic bit mem_ok(input longint a, input int len, input [31:0] seed);
        for (longint x = a; x < a + len; x += BB) begin
            if (!mem.exists(x)) return 0;
            for (int i = 0; i < LANES; i++) if (mem[x][32*i +: 32] != pat(x + 4 * i, seed)) return 0;
        end
        return 1;
    endfunction
    task automatic expect_eq(input string what, input [31:0] got, input [31:0] exp);
        if (got !== exp) fail($sformatf("%s = 0x%0h, expected 0x%0h", what, got, exp));
    endtask

    localparam longint BASE = 64'h0000_0000_E800_0000;
    reg [31:0] st, v, v2;

    initial begin
        repeat (10) @(posedge clk); resetn = 1; repeat (5) @(posedge clk);

        rd(16'h000, v); expect_eq("ID", v, 32'h48505242);
        rd(16'h008, v); expect_eq("BUILD", v, 14);
        rd(16'h00C, v); expect_eq("CAPS", v, {8'd64, 8'd32, 16'd64});

        // 1. no window programmed: refused, no traffic
        setup(BASE, 4096, 16, 4, 32'h1234_5678);
        lw(CMD, 1); wait_done(st);
        if (!st[6] || !st[1]) fail($sformatf("start without window not refused, STATUS 0x%0h", st));
        if (out_w != 0 || mem.size() != 0) fail("traffic without window");

        // 2. window too small / bad parameters
        window(BASE, 64'h20_0000);                       // 2 MiB
        setup(BASE + 64'h1F_F000, 8192, 16, 4, 1);       // runs past the window end
        lw(CMD, 1); wait_done(st); if (!st[6]) fail("run past window end not refused");
        setup(BASE + 64, 4096, 16, 4, 1);                // misaligned for 1 KiB bursts
        lw(CMD, 1); wait_done(st); if (!st[8]) fail("misaligned start not refused");
        setup(BASE, 4096, 3, 4, 1);                      // beats not a power of two
        lw(CMD, 1); wait_done(st); if (!st[8]) fail("beats=3 not refused");
        setup(BASE, 4096, 128, 4, 1);                    // 8 KiB burst
        lw(CMD, 1); wait_done(st); if (!st[8]) fail("beats=128 not refused");
        if (mem.size() != 0) fail("traffic on refused start");

        // 3. write 64 KiB, 4 KiB bursts, 8 outstanding
        setup(BASE, 65536, 64, 8, 32'hA5A5_0001);
        lw(CMD, 1); wait_done(st);
        if (st[0] || !st[1] || (st[8:2] != 0)) fail($sformatf("write run STATUS 0x%0h", st));
        rd(16'h070, v); expect_eq("W_BURSTS", v, 16);
        if (!mem_ok(BASE, 65536, 32'hA5A5_0001)) fail("memory != pattern after write");
        if (max_out_w > 8) fail($sformatf("write outstanding %0d > 8", max_out_w));
        rd(16'h0B0, v); rd(16'h0B4, v2); if (v == 0 || v > v2) fail("write latency min/max");

        // 4. read back and verify
        lw(CMD, 2); wait_done(st);
        if (st[8:2] != 0) fail($sformatf("read run STATUS 0x%0h", st));
        rd(16'h074, v); expect_eq("R_BEATS", v, 1024);
        rd(16'h080, v); expect_eq("MISMATCHES", v, 0);
        if (max_out_r > 8) fail($sformatf("read outstanding %0d > 8", max_out_r));
        rd(16'h068, v); if (v == 0) fail("R_CYCLES zero");

        // 5. a corrupted word is found at the right address
        mem[BASE + 64'h1240][31:0] = 32'hDEAD_BEEF;
        lw(CMD, 2); wait_done(st);
        if (!st[4]) fail("mismatch not flagged");
        rd(16'h080, v); expect_eq("MISMATCHES", v, 1);
        rd(16'h088, v); expect_eq("MM_ADDR_LO", v, 32'hE800_1240);
        rd(16'h094, v); expect_eq("MM_GOT", v, 32'hDEAD_BEEF);
        rd(16'h090, v); expect_eq("MM_EXP", v, pat(BASE + 64'h1240, 32'hA5A5_0001));
        lw(CMD, 32'h0000_000A); wait_done(st);             // read with no-verify
        if (st[4]) fail("mismatch flagged with no-verify");

        // 6. write then read in one command, single-beat bursts, 32 outstanding
        max_out_w = 0; max_out_r = 0; rdy_pct = 95; lat_min = 40; lat_max = 300;
        setup(BASE + 64'h10_0000, 32768, 1, 32, 32'h0BAD_F00D);
        lw(CMD, 3); wait_done(st);
        if (st[8:2] != 0) fail($sformatf("write+read STATUS 0x%0h", st));
        rd(16'h070, v); expect_eq("W_BURSTS (1-beat)", v, 512);
        rd(16'h074, v); expect_eq("R_BEATS (1-beat)", v, 512);
        if (!mem_ok(BASE + 64'h10_0000, 32768, 32'h0BAD_F00D)) fail("memory after write+read");
        if (max_out_w > 32 || max_out_r > 32) fail("outstanding > 32");
        if (max_out_w < 16) fail($sformatf("write outstanding only reached %0d", max_out_w));
        $display("1-beat run: max outstanding w %0d r %0d", max_out_w, max_out_r);

        // 7. concurrent write and read
        setup(BASE + 64'h10_0000, 32768, 16, 16, 32'h0BAD_F00D);
        lw(CMD, 32'h7); wait_done(st);
        if (st[8:2] != 0) fail($sformatf("concurrent STATUS 0x%0h", st));
        rd(16'h070, v); expect_eq("W_BURSTS (concurrent)", v, 32);
        rd(16'h074, v); expect_eq("R_BEATS (concurrent)", v, 512);

        // 8. error responses are counted
        err_lo = BASE + 64'h2000; err_hi = BASE + 64'h3000;
        setup(BASE, 16384, 16, 4, 32'h1);
        lw(CMD, 3); wait_done(st);
        if (!st[2] || !st[3]) fail($sformatf("error responses not flagged, STATUS 0x%0h", st));
        rd(16'h078, v); expect_eq("BRESP_ERRS", v, 4);
        rd(16'h07C, v); expect_eq("RRESP_ERRS", v, 64);
        rd(16'h084, v); expect_eq("FIRST_RESP", v, 32'h0000_0202);
        err_lo = -1; err_hi = -1;

        // 9. stuck slave: timeout flagged, busy until it drains
        lw(16'h038, 2000);
        setup(BASE, 65536, 64, 8, 32'h2);
        stuck = 1;
        lw(CMD, 1);
        repeat (6000) @(posedge clk);
        rd(STATUS, st);
        if (!st[5] || !st[0]) fail($sformatf("stuck bus: STATUS 0x%0h (want busy + timeout)", st));
        stuck = 0;
        wait_done(st);
        if (st[0] || !st[5]) fail($sformatf("after unstick: STATUS 0x%0h", st));
        rd(16'h070, v); if (v == 0 || v >= 16) fail($sformatf("bursts after timeout %0d (want issued ones only)", v));
        lw(16'h038, 250_000_000);

        // 10. abort
        lat_min = 200; lat_max = 400;
        setup(BASE, 1 << 20, 64, 2, 32'h3);
        lw(CMD, 1); repeat (200) @(posedge clk); lw(CMD, 32'h4000_0000);
        wait_done(st);
        if (!st[7] || st[0]) fail($sformatf("abort: STATUS 0x%0h", st));
        rd(16'h070, v); if (v >= 256) fail("abort did not stop the run");

        // 11. clear
        lw(CMD, 32'h8000_0000); rd(STATUS, st);
        if (st[8:1] != 0) fail($sformatf("clear: STATUS 0x%0h", st));

        if (errors == 0) $display("TB_HOST_PROBE PASS");
        else $display("TB_HOST_PROBE FAIL (%0d errors)", errors);
        $finish;
    end

    initial begin #20ms; $display("TB_HOST_PROBE FAIL (global timeout)"); $finish; end
endmodule
