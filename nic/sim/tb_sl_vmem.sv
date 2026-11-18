/**
 * The MIT License (MIT)
 * Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 * NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

`timescale 1ns / 1ps

// Self-checking testbench of sl_vmem (through sl_vmem_bd).
//
// Upstream: an AXI master with independent AW, W and AR drivers (W beats may
// lead their AW), random valid gaps and random B/R ready. Downstream: an AXI
// slave with random ready, a byte memory for writes, read data that is a
// function of the address, B and R returned out of order across IDs and R
// beats interleaved across IDs, DECERR from window 61.
//
// The expected outcome of every request is computed from a model of the table
// when its AW/AR handshake happens: the forwarded address downstream (in
// order), the response per ID (in order), the read data, and the host memory
// after the writes. Downstream, any request that was not expected (a fault
// that leaked) is an error. AXI valid/payload stability is checked on every
// channel the DUT drives.
//
// Phases: reset state and table clear, registers and table bits, translation
// of every address bit, all burst lengths, each fault cause in both
// directions behind traffic in flight, random traffic under several
// back-pressure profiles, pause/drain with a table change, reset during
// traffic.
module tb_sl_vmem;
    localparam integer ID_W = 6;
    localparam integer DW   = 128;
    localparam integer NB   = DW / 8;
    localparam [1:0] OKAY = 2'b00, SLVERR = 2'b10, DECERR = 2'b11;
    localparam [63:0] VA_BASE = 64'h0000_0208_0000_0000;
    localparam integer POISON_WIN = 61;      // the slave answers DECERR there

    reg clk = 1'b0;
    always #2 clk = ~clk;                    // 250 MHz
    reg aresetn = 1'b0;

    // ---------------------------------------------------------------- DUT
    reg  [17:0] l_awaddr = 0, l_araddr = 0;
    reg         l_awvalid = 0, l_wvalid = 0, l_bready = 0, l_arvalid = 0, l_rready = 0;
    reg  [31:0] l_wdata = 0;
    wire        l_awready, l_wready, l_bvalid, l_arready, l_rvalid;
    wire [1:0]  l_bresp, l_rresp;
    wire [31:0] l_rdata;

    reg  [ID_W-1:0] s_awid = 0, s_arid = 0;
    reg  [63:0]     s_awaddr = 0, s_araddr = 0;
    reg  [7:0]      s_awlen = 0, s_arlen = 0;
    reg  [2:0]      s_awsize = 0, s_arsize = 0, s_awprot = 0, s_arprot = 0;
    reg  [1:0]      s_awburst = 0, s_arburst = 0;
    reg             s_awlock = 0, s_arlock = 0, s_awuser = 0, s_aruser = 0;
    reg  [3:0]      s_awcache = 0, s_arcache = 0, s_awqos = 0, s_arqos = 0;
    reg  [3:0]      s_awregion = 0, s_arregion = 0;
    reg             s_awvalid = 0, s_arvalid = 0;
    wire            s_awready, s_arready;
    reg  [DW-1:0]   s_wdata = 0;
    reg  [NB-1:0]   s_wstrb = 0;
    reg             s_wlast = 0, s_wuser = 0, s_wvalid = 0;
    wire            s_wready;
    wire [ID_W-1:0] s_bid, s_rid;
    wire [1:0]      s_bresp, s_rresp;
    wire            s_buser, s_bvalid, s_rlast, s_ruser, s_rvalid;
    reg             s_bready = 0, s_rready = 0;
    wire [DW-1:0]   s_rdata;

    wire [ID_W-1:0] m_awid, m_arid;
    wire [63:0]     m_awaddr, m_araddr;
    wire [7:0]      m_awlen, m_arlen;
    wire [2:0]      m_awsize, m_arsize, m_awprot, m_arprot;
    wire [1:0]      m_awburst, m_arburst;
    wire            m_awlock, m_arlock, m_awuser, m_aruser;
    wire [3:0]      m_awcache, m_arcache, m_awqos, m_arqos, m_awregion, m_arregion;
    wire            m_awvalid, m_arvalid;
    reg             m_awready = 0, m_arready = 0;
    wire [DW-1:0]   m_wdata;
    wire [NB-1:0]   m_wstrb;
    wire            m_wlast, m_wuser, m_wvalid;
    reg             m_wready = 0;
    reg  [ID_W-1:0] m_bid = 0, m_rid = 0;
    reg  [1:0]      m_bresp = 0, m_rresp = 0;
    reg             m_buser = 0, m_bvalid = 0, m_rlast = 0, m_ruser = 0, m_rvalid = 0;
    wire            m_bready, m_rready;
    reg  [DW-1:0]   m_rdata = 0;

    sl_vmem_bd #(.AXI_ID_WIDTH(ID_W), .AXI_DATA_WIDTH(DW), .BUILD(7)) dut (
        .aclk(clk), .aresetn(aresetn),
        .s_axil_awaddr(l_awaddr), .s_axil_awvalid(l_awvalid), .s_axil_awready(l_awready),
        .s_axil_wdata(l_wdata), .s_axil_wstrb(4'hF), .s_axil_wvalid(l_wvalid),
        .s_axil_wready(l_wready), .s_axil_bresp(l_bresp), .s_axil_bvalid(l_bvalid),
        .s_axil_bready(l_bready), .s_axil_araddr(l_araddr), .s_axil_arvalid(l_arvalid),
        .s_axil_arready(l_arready), .s_axil_rdata(l_rdata), .s_axil_rresp(l_rresp),
        .s_axil_rvalid(l_rvalid), .s_axil_rready(l_rready),
        .s_axi_awid(s_awid), .s_axi_awaddr(s_awaddr), .s_axi_awlen(s_awlen),
        .s_axi_awsize(s_awsize), .s_axi_awburst(s_awburst), .s_axi_awlock(s_awlock),
        .s_axi_awcache(s_awcache), .s_axi_awprot(s_awprot), .s_axi_awqos(s_awqos),
        .s_axi_awregion(s_awregion), .s_axi_awuser(s_awuser), .s_axi_awvalid(s_awvalid),
        .s_axi_awready(s_awready), .s_axi_wdata(s_wdata), .s_axi_wstrb(s_wstrb),
        .s_axi_wlast(s_wlast), .s_axi_wuser(s_wuser), .s_axi_wvalid(s_wvalid),
        .s_axi_wready(s_wready), .s_axi_bid(s_bid), .s_axi_bresp(s_bresp),
        .s_axi_buser(s_buser), .s_axi_bvalid(s_bvalid), .s_axi_bready(s_bready),
        .s_axi_arid(s_arid), .s_axi_araddr(s_araddr), .s_axi_arlen(s_arlen),
        .s_axi_arsize(s_arsize), .s_axi_arburst(s_arburst), .s_axi_arlock(s_arlock),
        .s_axi_arcache(s_arcache), .s_axi_arprot(s_arprot), .s_axi_arqos(s_arqos),
        .s_axi_arregion(s_arregion), .s_axi_aruser(s_aruser), .s_axi_arvalid(s_arvalid),
        .s_axi_arready(s_arready), .s_axi_rid(s_rid), .s_axi_rdata(s_rdata),
        .s_axi_rresp(s_rresp), .s_axi_rlast(s_rlast), .s_axi_ruser(s_ruser),
        .s_axi_rvalid(s_rvalid), .s_axi_rready(s_rready),
        .m_axi_awid(m_awid), .m_axi_awaddr(m_awaddr), .m_axi_awlen(m_awlen),
        .m_axi_awsize(m_awsize), .m_axi_awburst(m_awburst), .m_axi_awlock(m_awlock),
        .m_axi_awcache(m_awcache), .m_axi_awprot(m_awprot), .m_axi_awqos(m_awqos),
        .m_axi_awregion(m_awregion), .m_axi_awuser(m_awuser), .m_axi_awvalid(m_awvalid),
        .m_axi_awready(m_awready), .m_axi_wdata(m_wdata), .m_axi_wstrb(m_wstrb),
        .m_axi_wlast(m_wlast), .m_axi_wuser(m_wuser), .m_axi_wvalid(m_wvalid),
        .m_axi_wready(m_wready), .m_axi_bid(m_bid), .m_axi_bresp(m_bresp),
        .m_axi_buser(m_buser), .m_axi_bvalid(m_bvalid), .m_axi_bready(m_bready),
        .m_axi_arid(m_arid), .m_axi_araddr(m_araddr), .m_axi_arlen(m_arlen),
        .m_axi_arsize(m_arsize), .m_axi_arburst(m_arburst), .m_axi_arlock(m_arlock),
        .m_axi_arcache(m_arcache), .m_axi_arprot(m_arprot), .m_axi_arqos(m_arqos),
        .m_axi_arregion(m_arregion), .m_axi_aruser(m_aruser), .m_axi_arvalid(m_arvalid),
        .m_axi_arready(m_arready), .m_axi_rid(m_rid), .m_axi_rdata(m_rdata),
        .m_axi_rresp(m_rresp), .m_axi_rlast(m_rlast), .m_axi_ruser(m_ruser),
        .m_axi_rvalid(m_rvalid), .m_axi_rready(m_rready));

    // ---------------------------------------------------------------- bookkeeping
    integer errors = 0;
    longint cyc = 0, last_progress = 0;
    always @(posedge clk) cyc <= cyc + 1;

    task automatic fail(input string msg);
        errors++;
        $display("ERROR @%0t: %s", $time, msg);
        if (errors > 20) begin
            $display("too many errors");
            $finish;
        end
    endtask

    // back-pressure profile (percent)
    int p_mvalid = 100, p_sready = 100, p_mready = 100, p_wvalid = 100;
    int slv_delay_max = 0;                   // extra B/R latency, cycles
    bit hold_aw = 0, hold_ar = 0;            // stop presenting AW/AR

    // model state
    bit [8:0] tbl_m [0:16383];
    bit       enable_m = 0;
    int       limit_m = 63;
    function automatic bit [8:0] ent(bit v, bit r, bit w, int win);
        return {win[5:0], w, r, v};
    endfunction

    function automatic void decide(input bit [63:0] a, input bit wr, output int cause,
                                   output bit [63:0] taddr);
        bit [8:0] e = tbl_m[a[34:21]];
        int win = e[8:3];
        if (!enable_m)                 cause = 5;
        else if (!e[0])                cause = 1;
        else if (win >= limit_m)       cause = 4;
        else if (wr && !e[2])          cause = 3;
        else if (!wr && !e[1])         cause = 2;
        else                           cause = 0;
        taddr = 64'hE800_0000 + (64'(win) << 21) + 64'(a[20:0]);
    endfunction

    function automatic bit [DW-1:0] pat(input bit [63:0] a);
        bit [DW-1:0] d;
        for (int i = 0; i < DW / 32; i++) d[32 * i +: 32] = (a[31:0] + 4 * i) ^ 32'h5A5A_0000 ^ a[63:32];
        return d;
    endfunction

    typedef struct {
        bit [ID_W-1:0] id;
        bit [63:0] addr;
        bit [7:0] len;
        bit [2:0] size, prot;
        bit [1:0] burst;
        bit lock, user;
        bit [3:0] cache, qos, region;
    } ax_t;
    typedef struct {
        bit [DW-1:0] data;
        bit [NB-1:0] strb;
        bit last;
    } w_t;
    typedef struct {
        bit fault;
        bit [63:0] taddr;
        bit [7:0] len;
        bit [1:0] resp;
        int beat;
    } er_t;
    typedef struct {
        bit write;
        int cause;
        bit [63:0] addr;
        bit [ID_W-1:0] id;
        bit [7:0] len;
    } f_t;

    ax_t aw_q[$], ar_q[$];
    w_t  w_q[$], wpred_q[$];
    ax_t exp_m_aw[$], exp_m_ar[$];
    bit [1:0] exp_b[64][$];
    er_t exp_r[64][$];
    f_t  faults[$];                           // predicted, since the last clear
    bit [7:0] exp_mem[bit [63:0]];
    bit [7:0] slv_mem[bit [63:0]];
    longint n_aw_hs = 0, n_ar_hs = 0, n_maw = 0, n_mar = 0, n_b = 0, n_r = 0;
    longint n_wfault = 0, n_rfault = 0;

    function automatic int outstanding();
        int n = aw_q.size() + ar_q.size() + w_q.size() + exp_m_aw.size() + exp_m_ar.size();
        for (int i = 0; i < 64; i++) n += exp_b[i].size() + exp_r[i].size();
        return n;
    endfunction

    function automatic bit [1:0] fwd_resp(input bit [63:0] taddr);
        return (taddr[26:21] == POISON_WIN) ? DECERR : OKAY;
    endfunction

    function automatic void on_aw(input ax_t a);
        int cause;
        bit [63:0] taddr;
        ax_t m;
        decide(a.addr, 1'b1, cause, taddr);
        n_aw_hs++;
        if (cause != 0) begin
            exp_b[a.id].push_back(SLVERR);
            faults.push_back('{1'b1, cause, a.addr, a.id, a.len});
            n_wfault++;
            for (int k = 0; k <= a.len; k++) void'(wpred_q.pop_front());
        end else begin
            m = a;
            m.addr = taddr;
            exp_m_aw.push_back(m);
            exp_b[a.id].push_back(fwd_resp(taddr));
            for (int k = 0; k <= a.len; k++) begin
                w_t w = wpred_q.pop_front();
                bit [63:0] ba = {taddr[63:4], 4'h0} + 16 * k;
                for (int b = 0; b < NB; b++) if (w.strb[b]) exp_mem[ba + b] = w.data[8 * b +: 8];
            end
        end
    endfunction

    function automatic void on_ar(input ax_t a);
        int cause;
        bit [63:0] taddr;
        ax_t m;
        decide(a.addr, 1'b0, cause, taddr);
        n_ar_hs++;
        if (cause != 0) begin
            exp_r[a.id].push_back('{1'b1, 0, a.len, SLVERR, 0});
            faults.push_back('{1'b0, cause, a.addr, a.id, a.len});
            n_rfault++;
        end else begin
            m = a;
            m.addr = taddr;
            exp_m_ar.push_back(m);
            exp_r[a.id].push_back('{1'b0, taddr, a.len, fwd_resp(taddr), 0});
        end
    endfunction

    // ---------------------------------------------------------------- upstream master
    ax_t aw_cur, ar_cur;
    function automatic bit roll(int p);
        return $urandom_range(99) < p;
    endfunction

    always @(posedge clk) begin
        if (!aresetn) begin
            s_awvalid <= 1'b0;
        end else begin
            automatic bit free = !s_awvalid;
            if (s_awvalid && s_awready) begin
                on_aw(aw_cur);
                free = 1'b1;
                s_awvalid <= 1'b0;
            end
            if (free && aw_q.size() != 0 && !hold_aw && roll(p_mvalid)) begin
                aw_cur = aw_q.pop_front();
                s_awvalid  <= 1'b1;
                s_awid     <= aw_cur.id;     s_awaddr  <= aw_cur.addr;  s_awlen   <= aw_cur.len;
                s_awsize   <= aw_cur.size;   s_awburst <= aw_cur.burst; s_awlock  <= aw_cur.lock;
                s_awcache  <= aw_cur.cache;  s_awprot  <= aw_cur.prot;  s_awqos   <= aw_cur.qos;
                s_awregion <= aw_cur.region; s_awuser  <= aw_cur.user;
            end
        end
    end

    always @(posedge clk) begin
        if (!aresetn) begin
            s_arvalid <= 1'b0;
        end else begin
            automatic bit free = !s_arvalid;
            if (s_arvalid && s_arready) begin
                on_ar(ar_cur);
                free = 1'b1;
                s_arvalid <= 1'b0;
            end
            if (free && ar_q.size() != 0 && !hold_ar && roll(p_mvalid)) begin
                ar_cur = ar_q.pop_front();
                s_arvalid  <= 1'b1;
                s_arid     <= ar_cur.id;     s_araddr  <= ar_cur.addr;  s_arlen   <= ar_cur.len;
                s_arsize   <= ar_cur.size;   s_arburst <= ar_cur.burst; s_arlock  <= ar_cur.lock;
                s_arcache  <= ar_cur.cache;  s_arprot  <= ar_cur.prot;  s_arqos   <= ar_cur.qos;
                s_arregion <= ar_cur.region; s_aruser  <= ar_cur.user;
            end
        end
    end

    always @(posedge clk) begin
        if (!aresetn) begin
            s_wvalid <= 1'b0;
        end else begin
            automatic bit free = !s_wvalid;
            if (s_wvalid && s_wready) begin
                free = 1'b1;
                s_wvalid <= 1'b0;
            end
            if (free && w_q.size() != 0 && roll(p_wvalid)) begin
                automatic w_t w = w_q.pop_front();
                s_wvalid <= 1'b1;
                s_wdata  <= w.data;
                s_wstrb  <= w.strb;
                s_wlast  <= w.last;
                s_wuser  <= w.data[0];
            end
        end
    end

    // responses
    always @(posedge clk) begin
        if (!aresetn) begin
            s_bready <= 1'b0;
            s_rready <= 1'b0;
        end else begin
            s_bready <= roll(p_mready);
            s_rready <= roll(p_mready);
            if (s_bvalid && s_bready) begin
                n_b++;
                last_progress = cyc;
                if (exp_b[s_bid].size() == 0) fail($sformatf("unexpected B id %0d resp %0d", s_bid, s_bresp));
                else begin
                    automatic bit [1:0] e = exp_b[s_bid].pop_front();
                    if (s_bresp !== e) fail($sformatf("B id %0d resp %0d, expected %0d", s_bid, s_bresp, e));
                end
                if (s_buser !== 1'b0 && s_bresp == SLVERR) fail("fault B with BUSER set");
            end
            if (s_rvalid && s_rready) begin
                n_r++;
                last_progress = cyc;
                if (exp_r[s_rid].size() == 0) fail($sformatf("unexpected R id %0d", s_rid));
                else begin
                    automatic er_t e = exp_r[s_rid][0];
                    automatic bit [DW-1:0] d = e.fault ? '0 : pat({e.taddr[63:4], 4'h0} + 16 * e.beat);
                    if (s_rresp !== e.resp)
                        fail($sformatf("R id %0d beat %0d resp %0d, expected %0d", s_rid, e.beat, s_rresp, e.resp));
                    if (s_rdata !== d)
                        fail($sformatf("R id %0d beat %0d data %h, expected %h", s_rid, e.beat, s_rdata, d));
                    if (s_rlast !== (e.beat == e.len))
                        fail($sformatf("R id %0d beat %0d of %0d rlast %0d", s_rid, e.beat, e.len, s_rlast));
                    if (e.beat == e.len) void'(exp_r[s_rid].pop_front());
                    else exp_r[s_rid][0].beat = e.beat + 1;
                end
            end
        end
    end

    // ---------------------------------------------------------------- downstream slave
    typedef struct {
        bit [ID_W-1:0] id;
        bit [63:0] addr;
        bit [7:0] len;
        int beat;
        longint t;
    } sp_t;
    sp_t slv_aw[$], slv_b[$], slv_r[$];
    w_t  slv_w[$];
    int  b_cur = -1, r_cur = -1;

    function automatic bit same_ax(ax_t e, bit [ID_W-1:0] id, bit [63:0] addr, bit [7:0] len,
                                   bit [2:0] size, bit [1:0] burst, bit lock, bit [3:0] cache,
                                   bit [2:0] prot, bit [3:0] qos, bit [3:0] region, bit user);
        return e.id == id && e.addr == addr && e.len == len && e.size == size && e.burst == burst &&
               e.lock == lock && e.cache == cache && e.prot == prot && e.qos == qos &&
               e.region == region && e.user == user;
    endfunction

    // earliest entry of its ID, and its delay has passed
    function automatic bit eligible(ref sp_t q[$], input int i);
        if (q[i].t > cyc) return 0;
        for (int j = 0; j < i; j++) if (q[j].id == q[i].id) return 0;
        return 1;
    endfunction

    always @(posedge clk) begin
        if (!aresetn) begin
            m_awready <= 1'b0;
            m_arready <= 1'b0;
            m_wready  <= 1'b0;
            m_bvalid  <= 1'b0;
            m_rvalid  <= 1'b0;
            b_cur = -1;
            r_cur = -1;
        end else begin
            m_awready <= roll(p_sready);
            m_arready <= roll(p_sready);
            m_wready  <= roll(p_sready);

            if (m_awvalid && m_awready) begin
                n_maw++;
                if (m_awaddr < 64'hE800_0000 || m_awaddr >= 64'hF000_0000)
                    fail($sformatf("AW downstream outside region 0: %h", m_awaddr));
                if (exp_m_aw.size() == 0) fail($sformatf("unexpected AW downstream %h (fault leaked?)", m_awaddr));
                else begin
                    automatic ax_t e = exp_m_aw.pop_front();
                    if (!same_ax(e, m_awid, m_awaddr, m_awlen, m_awsize, m_awburst, m_awlock,
                                 m_awcache, m_awprot, m_awqos, m_awregion, m_awuser))
                        fail($sformatf("AW downstream id %0d addr %h len %0d, expected id %0d addr %h len %0d",
                                       m_awid, m_awaddr, m_awlen, e.id, e.addr, e.len));
                end
                slv_aw.push_back('{m_awid, m_awaddr, m_awlen, 0, 0});
            end
            if (m_wvalid && m_wready) slv_w.push_back('{m_wdata, m_wstrb, m_wlast});
            // apply W beats to the bursts they belong to (W may lead AW)
            while (slv_aw.size() != 0 && slv_w.size() != 0) begin
                automatic w_t w = slv_w.pop_front();
                automatic bit [63:0] ba = {slv_aw[0].addr[63:4], 4'h0} + 16 * slv_aw[0].beat;
                for (int b = 0; b < NB; b++) if (w.strb[b]) slv_mem[ba + b] = w.data[8 * b +: 8];
                if (w.last !== (slv_aw[0].beat == slv_aw[0].len))
                    fail($sformatf("WLAST %0d at beat %0d of len %0d", w.last, slv_aw[0].beat, slv_aw[0].len));
                if (slv_aw[0].beat == slv_aw[0].len) begin
                    automatic sp_t s = slv_aw.pop_front();
                    s.t = cyc + $urandom_range(slv_delay_max);
                    slv_b.push_back(s);
                end else slv_aw[0].beat++;
            end

            if (m_arvalid && m_arready) begin
                n_mar++;
                if (m_araddr < 64'hE800_0000 || m_araddr >= 64'hF000_0000)
                    fail($sformatf("AR downstream outside region 0: %h", m_araddr));
                if (exp_m_ar.size() == 0) fail($sformatf("unexpected AR downstream %h (fault leaked?)", m_araddr));
                else begin
                    automatic ax_t e = exp_m_ar.pop_front();
                    if (!same_ax(e, m_arid, m_araddr, m_arlen, m_arsize, m_arburst, m_arlock,
                                 m_arcache, m_arprot, m_arqos, m_arregion, m_aruser))
                        fail($sformatf("AR downstream id %0d addr %h len %0d, expected id %0d addr %h len %0d",
                                       m_arid, m_araddr, m_arlen, e.id, e.addr, e.len));
                end
                slv_r.push_back('{m_arid, m_araddr, m_arlen, 0, cyc + $urandom_range(slv_delay_max)});
            end

            // B: out of order across IDs
            if (m_bvalid && m_bready) begin
                slv_b.delete(b_cur);
                b_cur = -1;
                m_bvalid <= 1'b0;
            end
            if (b_cur < 0 && slv_b.size() != 0 && roll(p_sready)) begin
                automatic int c[$];
                for (int i = 0; i < slv_b.size(); i++) if (eligible(slv_b, i)) c.push_back(i);
                if (c.size() != 0) begin
                    b_cur = c[$urandom_range(c.size() - 1)];
                    m_bvalid <= 1'b1;
                    m_bid    <= slv_b[b_cur].id;
                    m_bresp  <= fwd_resp(slv_b[b_cur].addr);
                    m_buser  <= slv_b[b_cur].addr[21];
                end
            end

            // R: beats interleaved across IDs
            if (m_rvalid && m_rready) begin
                if (slv_r[r_cur].beat == slv_r[r_cur].len) slv_r.delete(r_cur);
                else slv_r[r_cur].beat++;
                r_cur = -1;
                m_rvalid <= 1'b0;
            end
            if (r_cur < 0 && slv_r.size() != 0 && roll(p_sready)) begin
                automatic int c[$];
                for (int i = 0; i < slv_r.size(); i++) if (eligible(slv_r, i)) c.push_back(i);
                if (c.size() != 0) begin
                    r_cur = c[$urandom_range(c.size() - 1)];
                    m_rvalid <= 1'b1;
                    m_rid    <= slv_r[r_cur].id;
                    m_rdata  <= pat({slv_r[r_cur].addr[63:4], 4'h0} + 16 * slv_r[r_cur].beat);
                    m_rresp  <= fwd_resp(slv_r[r_cur].addr);
                    m_rlast  <= slv_r[r_cur].beat == slv_r[r_cur].len;
                    m_ruser  <= 1'b0;
                end
            end
        end
    end

    // ---------------------------------------------------------------- AXI stability (DUT outputs)
    reg        p_maw = 0, p_mar = 0, p_mw = 0, p_sb = 0, p_sr = 0;
    reg [127:0] k_maw, k_mar, k_sb;
    reg [DW+NB+1:0] k_mw;
    reg [DW+ID_W+3:0] k_sr;
    wire [127:0] c_maw = {m_awid, m_awaddr, m_awlen, m_awsize, m_awburst, m_awlock, m_awcache,
                          m_awprot, m_awqos, m_awregion, m_awuser};
    wire [127:0] c_mar = {m_arid, m_araddr, m_arlen, m_arsize, m_arburst, m_arlock, m_arcache,
                          m_arprot, m_arqos, m_arregion, m_aruser};
    wire [127:0] c_sb  = {s_bid, s_bresp, s_buser};
    wire [DW+NB+1:0] c_mw = {m_wdata, m_wstrb, m_wlast, m_wuser};
    wire [DW+ID_W+3:0] c_sr = {s_rid, s_rdata, s_rresp, s_rlast, s_ruser};
    always @(posedge clk) begin
        if (aresetn) begin
            if (p_maw && !m_awvalid) fail("m_axi_awvalid dropped before awready");
            if (p_maw && m_awvalid && c_maw !== k_maw) fail("m_axi AW payload changed while waiting");
            if (p_mar && !m_arvalid) fail("m_axi_arvalid dropped before arready");
            if (p_mar && m_arvalid && c_mar !== k_mar) fail("m_axi AR payload changed while waiting");
            if (p_mw && !m_wvalid) fail("m_axi_wvalid dropped before wready");
            if (p_mw && m_wvalid && c_mw !== k_mw) fail("m_axi W payload changed while waiting");
            if (p_sb && !s_bvalid) fail("s_axi_bvalid dropped before bready");
            if (p_sb && s_bvalid && c_sb !== k_sb) fail("s_axi B payload changed while waiting");
            if (p_sr && !s_rvalid) fail("s_axi_rvalid dropped before rready");
            if (p_sr && s_rvalid && c_sr !== k_sr) fail("s_axi R payload changed while waiting");
        end
        p_maw <= aresetn && m_awvalid && !m_awready;  k_maw <= c_maw;
        p_mar <= aresetn && m_arvalid && !m_arready;  k_mar <= c_mar;
        p_mw  <= aresetn && m_wvalid && !m_wready;    k_mw  <= c_mw;
        p_sb  <= aresetn && s_bvalid && !s_bready;    k_sb  <= c_sb;
        p_sr  <= aresetn && s_rvalid && !s_rready;    k_sr  <= c_sr;
    end

    // ---------------------------------------------------------------- AXI-Lite
    task automatic lwr(input bit [17:0] a, input bit [31:0] d);
        l_awaddr <= a; l_wdata <= d; l_awvalid <= 1'b1; l_wvalid <= 1'b1;
        do @(posedge clk); while (!(l_awready && l_wready));
        l_awvalid <= 1'b0; l_wvalid <= 1'b0; l_bready <= 1'b1;
        do @(posedge clk); while (!l_bvalid);
        l_bready <= 1'b0;
        if (l_bresp != OKAY) fail("AXI-Lite BRESP");
        if (a[17:16] == 2'b01) tbl_m[a[15:2]] = {d[13:8], d[2:0]};
        if (a == 18'h10) enable_m = d[0];
        if (a == 18'h18) limit_m = (d[6:0] > 64) ? 64 : d[6:0];
        if (a == 18'h24 && d[0]) faults.delete();
    endtask

    task automatic lrd(input bit [17:0] a, output bit [31:0] d);
        l_araddr <= a; l_arvalid <= 1'b1;
        do @(posedge clk); while (!l_arready);
        l_arvalid <= 1'b0; l_rready <= 1'b1;
        do @(posedge clk); while (!l_rvalid);
        d = l_rdata;
        l_rready <= 1'b0;
        if (l_rresp != OKAY) fail("AXI-Lite RRESP");
    endtask

    task automatic lexp(input bit [17:0] a, input bit [31:0] e, input bit [31:0] mask = 32'hFFFF_FFFF);
        bit [31:0] d;
        lrd(a, d);
        if ((d & mask) !== (e & mask)) fail($sformatf("reg 0x%05h = 0x%08h, expected 0x%08h (mask %08h)", a, d, e, mask));
    endtask

    task automatic set_entry(input int vpn, input bit v, input bit r, input bit w, input int win);
        lwr(18'h1_0000 + 4 * vpn, {18'd0, 6'(win), 5'd0, w, r, v});
    endtask

    // ---------------------------------------------------------------- stimulus helpers
    function automatic bit [63:0] va(input int vpn, input int off);
        return VA_BASE + (64'(vpn) << 21) + 64'(off);
    endfunction

    task automatic push_wr(input int id, input bit [63:0] a, input int len);
        ax_t x;
        x.id = id; x.addr = a; x.len = len; x.size = 3'd4; x.burst = 2'b01;
        x.lock = $urandom_range(1); x.cache = $urandom; x.prot = $urandom; x.qos = $urandom;
        x.region = $urandom; x.user = $urandom_range(1);
        for (int k = 0; k <= len; k++) begin
            w_t w;
            w.data = {$urandom, $urandom, $urandom, $urandom};
            w.strb = (k == 0 && $urandom_range(3) == 0) ? 16'($urandom) : '1;
            w.last = (k == len);
            w_q.push_back(w);
            wpred_q.push_back(w);
        end
        aw_q.push_back(x);
    endtask

    task automatic push_rd(input int id, input bit [63:0] a, input int len);
        ax_t x;
        x.id = id; x.addr = a; x.len = len; x.size = 3'd4; x.burst = 2'b01;
        x.lock = $urandom_range(1); x.cache = $urandom; x.prot = $urandom; x.qos = $urandom;
        x.region = $urandom; x.user = $urandom_range(1);
        ar_q.push_back(x);
    endtask

    // random offset in a page for a burst of len+1 beats that stays in 4 KiB
    function automatic int rand_off(input int len);
        int bytes = (len + 1) * 16;
        int blk = $urandom_range(511);
        int in4k = (bytes >= 4096) ? 0 : 16 * $urandom_range((4096 - bytes) / 16);
        return blk * 4096 + in4k;
    endfunction

    task automatic drain(input string what, input longint limit = 400000);
        longint t0 = cyc;
        last_progress = cyc;
        while (outstanding() != 0) begin
            @(posedge clk);
            if (cyc - last_progress > limit) begin
                fail($sformatf("%s: no progress for %0d cycles; aw_q %0d w_q %0d ar_q %0d exp_m_aw %0d exp_m_ar %0d",
                               what, limit, aw_q.size(), w_q.size(), ar_q.size(), exp_m_aw.size(), exp_m_ar.size()));
                $finish;
            end
        end
        repeat (20) @(posedge clk);
    endtask

    task automatic check_mem(input string what);
        int n = 0;
        foreach (exp_mem[a]) begin
            if (!slv_mem.exists(a) || slv_mem[a] !== exp_mem[a]) begin
                if (n < 5) fail($sformatf("%s: host byte %h = %h, expected %h", what, a,
                                          slv_mem.exists(a) ? slv_mem[a] : 8'hxx, exp_mem[a]));
                n++;
            end
        end
        foreach (slv_mem[a]) if (!exp_mem.exists(a)) begin
            if (n < 5) fail($sformatf("%s: host byte %h written, not expected", what, a));
            n++;
        end
        if (n != 0) fail($sformatf("%s: %0d host bytes differ", what, n));
    endtask

    task automatic profile(input int mv, input int wv, input int sr, input int mr, input int dly);
        p_mvalid = mv; p_wvalid = wv; p_sready = sr; p_mready = mr; slv_delay_max = dly;
    endtask

    task automatic do_reset(input int cycles = 10);
        aresetn <= 1'b0;
        repeat (cycles) @(posedge clk);
        aw_q.delete(); ar_q.delete(); w_q.delete(); wpred_q.delete();
        exp_m_aw.delete(); exp_m_ar.delete(); faults.delete();
        for (int i = 0; i < 64; i++) begin exp_b[i].delete(); exp_r[i].delete(); end
        slv_aw.delete(); slv_b.delete(); slv_r.delete(); slv_w.delete();
        exp_mem.delete(); slv_mem.delete();
        for (int i = 0; i < 16384; i++) tbl_m[i] = 0;
        enable_m = 0; limit_m = 63;
        hold_aw = 0; hold_ar = 0;
        aresetn <= 1'b1;
        @(posedge clk);
    endtask

    task automatic check_reset_state(input string what);
        bit [31:0] d;
        int n = 0;
        // AW/AR wait while the table is cleared
        if (s_awready || s_arready) fail({what, ": AW/AR ready during the table clear"});
        do begin
            @(posedge clk);
            n++;
        end while (dut.u_vmem.init_busy && n < 20000);
        if (n < 16000 || n >= 20000) fail($sformatf("%s: table clear took %0d cycles", what, n));
        lexp(18'h00, 32'h534C564D);
        lexp(18'h04, 32'h0001_0000);
        lexp(18'h08, 32'd7);
        lexp(18'h0C, 32'h4015_4000);
        lexp(18'h10, 32'h0);
        lexp(18'h14, 32'h0);
        lexp(18'h18, 32'd63);
        lexp(18'h1C, 32'h0);
        lexp(18'h20, 32'h0);
        lexp(18'h30, 32'h0);
        lexp(18'h34, 32'h0);
        lexp(18'h38, 32'h0);
        lexp(18'h3C, 32'h0);
        lexp(18'h40, 32'hE800_0000);
        lexp(18'h44, 32'h0);
        lexp(18'h48, 32'h0);
        lexp(18'h4C, 32'h208);
        lexp(18'h80, 32'h0);
        lexp(18'h2_0000, 32'h0);
        for (int i = 0; i < 40; i++) begin
            int v = (i < 8) ? (i < 4 ? i : 16380 + i - 4) : $urandom_range(16383);
            lexp(18'h1_0000 + 4 * v, 32'h0);
        end
    endtask

    // ---------------------------------------------------------------- phases
    task automatic ph_regs();
        bit [31:0] d;
        $display("[%0t] phase: registers and table bits", $time);
        lwr(18'h18, 32'd10); lexp(18'h18, 32'd10);
        lwr(18'h18, 32'd100); lexp(18'h18, 32'd64);
        lwr(18'h18, 32'd63); lexp(18'h18, 32'd63);
        lwr(18'h10, 32'h3); lexp(18'h10, 32'h3); lexp(18'h14, 32'h182, 32'h183);
        lwr(18'h10, 32'h0); lexp(18'h10, 32'h0);
        // every table bit, at walking-one VPNs
        for (int k = 0; k < 14; k++) begin
            int vpn = 1 << k;
            for (int b = 0; b < 9; b++) begin
                bit [8:0] e = 9'(1) << b;
                set_entry(vpn, e[0], e[1], e[2], e[8:3]);
                lexp(18'h1_0000 + 4 * vpn, {18'd0, e[8:3], 5'd0, e[2:0]});
            end
            set_entry(vpn, 1, 1, 1, 63);
            lexp(18'h1_0000 + 4 * vpn, 32'h3F07);
            // neighbours untouched
            lexp(18'h1_0000 + 4 * (vpn ^ 1), 32'h0);
            set_entry(vpn, 0, 0, 0, 0);
        end
        // reserved bits are ignored
        lwr(18'h1_0000 + 4 * 77, 32'hFFFF_FFFF);
        lexp(18'h1_0000 + 4 * 77, 32'h0000_3F07);
        set_entry(77, 0, 0, 0, 0);
    endtask

    task automatic ph_xlat();
        $display("[%0t] phase: translation of every address bit", $time);
        lwr(18'h10, 32'h1);
        lwr(18'h18, 32'd64);
        profile(100, 100, 100, 100, 0);
        // VPN bits: walking one (and 0, all ones), window bits walking
        for (int k = -1; k < 15; k++) begin
            int vpn = (k < 0) ? 0 : (k == 14 ? 16383 : (1 << k));
            int win = (k < 0) ? 0 : (k == 14 ? 63 : (1 << (k % 6)));
            set_entry(vpn, 1, 1, 1, win);
            // offset bits 0..20 walking (a single beat never crosses 4 KiB)
            for (int j = 0; j <= 21; j++) begin
                int off = (j == 21) ? 0 : (1 << j);
                bit [63:0] a = va(vpn, off);
                if (j % 3 == 0) a[63:35] = $urandom;        // upper bits are ignored
                push_wr($urandom_range(63), a, 0);
                push_rd($urandom_range(63), a, 0);
            end
            drain("xlat");
            set_entry(vpn, 0, 0, 0, 0);
        end
        check_mem("xlat");
        lwr(18'h18, 32'd63);
    endtask

    task automatic ph_lengths();
        $display("[%0t] phase: all burst lengths", $time);
        lwr(18'h10, 32'h1);
        set_entry(5, 1, 1, 1, 7);
        set_entry(9000, 1, 1, 1, 62);
        profile(80, 80, 70, 80, 8);
        for (int len = 0; len < 256; len++) begin
            int vpn = (len % 2) ? 5 : 9000;
            push_wr($urandom_range(7), va(vpn, rand_off(len)), len);
            push_rd($urandom_range(7), va(vpn, rand_off(len)), len);
        end
        drain("lengths");
        check_mem("lengths");
    endtask

    task automatic fault_case(input string name, input bit wr, input int cause,
                              input int vpn, input bit v, input bit r, input bit w, input int win,
                              input int limit, input bit en);
        bit [31:0] info;
        bit [63:0] fa;
        int id = 3;
        lwr(18'h10, 32'h1);
        lwr(18'h18, 32'd63);
        set_entry(100, 1, 1, 1, 4);
        set_entry(vpn, v, r, w, win);
        lwr(18'h24, 32'h3);
        // forwarded traffic on the same ID, in flight at a slow slave
        profile(100, 100, 15, 60, 60);
        for (int i = 0; i < 6; i++) begin
            if (wr) push_wr(id, va(100, rand_off(15)), 15);
            else    push_rd(id, va(100, rand_off(15)), 15);
        end
        while (aw_q.size() != 0 || ar_q.size() != 0 || s_awvalid || s_arvalid) @(posedge clk);
        // registers change only once the lookup pipeline is empty (the model
        // decides at the handshake, the DUT a few cycles later)
        while (dut.u_vmem.aw_busy || dut.u_vmem.ar_busy) @(posedge clk);
        hold_aw = 1; hold_ar = 1;
        if (limit != 63) lwr(18'h18, limit);
        if (!en) lwr(18'h10, 32'h0);
        fa = va(vpn, rand_off(7));
        if (wr) push_wr(id, fa, 7); else push_rd(id, fa, 7);
        // more on the same ID behind the fault (they fault too when disabled)
        for (int i = 0; i < 4; i++) begin
            if (wr) push_wr(id, va(100, rand_off(3)), 3);
            else    push_rd(id, va(100, rand_off(3)), 3);
        end
        // a second fault: the record keeps the first
        if (wr) push_wr(id + 1, va(16000, 0), 0); else push_rd(id + 1, va(16000, 0), 0);
        hold_aw = 0; hold_ar = 0;
        drain({"fault ", name});
        if (faults.size() < 2 || faults[0].cause != cause || faults[0].addr != fa)
            fail($sformatf("fault %s: the scenario did not fault as intended (%0d faults)", name, faults.size()));
        lexp(18'h34, 32'(faults.size()));
        lrd(18'h30, info);
        if (info !== {1'b1, wr, 3'd0, 3'(cause), 8'd7, 16'(id)})
            fail($sformatf("fault %s: FAULT_INFO 0x%08h, expected valid write=%0d cause %0d len 7 id %0d",
                           name, info, wr, cause, id));
        lexp(18'h28, fa[31:0]);
        lexp(18'h2C, fa[63:32]);
        lexp(18'h14, 32'h4, 32'h4);
        lwr(18'h24, 32'h1);
        lexp(18'h30, 32'h0);
        lexp(18'h34, 32'h0);
        lexp(18'h14, 32'h0, 32'h4);
        set_entry(vpn, 0, 0, 0, 0);
        lwr(18'h10, 32'h1);
        lwr(18'h18, 32'd63);
    endtask

    task automatic ph_faults();
        $display("[%0t] phase: faults behind traffic in flight", $time);
        for (int wr = 0; wr < 2; wr++) begin
            fault_case("invalid",   wr, 1, 200, 0, 1, 1, 3, 63, 1);
            if (wr) fault_case("no write", wr, 3, 202, 1, 1, 0, 3, 63, 1);
            else    fault_case("no read",  wr, 2, 201, 1, 0, 1, 3, 63, 1);
            fault_case("window 63", wr, 4, 203, 1, 1, 1, 63, 63, 1);
            fault_case("limit 8",   wr, 4, 204, 1, 1, 1, 9, 8, 1);
            fault_case("disabled",  wr, 5, 205, 1, 1, 1, 3, 63, 0);
        end
        check_mem("faults");
    endtask

    // random pages: some r/w, read-only, write-only, invalid, out of the window
    // limit, the slave's DECERR window
    int rpages[$];
    task automatic setup_random_pages();
        rpages.delete();
        for (int i = 0; i < 24; i++) begin
            int vpn = (i == 0) ? 0 : (i == 1 ? 16383 : $urandom_range(16383));
            int kind = i % 8;
            rpages.push_back(vpn);
            case (kind)
                0, 1, 2: set_entry(vpn, 1, 1, 1, $urandom_range(60));
                3:       set_entry(vpn, 1, 1, 0, $urandom_range(60));
                4:       set_entry(vpn, 1, 0, 1, $urandom_range(60));
                5:       set_entry(vpn, 0, 1, 1, $urandom_range(62));
                6:       set_entry(vpn, 1, 1, 1, 63);
                7:       set_entry(vpn, 1, 1, 1, POISON_WIN);
            endcase
        end
    endtask

    task automatic random_traffic(input int n, input int max_id);
        for (int i = 0; i < n; i++) begin
            int vpn = rpages[$urandom_range(rpages.size() - 1)];
            int len = ($urandom_range(3) == 0) ? $urandom_range(255) : $urandom_range(15);
            if ($urandom_range(1)) push_wr($urandom_range(max_id), va(vpn, rand_off(len)), len);
            else                   push_rd($urandom_range(max_id), va(vpn, rand_off(len)), len);
        end
    endtask

    task automatic ph_random();
        bit [31:0] d;
        longint f0;
        $display("[%0t] phase: random traffic", $time);
        lwr(18'h10, 32'h1);
        lwr(18'h18, 32'd63);
        lwr(18'h24, 32'h3);
        setup_random_pages();
        n_maw = 0; n_mar = 0;
        f0 = n_wfault + n_rfault;
        for (int p = 0; p < 6; p++) begin
            case (p)
                0: profile(100, 100, 100, 100, 0);
                1: profile(100, 100, 20, 100, 30);
                2: profile(100, 100, 100, 20, 0);
                3: profile(50, 50, 50, 50, 10);
                4: profile(100, 30, 60, 60, 5);
                5: profile(30, 100, 80, 80, 40);
            endcase
            // p 4/5: W leads or lags AW
            random_traffic(400, (p % 2) ? 3 : 63);
            drain($sformatf("random profile %0d", p));
            $display("[%0t]   profile %0d done: %0d AW / %0d AR forwarded, %0d write / %0d read faults so far",
                     $time, p, n_maw, n_mar, n_wfault, n_rfault);
        end
        check_mem("random");
        lexp(18'h38, 32'(n_maw));
        lexp(18'h3C, 32'(n_mar));
        lexp(18'h34, 32'(n_wfault + n_rfault - f0));
        lrd(18'h30, d);
        begin
            bit hit = 0;
            foreach (faults[i])
                if (d == {1'b1, faults[i].write, 3'd0, 3'(faults[i].cause), faults[i].len, 16'(faults[i].id)})
                    hit = 1;
            if (!hit || faults.size() == 0) fail($sformatf("random: FAULT_INFO 0x%08h matches no predicted fault", d));
        end
        lexp(18'h1C, 32'h0);
        lexp(18'h20, 32'h0);
    endtask

    task automatic ph_pause();
        bit [31:0] d;
        longint maw0, mar0, saw0, sar0;
        int vpn_a, vpn_b, n;
        $display("[%0t] phase: pause, drain and remap under traffic", $time);
        lwr(18'h10, 32'h1);
        setup_random_pages();
        vpn_a = rpages[0];
        vpn_b = rpages[1];
        set_entry(vpn_a, 1, 1, 1, 11);
        set_entry(vpn_b, 1, 1, 1, 12);
        profile(100, 100, 40, 70, 20);
        for (int round = 0; round < 4; round++) begin
            random_traffic(300, 7);
            for (int i = 0; i < 40; i++) begin
                push_wr($urandom_range(7), va(vpn_a, rand_off(7)), 7);
                push_rd($urandom_range(7), va(vpn_b, rand_off(7)), 7);
            end
            repeat ($urandom_range(300, 50)) @(posedge clk);
            lwr(18'h10, 32'h3);
            n = 0;
            do begin
                lrd(18'h14, d);
                n++;
            end while (!d[1] && n < 10000);
            if (!d[1]) fail("pause: never drained");
            lexp(18'h1C, 32'h0);
            lexp(18'h20, 32'h0);
            maw0 = n_maw; mar0 = n_mar; saw0 = n_aw_hs; sar0 = n_ar_hs;
            repeat (200) @(posedge clk);
            if (n_maw != maw0 || n_mar != mar0) fail("pause: traffic forwarded while drained");
            if (n_aw_hs != saw0 || n_ar_hs != sar0) fail("pause: AW/AR accepted while paused");
            // remap (window change, invalidation) while paused
            set_entry(vpn_a, 1, 1, 1, (round % 2) ? 11 : 21);
            set_entry(vpn_b, round % 2, 1, 1, 12);
            lwr(18'h10, 32'h1);
            drain($sformatf("pause round %0d", round));
        end
        check_mem("pause");
    endtask

    task automatic ph_reset_traffic();
        $display("[%0t] phase: reset during traffic", $time);
        lwr(18'h10, 32'h1);
        setup_random_pages();
        profile(100, 100, 30, 50, 20);
        random_traffic(300, 15);
        repeat ($urandom_range(2000, 500)) @(posedge clk);
        if (n_aw_hs == 0 && n_ar_hs == 0) fail("reset phase: no traffic before the reset");
        do_reset(13);
        check_reset_state("reset during traffic");
        lwr(18'h10, 32'h1);
        setup_random_pages();
        profile(80, 80, 80, 80, 4);
        random_traffic(300, 15);
        drain("after reset");
        check_mem("after reset");
    endtask

    initial begin
        int seed;
        if (!$value$plusargs("seed=%d", seed)) seed = 1;
        $display("tb_sl_vmem seed %0d", seed);
        seed = $urandom(seed);
        repeat (5) @(posedge clk);
        do_reset(10);
        $display("[%0t] phase: reset state and table clear", $time);
        check_reset_state("power-on");
        ph_regs();
        ph_xlat();
        ph_lengths();
        ph_faults();
        ph_random();
        ph_pause();
        ph_reset_traffic();
        $display("forwarded AW %0d AR %0d, B %0d, R beats %0d, faults write %0d read %0d",
                 n_maw, n_mar, n_b, n_r, n_wfault, n_rfault);
        if (errors == 0) $display("TB_SL_VMEM PASS");
        else             $display("TB_SL_VMEM FAIL (%0d errors)", errors);
        $finish;
    end

    initial begin
        #50ms;
        $display("TB_SL_VMEM FAIL (global timeout)");
        $finish;
    end
endmodule
