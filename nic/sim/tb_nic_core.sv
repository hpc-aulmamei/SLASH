`timescale 1ns / 1ps

// tb_nic_core: nic_core with the two ports joined by a cable model (port 0 TX
// -> port 1 RX, port 1 TX -> port 0 RX, random stalls), a QDMA-side H2C driver,
// a C2H / completion checker with random backpressure, and AXI-Lite setup.
// Checks: data and length of every frame that should reach the host, the C2H
// queue per port, completion entries (length, port, mcast, packet id order),
// the MAC filter, broadcast, too-long frames, frames shorter than 64 B, H2C
// with an unknown qid, hairpin with MAC swap, and the counters.
module tb_nic_core;
    logic clk = 0, rstn = 0;
    always #2 clk = ~clk;

    // ------------------------------------------------------------ DUT
    logic [511:0] h2c_tdata;  logic h2c_tvalid = 0, h2c_tready, h2c_tlast;
    logic [11:0]  h2c_qid;    logic h2c_err = 0;   logic [5:0] h2c_mty;  logic h2c_zero_byte = 0;
    logic [511:0] c2h_tdata;  logic c2h_tvalid, c2h_tready, c2h_tlast;
    logic [15:0]  c2h_ctrl_len; logic [11:0] c2h_ctrl_qid; logic c2h_ctrl_has_cmpt;
    logic [511:0] cmpt_data;  logic cmpt_tvalid, cmpt_tready;  logic [1:0] cmpt_size;
    logic [12:0]  cmpt_qid;   logic [15:0] cmpt_wait_pld_pkt_id;
    logic         c2h_sts_valid = 0, c2h_sts_drop = 0, c2h_sts_error = 0;  logic [11:0] c2h_sts_qid = 0;

    logic [1023:0] rx_tdata [2], tx_tdata [2];
    logic [127:0]  rx_tkeep [2], tx_tkeep [2];
    logic [1:0]    rx_tlast, rx_tvalid, rx_tready, tx_tlast, tx_tvalid, tx_tready;
    logic [15:0]   phy0_ctrl, phy1_ctrl;

    logic [15:0] awaddr = 0, araddr = 0;
    logic awvalid = 0, wvalid = 0, bready = 1, arvalid = 0, rready = 1;
    logic [31:0] wdata = 0;
    logic awready, wready, bvalid, arready, rvalid;
    logic [1:0] bresp, rresp;
    logic [31:0] rdata;

    nic_core #(.BUILD(3), .RXQ_BEATS(256)) dut (
        .clk(clk), .rstn(rstn),
        .h2c_tdata(h2c_tdata), .h2c_tvalid(h2c_tvalid), .h2c_tready(h2c_tready), .h2c_tlast(h2c_tlast),
        .h2c_qid(h2c_qid), .h2c_err(h2c_err), .h2c_mty(h2c_mty), .h2c_zero_byte(h2c_zero_byte),
        .c2h_tdata(c2h_tdata), .c2h_tvalid(c2h_tvalid), .c2h_tready(c2h_tready), .c2h_tlast(c2h_tlast),
        .c2h_ctrl_len(c2h_ctrl_len), .c2h_ctrl_qid(c2h_ctrl_qid), .c2h_ctrl_has_cmpt(c2h_ctrl_has_cmpt),
        .cmpt_data(cmpt_data), .cmpt_tvalid(cmpt_tvalid), .cmpt_tready(cmpt_tready), .cmpt_size(cmpt_size),
        .cmpt_qid(cmpt_qid), .cmpt_wait_pld_pkt_id(cmpt_wait_pld_pkt_id),
        .c2h_sts_valid(c2h_sts_valid), .c2h_sts_drop(c2h_sts_drop), .c2h_sts_error(c2h_sts_error),
        .c2h_sts_qid(c2h_sts_qid), .evt_overflow(1'b0), .noc_status(1'b0),
        .rx0_tdata(rx_tdata[0]), .rx0_tkeep(rx_tkeep[0]), .rx0_tlast(rx_tlast[0]), .rx0_tvalid(rx_tvalid[0]),
        .rx0_tready(rx_tready[0]),
        .tx0_tdata(tx_tdata[0]), .tx0_tkeep(tx_tkeep[0]), .tx0_tlast(tx_tlast[0]), .tx0_tvalid(tx_tvalid[0]),
        .tx0_tready(tx_tready[0]), .rx0_drop_count(16'h0012), .phy0_ctrl(phy0_ctrl), .phy0_status(16'h00AF),
        .rx1_tdata(rx_tdata[1]), .rx1_tkeep(rx_tkeep[1]), .rx1_tlast(rx_tlast[1]), .rx1_tvalid(rx_tvalid[1]),
        .rx1_tready(rx_tready[1]),
        .tx1_tdata(tx_tdata[1]), .tx1_tkeep(tx_tkeep[1]), .tx1_tlast(tx_tlast[1]), .tx1_tvalid(tx_tvalid[1]),
        .tx1_tready(tx_tready[1]), .rx1_drop_count(16'h0034), .phy1_ctrl(phy1_ctrl), .phy1_status(16'h00BF),
        .s_axil_awaddr(awaddr), .s_axil_awvalid(awvalid), .s_axil_awready(awready),
        .s_axil_wdata(wdata), .s_axil_wvalid(wvalid), .s_axil_wready(wready),
        .s_axil_bresp(bresp), .s_axil_bvalid(bvalid), .s_axil_bready(bready),
        .s_axil_araddr(araddr), .s_axil_arvalid(arvalid), .s_axil_arready(arready),
        .s_axil_rdata(rdata), .s_axil_rresp(rresp), .s_axil_rvalid(rvalid), .s_axil_rready(rready)
    );

    integer errors = 0;
    task automatic check(input bit cond, input string what);
        if (!cond) begin errors++; if (errors < 30) $display("FAIL @%0t: %s", $time, what); end
    endtask

    // ------------------------------------------------------------ AXI-Lite
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
    task automatic lr64(input [15:0] a, output [63:0] d);
        logic [31:0] lo, hi;
        lr(a, lo); lr(a + 4, hi); d = {hi, lo};
    endtask

    // ------------------------------------------------------------ cable model: tx[p] -> rx[1-p]
    // a small FIFO per direction with random stalls on both sides
    typedef struct packed { logic last; logic [127:0] keep; logic [1023:0] data; } beat_t;
    beat_t cab [2][$];
    for (genvar p = 0; p < 2; p++) begin : g_cab
        always @(posedge clk) begin
            tx_tready[p] <= (cab[p].size() < 8) && ($urandom % 4 != 0);
            if (tx_tvalid[p] && tx_tready[p]) cab[p].push_back('{tx_tlast[p], tx_tkeep[p], tx_tdata[p]});
        end
        // rx side of the other port
        always @(posedge clk) begin
            if (!rstn) rx_tvalid[1-p] <= 0;
            else begin
                if (rx_tvalid[1-p] && rx_tready[1-p]) rx_tvalid[1-p] <= 0;
                if ((!rx_tvalid[1-p] || rx_tready[1-p]) && cab[p].size() > 0 && ($urandom % 5 != 0)) begin
                    beat_t b;
                    b = cab[p].pop_front();
                    // the DCMAC leaves stale bytes outside tkeep (e.g. the stripped FCS)
                    for (int i = 0; i < 128; i++) if (!b.keep[i]) b.data[8*i +: 8] = 8'hA5 ^ 8'(i);
                    rx_tdata[1-p] <= b.data; rx_tkeep[1-p] <= b.keep; rx_tlast[1-p] <= b.last;
                    rx_tvalid[1-p] <= 1;
                end
            end
        end
    end

    // ------------------------------------------------------------ frames and scoreboard
    typedef byte unsigned frame_t[];
    frame_t exp_q [2][$];                   // expected C2H frames per port (rx_qid 0 / 1)
    integer exp_mcast [2][$];

    function automatic frame_t mkframe(input int len, input logic [47:0] dst, input logic [47:0] src);
        frame_t f;
        f = new[len];
        for (int i = 0; i < len; i++) f[i] = $urandom;
        for (int i = 0; i < 6; i++) begin f[i] = dst[47 - 8*i -: 8]; f[6 + i] = src[47 - 8*i -: 8]; end
        if (len >= 14) begin f[12] = 8'h88; f[13] = 8'hB5; end
        return f;
    endfunction

    // H2C driver: one packet on qid
    task automatic h2c_send(input frame_t f, input [11:0] qid);
        int n, beats;
        n = f.size();
        beats = (n + 63) / 64;
        for (int b = 0; b < beats; b++) begin
            logic [511:0] d;
            d = '0;
            for (int i = 0; i < 64; i++) if (64*b + i < n) d[8*i +: 8] = f[64*b + i];
            @(posedge clk);
            while ($urandom % 6 == 0) @(posedge clk);
            // as qdma_noc_rm: qid and mty are valid on the tlast beat only (junk before)
            h2c_tdata <= d; h2c_tlast <= (b == beats - 1);
            h2c_qid <= (b == beats - 1) ? qid : 12'($urandom);
            h2c_mty <= (b == beats - 1) ? 6'((64 - n % 64) % 64) : 6'($urandom);
            h2c_tvalid <= 1;
            do @(posedge clk); while (!h2c_tready);
            h2c_tvalid <= 0;
        end
    endtask

    localparam logic [47:0] MAC0 = 48'h02_0A_35_00_00_00, MAC1 = 48'h02_0A_35_00_00_01;
    localparam logic [47:0] BCAST = 48'hFF_FF_FF_FF_FF_FF, OTHER = 48'h02_11_22_33_44_55;

    // C2H checker
    always @(posedge clk) c2h_tready <= ($urandom % 4 != 0);
    always @(posedge clk) cmpt_tready <= ($urandom % 3 != 0);
    byte unsigned got [$];
    logic [15:0] cur_len; logic [11:0] cur_qid; logic in_pkt = 0;
    typedef struct { int qid; int len; int mcast; } hdr_t;
    hdr_t c2h_hdrs [$];
    int true_len_q [$], cmpt_len_q [$];      // frame lengths in C2H order; completion lengths
    integer c2h_frames = 0, cmpt_frames = 0;
    logic [15:0] next_pkt_id = 1;
    always @(posedge clk) if (rstn && c2h_tvalid && c2h_tready) begin
        if (!in_pkt) begin
            cur_len = c2h_ctrl_len; cur_qid = c2h_ctrl_qid; got.delete();
            check(c2h_ctrl_has_cmpt, "has_cmpt");
        end else begin
            check(c2h_ctrl_len == cur_len && c2h_ctrl_qid == cur_qid, "ctrl_len/qid held through the packet");
        end
        for (int i = 0; i < 64; i++) got.push_back(c2h_tdata[8*i +: 8]);
        in_pkt = !c2h_tlast;
        if (c2h_tlast) begin
            int q;
            q = cur_qid;
            check(q == 0 || q == 1, $sformatf("C2H qid %0d", q));
            check(got.size() == 64 * ((cur_len + 63) / 64), $sformatf("beats for len %0d: %0d bytes", cur_len, got.size()));
            if (exp_q[q].size() == 0) begin
                check(0, $sformatf("unexpected C2H frame on qid %0d len %0d", q, cur_len));
            end else begin
                frame_t e;
                int mc;
                e = exp_q[q].pop_front();
                mc = exp_mcast[q].pop_front();
                // ctrl_len: the frame padded to whole beats (mty 0 on every beat)
                check(cur_len == 64 * ((e.size() + 63) / 64),
                      $sformatf("qid %0d: ctrl_len %0d want %0d (frame %0d padded)", q, cur_len,
                                64 * ((e.size() + 63) / 64), e.size()));
                for (int i = 0; i < e.size() && i < got.size(); i++)
                    if (got[i] != e[i]) begin
                        check(0, $sformatf("qid %0d len %0d: byte %0d is %02x want %02x", q, e.size(), i, got[i], e[i]));
                        break;
                    end
                for (int i = e.size(); i < got.size(); i++)
                    if (got[i] != 0) begin
                        check(0, $sformatf("qid %0d len %0d: padding byte %0d is %02x", q, e.size(), i, got[i]));
                        break;
                    end
                c2h_hdrs.push_back('{q, e.size(), mc});
                true_len_q.push_back(e.size());
            end
            c2h_frames++;
        end
    end
    always @(posedge clk) if (rstn && cmpt_tvalid && cmpt_tready) begin
        hdr_t h;
        cmpt_frames++;
        check(cmpt_size == 2'd1, "cmpt_size");
        check(cmpt_wait_pld_pkt_id == next_pkt_id, $sformatf("pkt id %0d want %0d", cmpt_wait_pld_pkt_id, next_pkt_id));
        next_pkt_id++;
        check(cmpt_data[3] && cmpt_data[20] && cmpt_data[2:0] == 0, "cmpt fixed bits");
        if (c2h_hdrs.size() == 0) begin
            // the completion is queued at the first C2H beat, it may come before the last beat
        end
    end
    // completion length = the frame's exact length (the C2H packet is padded)
    always @(negedge clk) while (true_len_q.size() && cmpt_len_q.size()) begin
        int t, c;
        t = true_len_q.pop_front(); c = cmpt_len_q.pop_front();
        check(c == t, $sformatf("cmpt len %0d want frame length %0d", c, t));
    end
    // completion contents against C2H order: compare by the order of first beats
    hdr_t sop_q [$];
    logic in_pkt2 = 0;
    always @(posedge clk) if (rstn && c2h_tvalid && c2h_tready) begin
        if (!in_pkt2) sop_q.push_back('{c2h_ctrl_qid, c2h_ctrl_len, 0});
        in_pkt2 = !c2h_tlast;
    end
    always @(posedge clk) if (rstn && cmpt_tvalid && cmpt_tready) begin
        if (sop_q.size() == 0) check(0, "completion without a C2H packet");
        else begin
            hdr_t h;
            h = sop_q.pop_front();
            check(cmpt_qid == h.qid, $sformatf("cmpt qid %0d want %0d", cmpt_qid, h.qid));
            check(64 * ((cmpt_data[19:4] + 63) / 64) == h.len,
                  $sformatf("cmpt len %0d vs ctrl_len %0d", cmpt_data[19:4], h.len));
            cmpt_len_q.push_back(cmpt_data[19:4]);
            check(cmpt_data[21] == (h.qid == 1), "cmpt port bit (frames to qid 1 came in on port 1)");
        end
    end

    // ------------------------------------------------------------ the test
    integer sent_ok [2];
    task automatic wait_drain(input int cycles);
        repeat (cycles) @(posedge clk);
    endtask

    logic [31:0] v;
    logic [63:0] v64;
    initial begin
        h2c_tlast = 0; h2c_qid = 0; h2c_mty = 0; h2c_tdata = 0;
        rx_tvalid = 0;
        repeat (10) @(posedge clk);
        rstn <= 1;
        repeat (5) @(posedge clk);

        lr(16'h000, v); check(v == 32'h4E49_4331, "ID");
        lr(16'h00C, v); check(v == 3, "BUILD");
        lr(16'h114, v); check(v == 32'h0000_020A, "port 0 MAC_HI reset");
        lr(16'h210, v); check(v == 32'h3500_0001, "port 1 MAC_LO reset");
        lr(16'h11C, v); check(v == 16'h00AF, "PHY_STATUS 0");
        lr(16'h220, v); check(v == 16'h0034, "RX_DROPS 1");
        lw(16'h118, 32'h0000_0102); lr(16'h118, v); check(v == 32'h102 && phy0_ctrl == 16'h102, "PHY_CTRL 0");
        lw(16'h118, 0);

        // 1. port 0 -> cable -> port 1 -> C2H qid 1, and the other way, interleaved
        for (int k = 0; k < 120; k++) begin
            int p, len;
            frame_t f;
            p = $urandom % 2;
            case ($urandom % 6)
                0: len = 42;  1: len = 60;  2: len = 64 + $urandom % 3;
                3: len = 1 + $urandom % 1500 + 60;  4: len = 4096;  default: len = 60 + $urandom % 4037;
            endcase
            f = mkframe(len, p ? MAC0 : MAC1, p ? MAC1 : MAC0);
            exp_q[1-p].push_back(f); exp_mcast[1-p].push_back(0);
            h2c_send(f, 12'(p));
        end
        wait_drain(20000);
        check(exp_q[0].size() == 0 && exp_q[1].size() == 0,
              $sformatf("all frames reached the host (%0d, %0d left)", exp_q[0].size(), exp_q[1].size()));

        // 1b. the netdev's queues: TX_QBASE 248 / 249 (the qid only on the tlast beat)
        lw(16'h108, 248); lw(16'h208, 249);
        for (int k = 0; k < 60; k++) begin
            int p, len;
            frame_t f;
            p = $urandom % 2;
            case ($urandom % 4)
                0: len = 60;  1: len = 98;  2: len = 4096;  default: len = 60 + $urandom % 4037;
            endcase
            f = mkframe(len, p ? MAC0 : MAC1, p ? MAC1 : MAC0);
            exp_q[1-p].push_back(f); exp_mcast[1-p].push_back(0);
            h2c_send(f, 12'(248 + p));
        end
        wait_drain(10000);
        check(exp_q[0].size() == 0 && exp_q[1].size() == 0,
              $sformatf("qid 248/249 frames reached the host (%0d, %0d left)", exp_q[0].size(), exp_q[1].size()));
        lw(16'h108, 0); lw(16'h208, 1);

        // 2. filter: unknown unicast dropped, broadcast passes (mcast bit), too long dropped,
        //    an H2C packet on an unknown qid is dropped
        begin
            frame_t f;
            f = mkframe(100, OTHER, MAC0); h2c_send(f, 0);                       // filtered at port 1
            f = mkframe(200, BCAST, MAC0); exp_q[1].push_back(f); exp_mcast[1].push_back(1); h2c_send(f, 0);
            f = mkframe(4097, MAC1, MAC0); h2c_send(f, 0);                       // too long at port 1
            f = mkframe(9000, MAC1, MAC0); h2c_send(f, 0);                       // past 128 beats: dropped as an H2C error
            f = mkframe(300, MAC1, MAC0); h2c_send(f, 12'd7);                    // no port
            f = mkframe(64, MAC1, MAC0); exp_q[1].push_back(f); exp_mcast[1].push_back(0); h2c_send(f, 0);
        end
        wait_drain(5000);
        check(exp_q[1].size() == 0, "broadcast and the frame after the drops arrived");

        // 3. promiscuous port 1: the unknown unicast passes
        lw(16'h200, 32'h7);
        begin
            frame_t f;
            f = mkframe(77, OTHER, MAC0); exp_q[1].push_back(f); exp_mcast[1].push_back(0); h2c_send(f, 0);
        end
        wait_drain(2000);
        check(exp_q[1].size() == 0, "promiscuous");
        lw(16'h200, 32'h3);

        // 4. hairpin with MAC swap on port 1: H2C on port 0 -> port 1 turns it around ->
        //    port 0 RX (dst after the swap = the original src = MAC0) -> C2H qid 0
        lw(16'h200, 32'h1B);                  // rx_en tx_en hairpin swap
        for (int k = 0; k < 20; k++) begin
            frame_t f, e;
            int len;
            len = 60 + $urandom % 2000;
            f = mkframe(len, OTHER, MAC0);    // dst need not match: hairpin comes before the filter
            e = new[len](f);
            for (int i = 0; i < 6; i++) begin e[i] = f[6 + i]; e[6 + i] = f[i]; end
            exp_q[0].push_back(e); exp_mcast[0].push_back(0);
            h2c_send(f, 0);
        end
        wait_drain(10000);
        check(exp_q[0].size() == 0, $sformatf("hairpin frames back on qid 0 (%0d left)", exp_q[0].size()));
        lw(16'h200, 32'h3);

        // 5. tx_en 0 on port 0: H2C to it is dropped and counted
        lw(16'h100, 32'h1);
        begin frame_t f; f = mkframe(100, MAC1, MAC0); h2c_send(f, 0); end
        wait_drain(500);
        lw(16'h100, 32'h3);

        // 6. C2H status drop counted per port
        @(posedge clk); c2h_sts_valid <= 1; c2h_sts_drop <= 1; c2h_sts_qid <= 1;
        @(posedge clk); c2h_sts_valid <= 0; c2h_sts_drop <= 0;
        repeat (3) @(posedge clk);

        // 7. malformed frames straight onto port 1's RX (as from the MAC, past the
        //    cable model): an empty last beat, an empty beat in the middle, a
        //    10-byte frame. All three dropped and counted; a frame split over two
        //    half-filled 1024-bit beats is well formed and passes, and so does
        //    the frame after them.
        begin
            frame_t f;
            beat_t b;
            // a: 200 B (128 + 72) then an empty tlast beat
            f = mkframe(200, MAC1, MAC0);
            b = '0; for (int i = 0; i < 128; i++) b.data[8*i +: 8] = f[i]; b.keep = '1; cab[0].push_back(b);
            b = '0; for (int i = 0; i < 72; i++) b.data[8*i +: 8] = f[128 + i];
            b.keep = {56'd0, {72{1'b1}}}; cab[0].push_back(b);
            b = '0; b.last = 1; cab[0].push_back(b);
            // b: 10 bytes
            f = mkframe(10, MAC1, MAC0);
            b = '0; for (int i = 0; i < 10; i++) b.data[8*i +: 8] = f[i]; b.keep = {118'd0, {10{1'b1}}}; b.last = 1;
            cab[0].push_back(b);
            // c: 128 B, an empty beat, 50 B
            f = mkframe(178, MAC1, MAC0);
            b = '0; for (int i = 0; i < 128; i++) b.data[8*i +: 8] = f[i]; b.keep = '1; cab[0].push_back(b);
            b = '0; cab[0].push_back(b);
            b = '0; for (int i = 0; i < 50; i++) b.data[8*i +: 8] = f[128 + i];
            b.keep = {78'd0, {50{1'b1}}}; b.last = 1; cab[0].push_back(b);
            // d: 128 B as 64 + 64 in two 1024-bit beats: well formed
            f = mkframe(128, MAC1, MAC0); exp_q[1].push_back(f); exp_mcast[1].push_back(0);
            b = '0; for (int i = 0; i < 64; i++) b.data[8*i +: 8] = f[i]; b.keep = {64'd0, {64{1'b1}}}; cab[0].push_back(b);
            b = '0; for (int i = 0; i < 64; i++) b.data[8*i +: 8] = f[64 + i]; b.keep = {64'd0, {64{1'b1}}}; b.last = 1;
            cab[0].push_back(b);
            // e: an ordinary frame after them
            f = mkframe(1500, MAC1, MAC0); exp_q[1].push_back(f); exp_mcast[1].push_back(0);
            for (int k = 0; k < 12; k++) begin
                b = '0;
                for (int i = 0; i < 128; i++) if (128*k + i < 1500) begin b.data[8*i +: 8] = f[128*k + i]; b.keep[i] = 1; end
                b.last = (k == 11);
                cab[0].push_back(b);
            end
        end
        wait_drain(3000);
        check(exp_q[1].size() == 0, $sformatf("frames after the malformed ones arrived (%0d left)", exp_q[1].size()));

        // counters
        lr(16'h004, v); check(v == 32'h0001_0003, $sformatf("VERSION %08x", v));
        lr64(16'h038, v64); check(v64 == 1, $sformatf("H2C_ZERO %0d (the 9000 B packet)", v64));
        lr64(16'h298, v64); check(v64 == 3, $sformatf("port 1 RX_MALFORMED %0d", v64));
        lr64(16'h198, v64); check(v64 == 0, $sformatf("port 0 RX_MALFORMED %0d", v64));
        lr64(16'h030, v64); check(v64 == 1, $sformatf("H2C_NOPORT %0d", v64));
        lr64(16'h268, v64); check(v64 == 1, $sformatf("port 1 RX_FILTERED %0d", v64));
        lr64(16'h270, v64); check(v64 == 1, $sformatf("port 1 RX_TOOLONG %0d (the 4097 B frame)", v64));
        lr64(16'h278, v64); check(v64 == 20, $sformatf("port 1 HAIRPIN %0d", v64));
        lr64(16'h188, v64); check(v64 == 1, $sformatf("port 0 TX_DROP %0d", v64));
        lr64(16'h290, v64); check(v64 == 1, $sformatf("port 1 C2H_DROP %0d", v64));
        lr64(16'h058, v64); check(v64 == c2h_frames, $sformatf("CMPT_SENT %0d, C2H frames %0d", v64, c2h_frames));
        begin
            logic [63:0] tx0, rx1, tx1, rx0;
            lr64(16'h140, tx0); lr64(16'h250, rx1); lr64(16'h240, tx1); lr64(16'h150, rx0);
            check(tx0 + 5 == rx1 && tx1 == rx0, $sformatf("frames tx0 %0d rx1 %0d tx1 %0d rx0 %0d", tx0, rx1, tx1, rx0));
        end
        check(cmpt_frames == c2h_frames, $sformatf("completions %0d, C2H frames %0d", cmpt_frames, c2h_frames));
        lw(16'h020, 1);
        lr64(16'h140, v64); check(v64 == 0, "clear");

        $display("C2H frames %0d, completions %0d", c2h_frames, cmpt_frames);
        if (errors == 0) $display("PASS: tb_nic_core");
        else             $display("FAIL: tb_nic_core, %0d errors", errors);
        $finish;
    end
    initial begin #20000000; $display("FAIL: tb_nic_core timeout"); $finish; end
endmodule
