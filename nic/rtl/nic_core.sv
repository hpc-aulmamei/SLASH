`timescale 1ns / 1ps

// nic_core: the NIC datapath of the NIC RM, between qdma_noc_rm (the CPM5
// QDMA streams over the NoC) and two DCMAC ports (dcmac_rx_path /
// dcmac_tx_path, 1024-bit AXIS, frames without FCS). One clock (service_clk,
// 250 MHz); the DCMAC clock crossing is in dcmac_rx_path / dcmac_tx_path.
//
// Ports: index 0 = QSFP0 (DCMAC0), index 1 = QSFP2 (DCMAC1). Lab setup: a
// cable QSFP0 <-> QSFP2.
//
//   H2C (512) -> store and forward (the shell gives the qid on the tlast
//               beat only) -> port by queue id (TX_QBASE / TX_QCNT per port; no
//               match: dropped, counted) -> 512->1024 packer -> frame arbiter with
//               the hairpin stream -> m_axis_tx (dcmac_tx_path pads to 60 B)
//   s_axis_rx (1024) -> hairpin (PCTRL.hairpin: the frame goes back out of
//               the same port, optional MAC swap) or -> 1024->512 -> nic_rxq
//               (MAC filter, length, store and forward; too long, shorter
//               than 14 B or malformed (512-bit beats != ceil(len / 64), e.g.
//               an empty beat) dropped, so that every C2H packet has exactly
//               the beats its ctrl_len says)
//               -> C2H frame arbiter -> C2H on RX_QID of the port, with a
//               completion entry
//
// Completion entry (CMPT_SIZE, default 1 = 16 B), as qnoc_lb writes it and
// libqdma parses it: [2:0] 0, [3] desc_used, [19:4] length, [20] eot, plus
// [21] port (0 QSFP0, 1 QSFP2), [22] multicast/broadcast destination.
// The C2H packet itself (c2h_ctrl_len) is the frame padded with zeros to whole
// 64 B beats; the host takes the frame length from the completion. The static
// derives mty from ctrl_len on every beat, and a non-zero mty on a beat that is
// not the last makes CPM5 latch a fatal WPL data parity error (hardware,
// 2026-11-16: 1504 B frames; AMD's example design drives mty 0 there), so the
// RM keeps mty at 0.
// wait_pld_pkt_id comes from one counter (PKT_ID; the CPM counts from 1 after
// its reset).
//
// Registers (AXI4-Lite, 64 KiB, 32-bit; 64-bit counters LO at a, HI at a + 4,
// cleared by CTRL.clear):
//   0x000 ID 0x4E494331 ("NIC1")  0x004 VERSION (0x00010003: H2C store and forward, port from the tlast-beat qid; 0x00010002: C2H padded to 64 B; 0x00010001: RX_MALFORMED)  0x008 SCRATCH  0x00C BUILD
//   0x010 CYCLES (64)
//   0x020 CTRL     [0] clear counters (self-clearing)
//   0x024 PKT_ID   [15:0] next wait_pld_pkt_id (write to set)
//   0x028 MAX_LEN  [15:0] longest frame to the host, bytes (default 4096 = one
//                  SLASH C2H buffer; at most RXQ_BEATS * 64)
//   0x02C CMPT_SIZE [1:0]
//   0x030 H2C_NOPORT (64)  H2C packets whose qid matches no port
//   0x038 H2C_ZERO   (64)  zero-byte / error H2C packets, dropped
//   0x040 STS_OK (64)  0x048 STS_DROP (64)  0x050 STS_ERR (64)   C2H status
//   0x058 CMPT_SENT (64)
//   0x060 EVT     [0] event overflow (sticky) [1] framing error (sticky)
// Per port p at 0x100 + 0x100 * p:
//   +0x00 PCTRL    [0] rx_en [1] tx_en [2] promiscuous [3] hairpin
//                  [4] hairpin MAC swap   (reset: rx_en, tx_en)
//   +0x04 RX_QID   [11:0] C2H queue of the port
//   +0x08 TX_QBASE [11:0]  +0x0C TX_QCNT [12:0] (H2C qids base .. base+cnt-1)
//   +0x10 MAC_LO   bytes 2..5 of the MAC ([31:24] byte 2)
//   +0x14 MAC_HI   [15:0] bytes 0..1 ([15:8] byte 0)
//   +0x18 PHY_CTRL [15:0] to the DCMAC hierarchy, as eth_test's PHY_CTRL
//   +0x1C PHY_STATUS (R)   +0x20 RX_DROPS (R) from dcmac_rx_path
//   +0x40 TX_FRAMES  +0x48 TX_BYTES      (to the DCMAC, H2C and hairpin)
//   +0x50 RX_FRAMES  +0x58 RX_BYTES      (from the DCMAC)
//   +0x60 RX_HOST    (frames queued for C2H)
//   +0x68 RX_FILTERED (MAC filter or rx_en 0)  +0x70 RX_TOOLONG
//   +0x78 HAIRPIN    (frames sent back)        +0x80 TX_H2C (frames from H2C)
//   +0x88 TX_DROP    (H2C frames while tx_en 0)
//   +0x90 C2H_DROP   (C2H status drop on RX_QID: no descriptor)
//   +0x98 RX_MALFORMED (shorter than 14 B, or beats not matching the length)
module nic_core #(
    parameter integer BUILD     = 1,
    parameter integer RXQ_BEATS = 512        // per-port RX buffer, 64 B beats (power of two)
) (
    input  logic          clk,
    input  logic          rstn,

    // qdma_noc_rm user side
    input  logic [511:0]  h2c_tdata,
    input  logic          h2c_tvalid,
    output logic          h2c_tready,
    input  logic          h2c_tlast,
    input  logic [11:0]   h2c_qid,
    input  logic          h2c_err,
    input  logic [5:0]    h2c_mty,
    input  logic          h2c_zero_byte,

    output logic [511:0]  c2h_tdata,
    output logic          c2h_tvalid,
    input  logic          c2h_tready,
    output logic          c2h_tlast,
    output logic [15:0]   c2h_ctrl_len,
    output logic [11:0]   c2h_ctrl_qid,
    output logic          c2h_ctrl_has_cmpt,

    output logic [511:0]  cmpt_data,
    output logic          cmpt_tvalid,
    input  logic          cmpt_tready,
    output logic [1:0]    cmpt_size,
    output logic [12:0]   cmpt_qid,
    output logic [15:0]   cmpt_wait_pld_pkt_id,

    input  logic          c2h_sts_valid,
    input  logic          c2h_sts_drop,
    input  logic          c2h_sts_error,
    input  logic [11:0]   c2h_sts_qid,
    input  logic          evt_overflow,
    input  logic          noc_status,

    // Ethernet ports (0: QSFP0, 1: QSFP2)
    input  logic [1023:0] rx0_tdata,  input  logic [127:0] rx0_tkeep,  input  logic rx0_tlast,
    input  logic          rx0_tvalid, output logic         rx0_tready,
    output logic [1023:0] tx0_tdata,  output logic [127:0] tx0_tkeep,  output logic tx0_tlast,
    output logic          tx0_tvalid, input  logic         tx0_tready,
    input  logic [15:0]   rx0_drop_count,
    output logic [15:0]   phy0_ctrl,
    input  logic [15:0]   phy0_status,

    input  logic [1023:0] rx1_tdata,  input  logic [127:0] rx1_tkeep,  input  logic rx1_tlast,
    input  logic          rx1_tvalid, output logic         rx1_tready,
    output logic [1023:0] tx1_tdata,  output logic [127:0] tx1_tkeep,  output logic tx1_tlast,
    output logic          tx1_tvalid, input  logic         tx1_tready,
    input  logic [15:0]   rx1_drop_count,
    output logic [15:0]   phy1_ctrl,
    input  logic [15:0]   phy1_status,

    // registers
    input  logic [15:0]   s_axil_awaddr,
    input  logic          s_axil_awvalid,
    output logic          s_axil_awready,
    input  logic [31:0]   s_axil_wdata,
    input  logic          s_axil_wvalid,
    output logic          s_axil_wready,
    output logic [1:0]    s_axil_bresp,
    output logic          s_axil_bvalid,
    input  logic          s_axil_bready,
    input  logic [15:0]   s_axil_araddr,
    input  logic          s_axil_arvalid,
    output logic          s_axil_arready,
    output logic [31:0]   s_axil_rdata,
    output logic [1:0]    s_axil_rresp,
    output logic          s_axil_rvalid,
    input  logic          s_axil_rready
);
    // bytes in a beat: tkeep is contiguous from bit 0
    function automatic [6:0] nic_kb(input [63:0] k);
        nic_kb = 7'd0;
        for (int i = 0; i < 64; i++) if (k[i]) nic_kb = 7'(i + 1);
    endfunction
    localparam integer NP = 2;

    // ------------------------------------------------------------ configuration
    logic        clear;
    logic [15:0] max_len, pkt_id;
    logic [1:0]  cmpt_size_r;
    logic        pkt_id_wr;
    logic [15:0] pkt_id_wdata;
    logic [NP-1:0] rx_en, tx_en, promisc, hairpin, hp_swap;
    logic [11:0] rx_qid   [NP];
    logic [11:0] tx_qbase [NP];
    logic [12:0] tx_qcnt  [NP];
    logic [47:0] mac      [NP];
    logic [15:0] phy_ctrl_r [NP];

    assign phy0_ctrl = phy_ctrl_r[0];
    assign phy1_ctrl = phy_ctrl_r[1];

    // port arrays of the Ethernet side
    logic [1023:0] rx_tdata [NP];  logic [127:0] rx_tkeep [NP];
    logic [NP-1:0] rx_tlast, rx_tvalid, rx_tready;
    logic [1023:0] tx_tdata [NP];  logic [127:0] tx_tkeep [NP];
    logic [NP-1:0] tx_tlast, tx_tvalid, tx_tready;
    logic [15:0]   phy_status [NP], rx_drops [NP];

    assign rx_tdata[0] = rx0_tdata;  assign rx_tkeep[0] = rx0_tkeep;
    assign rx_tlast[0] = rx0_tlast;  assign rx_tvalid[0] = rx0_tvalid;  assign rx0_tready = rx_tready[0];
    assign rx_tdata[1] = rx1_tdata;  assign rx_tkeep[1] = rx1_tkeep;
    assign rx_tlast[1] = rx1_tlast;  assign rx_tvalid[1] = rx1_tvalid;  assign rx1_tready = rx_tready[1];
    assign tx0_tdata = tx_tdata[0];  assign tx0_tkeep = tx_tkeep[0];  assign tx0_tlast = tx_tlast[0];
    assign tx0_tvalid = tx_tvalid[0];  assign tx_tready[0] = tx0_tready;
    assign tx1_tdata = tx_tdata[1];  assign tx1_tkeep = tx_tkeep[1];  assign tx1_tlast = tx_tlast[1];
    assign tx1_tvalid = tx_tvalid[1];  assign tx_tready[1] = tx1_tready;
    assign phy_status[0] = phy0_status;  assign phy_status[1] = phy1_status;
    assign rx_drops[0] = rx0_drop_count;  assign rx_drops[1] = rx1_drop_count;

    // bytes of a contiguous keep (frames are contiguous from byte 0)
    function automatic [7:0] keep_bytes128(input [127:0] k);
        keep_bytes128 = 8'd0;
        for (int i = 0; i < 128; i++) if (k[i]) keep_bytes128 = 8'(i + 1);
    endfunction

    // ------------------------------------------------------------ H2C store and forward
    // qdma_noc_rm gives qid, err, zero_byte and mty only on the tlast beat (they
    // travel in the trailer), so a packet's port is known at its end: each packet
    // is buffered whole and leaves with its tlast-beat fields on every beat.
    logic [511:0] hs_tdata;
    logic         hs_tvalid, hs_tready, hs_tlast, hs_err, hs_zero_byte;
    logic [11:0]  hs_qid;
    logic [5:0]   hs_mty;
    nic_h2c_sf u_h2c_sf (
        .clk(clk), .rstn(rstn),
        .s_tdata(h2c_tdata), .s_tlast(h2c_tlast), .s_tvalid(h2c_tvalid), .s_tready(h2c_tready),
        .s_qid(h2c_qid), .s_err(h2c_err), .s_zero_byte(h2c_zero_byte), .s_mty(h2c_mty),
        .m_tdata(hs_tdata), .m_tlast(hs_tlast), .m_tvalid(hs_tvalid), .m_tready(hs_tready),
        .m_qid(hs_qid), .m_err(hs_err), .m_zero_byte(hs_zero_byte), .m_mty(hs_mty));

    // ------------------------------------------------------------ H2C -> port
    // The port is chosen on the first beat of a packet (from the stored packet's
    // tlast-beat qid) and held to its tlast.
    logic        h_sop;
    logic [1:0]  h_sel, h_sel_q;          // 0/1 port, 2 drop
    logic        h2c_fire;
    logic [NP-1:0] pk_ready;
    logic [NP-1:0] h_match;
    always_comb begin
        for (int p = 0; p < NP; p++)
            h_match[p] = ({1'b0, hs_qid} >= {1'b0, tx_qbase[p]}) &&
                         ({1'b0, hs_qid} < {1'b0, tx_qbase[p]} + tx_qcnt[p]);
        if (hs_zero_byte || hs_err)       h_sel = 2'd2;
        else if (h_match[0] && tx_en[0])     h_sel = 2'd0;
        else if (h_match[1] && tx_en[1])     h_sel = 2'd1;
        else                                 h_sel = 2'd2;
    end
    logic [1:0] h_cur;
    assign h_cur = h_sop ? h_sel : h_sel_q;
    assign hs_tready = (h_cur == 2'd2) ? 1'b1 : pk_ready[h_cur[0]];
    assign h2c_fire   = hs_tvalid && hs_tready;

    // per-port packer state (512 -> 1024)
    logic [511:0]  pk_lo   [NP];
    logic [63:0]   pk_lokeep [NP];
    logic [NP-1:0] pk_half;               // low half held
    logic [1023:0] pk_tdata [NP];  logic [127:0] pk_tkeep [NP];
    logic [NP-1:0] pk_tlast, pk_tvalid, pk_tready;
    logic [63:0]   h_keep;
    always_comb begin
        for (int b = 0; b < 64; b++) h_keep[b] = !hs_tlast || (b < 64 - hs_mty);
    end

    // the packer output register
    for (genvar p = 0; p < NP; p++) begin : g_pack
        logic in_v;
        assign in_v = hs_tvalid && h_cur == 2'(p);
        // ready: room in the output register, or the low half can be latched
        assign pk_ready[p] = !pk_tvalid[p] || pk_tready[p] || (!pk_half[p] && !hs_tlast);
        always_ff @(posedge clk) begin
            if (!rstn) begin
                pk_half[p] <= 1'b0; pk_tvalid[p] <= 1'b0; pk_tlast[p] <= 1'b0;
            end else begin
                if (pk_tvalid[p] && pk_tready[p]) pk_tvalid[p] <= 1'b0;
                if (in_v && pk_ready[p]) begin
                    if (!pk_half[p] && !hs_tlast) begin
                        pk_lo[p] <= hs_tdata; pk_lokeep[p] <= h_keep; pk_half[p] <= 1'b1;
                    end else if (!pk_half[p]) begin           // single-half last beat
                        pk_tdata[p] <= {512'd0, hs_tdata}; pk_tkeep[p] <= {64'd0, h_keep};
                        pk_tlast[p] <= 1'b1; pk_tvalid[p] <= 1'b1;
                    end else begin
                        pk_tdata[p] <= {hs_tdata, pk_lo[p]}; pk_tkeep[p] <= {h_keep, pk_lokeep[p]};
                        pk_tlast[p] <= hs_tlast; pk_tvalid[p] <= 1'b1; pk_half[p] <= 1'b0;
                    end
                end
            end
        end
    end

    always_ff @(posedge clk) begin
        if (!rstn) begin
            h_sop <= 1'b1; h_sel_q <= 2'd2;
        end else if (h2c_fire) begin
            h_sop <= hs_tlast;
            if (h_sop) h_sel_q <= h_sel;
        end
    end

    // ------------------------------------------------------------ RX: hairpin split
    // hp_*: frames going back out of the same port; ru_*: frames to the host path
    logic [1023:0] hp_tdata [NP];  logic [127:0] hp_tkeep [NP];
    logic [NP-1:0] hp_tlast, hp_tvalid, hp_tready;
    logic [1023:0] ru_tdata [NP];  logic [127:0] ru_tkeep [NP];
    logic [NP-1:0] ru_tlast, ru_tvalid, ru_tready;
    logic [NP-1:0] r_sop, r_hp_q, r_hp;

    for (genvar p = 0; p < NP; p++) begin : g_rxsplit
        assign r_hp[p] = r_sop[p] ? hairpin[p] : r_hp_q[p];
        logic [1023:0] d;
        always_comb begin
            d = rx_tdata[p];
            if (r_sop[p] && hp_swap[p]) begin     // dst <-> src (bytes 0..5 and 6..11)
                d[47:0]  = rx_tdata[p][95:48];
                d[95:48] = rx_tdata[p][47:0];
            end
        end
        assign hp_tdata[p] = d;                 assign hp_tkeep[p] = rx_tkeep[p];
        assign hp_tlast[p] = rx_tlast[p];       assign hp_tvalid[p] = rx_tvalid[p] && r_hp[p];
        assign ru_tdata[p] = rx_tdata[p];       assign ru_tkeep[p] = rx_tkeep[p];
        assign ru_tlast[p] = rx_tlast[p];       assign ru_tvalid[p] = rx_tvalid[p] && !r_hp[p];
        assign rx_tready[p] = r_hp[p] ? hp_tready[p] : ru_tready[p];
        always_ff @(posedge clk) begin
            if (!rstn) begin
                r_sop[p] <= 1'b1; r_hp_q[p] <= 1'b0;
            end else if (rx_tvalid[p] && rx_tready[p]) begin
                r_sop[p] <= rx_tlast[p];
                if (r_sop[p]) r_hp_q[p] <= hairpin[p];
            end
        end
    end

    // ------------------------------------------------------------ TX: frame arbiter (H2C, hairpin)
    logic [NP-1:0] t_busy, t_src;          // t_src: 0 H2C, 1 hairpin
    logic [NP-1:0] t_cur;                  // source of the beat on tx_* now
    for (genvar p = 0; p < NP; p++) begin : g_txarb
        logic pick_hp, last_hp;
        // round robin at frame boundaries
        assign pick_hp = hp_tvalid[p] && (!pk_tvalid[p] || !last_hp);
        logic src;
        assign src = t_busy[p] ? t_src[p] : pick_hp;
        assign t_cur[p] = src;
        assign tx_tvalid[p] = src ? hp_tvalid[p] : pk_tvalid[p];
        assign tx_tdata[p]  = src ? hp_tdata[p]  : pk_tdata[p];
        assign tx_tkeep[p]  = src ? hp_tkeep[p]  : pk_tkeep[p];
        assign tx_tlast[p]  = src ? hp_tlast[p]  : pk_tlast[p];
        assign hp_tready[p] = src && tx_tready[p];
        assign pk_tready[p] = !src && tx_tready[p];
        always_ff @(posedge clk) begin
            if (!rstn) begin
                t_busy[p] <= 1'b0; t_src[p] <= 1'b0; last_hp <= 1'b0;
            end else if (tx_tvalid[p] && tx_tready[p]) begin
                t_busy[p] <= !tx_tlast[p];
                if (!t_busy[p]) begin t_src[p] <= src; last_hp <= src; end
            end
        end
    end

    // ------------------------------------------------------------ RX: 1024 -> 512 -> nic_rxq
    logic [511:0]  q_tdata [NP];
    logic [NP-1:0] q_tlast, q_tvalid, q_tready;
    logic [15:0]   q_len [NP], q_lenp [NP];   // frame length; padded to 64 B
    logic [NP-1:0] q_mcast;
    logic [NP-1:0] rq_host, rq_filt, rq_long, rq_bad;    // one-cycle events from nic_rxq

    for (genvar p = 0; p < NP; p++) begin : g_rxq
        // split: the high half goes out only if it has bytes
        logic        sp_hi;                  // low half sent, high half pending
        logic [511:0] x_tdata, s_tdata;
        logic [63:0]  x_tkeep, s_tkeep;
        logic         x_tlast, x_tvalid, x_tready, s_tlast, s_tvalid, s_tready;
        logic         has_hi;
        assign has_hi   = ru_tkeep[p][64];
        assign x_tvalid = ru_tvalid[p];
        assign x_tdata  = sp_hi ? ru_tdata[p][1023:512] : ru_tdata[p][511:0];
        assign x_tkeep  = sp_hi ? ru_tkeep[p][127:64]   : ru_tkeep[p][63:0];
        assign x_tlast  = ru_tlast[p] && (sp_hi || !has_hi);
        assign ru_tready[p] = x_tready && (sp_hi || !has_hi);
        always_ff @(posedge clk) begin
            if (!rstn) sp_hi <= 1'b0;
            else if (x_tvalid && x_tready) sp_hi <= !sp_hi && has_hi;
        end

        // register slice: the DCMAC RX CDC FIFO's block RAM output reaches
        // nic_rxq's filter/length logic and its ready only through registers
        // (build 2 failed at -0.26 ns on that path)
        // bytes outside tkeep are zeroed: the padding of the last beat reaches the host
        logic [511:0] x_kmask;
        always_comb for (int i = 0; i < 64; i++) x_kmask[8*i +: 8] = {8{x_tkeep[i]}};
        logic [511:0] y_tdata;
        logic [63:0]  y_tkeep;
        logic         y_tlast, y_tvalid, y_tready;
        logic [6:0]   s_kb;
        qnoc_fifo #(.W(577), .DEPTH(2)) u_rx_slice (
            .clk(clk), .rstn(rstn),
            .s_valid(x_tvalid), .s_ready(x_tready), .s_data({x_tlast, x_tkeep, x_tdata & x_kmask}),
            .m_valid(y_tvalid), .m_ready(y_tready), .m_data({y_tlast, y_tkeep, y_tdata}));
        // second slice: carries the beat's byte count, so that nic_rxq's
        // commit (the packet FIFO write) does not wait on the tkeep encoder
        // (build 4 routed at -0.146 ns on that path)
        qnoc_fifo #(.W(584), .DEPTH(2)) u_rx_slice2 (
            .clk(clk), .rstn(rstn),
            .s_valid(y_tvalid), .s_ready(y_tready), .s_data({nic_kb(y_tkeep), y_tlast, y_tkeep, y_tdata}),
            .m_valid(s_tvalid), .m_ready(s_tready), .m_data({s_kb, s_tlast, s_tkeep, s_tdata}));

        nic_rxq #(.BEATS(RXQ_BEATS)) u_rxq (
            .clk(clk), .rstn(rstn),
            .rx_en(rx_en[p]), .promisc(promisc[p]), .mac(mac[p]), .max_len(max_len),
            .s_tdata(s_tdata), .s_tkeep(s_tkeep), .s_kb(s_kb), .s_tlast(s_tlast), .s_tvalid(s_tvalid), .s_tready(s_tready),
            .m_tdata(q_tdata[p]), .m_tlast(q_tlast[p]), .m_tvalid(q_tvalid[p]), .m_tready(q_tready[p]),
            .m_len(q_len[p]), .m_len_pad(q_lenp[p]), .m_mcast(q_mcast[p]),
            .ev_host(rq_host[p]), .ev_filtered(rq_filt[p]), .ev_toolong(rq_long[p]), .ev_malformed(rq_bad[p])
        );
    end

    // ------------------------------------------------------------ C2H: frame arbiter + completion
    logic        c_busy, c_src, c_last_src, c_pick;
    logic        c_go, c_sop;
    logic        k_in_valid, k_in_ready, k_out_valid;
    logic [46:0] k_in, k_out;               // {mcast, port, qid 13, pkt id 16, len 16}
    assign c_pick = c_busy ? c_src : (q_tvalid[1] && (!q_tvalid[0] || !c_last_src));
    assign c_go   = !c_sop || k_in_ready;
    assign c2h_tvalid        = q_tvalid[c_pick] && c_go;
    assign c2h_tdata         = q_tdata[c_pick];
    assign c2h_tlast         = q_tlast[c_pick];
    assign c2h_ctrl_len      = q_lenp[c_pick];     // whole beats: mty 0 (see the header)
    assign c2h_ctrl_qid      = rx_qid[c_pick];
    assign c2h_ctrl_has_cmpt = 1'b1;
    assign q_tready[0] = c2h_tready && c_go && c_pick == 1'b0;
    assign q_tready[1] = c2h_tready && c_go && c_pick == 1'b1;
    logic c2h_fire;
    assign c2h_fire = c2h_tvalid && c2h_tready;

    assign k_in_valid = c2h_fire && c_sop;
    assign k_in = {q_mcast[c_pick], c_pick, 1'b0, rx_qid[c_pick], pkt_id, q_len[c_pick]};
    qnoc_fifo #(.W(47), .DEPTH(16)) u_cmpt (
        .clk(clk), .rstn(rstn), .s_valid(k_in_valid), .s_ready(k_in_ready), .s_data(k_in),
        .m_valid(k_out_valid), .m_ready(cmpt_tready), .m_data(k_out));
    assign cmpt_tvalid          = k_out_valid;
    assign cmpt_size            = cmpt_size_r;
    assign cmpt_qid             = k_out[44:32];
    assign cmpt_wait_pld_pkt_id = k_out[31:16];
    always_comb begin
        cmpt_data        = '0;
        cmpt_data[3]     = 1'b1;              // desc_used
        cmpt_data[19:4]  = k_out[15:0];       // length
        cmpt_data[20]    = 1'b1;              // eot
        cmpt_data[21]    = k_out[45];         // port
        cmpt_data[22]    = k_out[46];         // multicast / broadcast
    end

    always_ff @(posedge clk) begin
        if (!rstn) begin
            c_busy <= 1'b0; c_src <= 1'b0; c_last_src <= 1'b0; c_sop <= 1'b1; pkt_id <= 16'd1;
        end else begin
            if (c2h_fire) begin
                c_sop  <= c2h_tlast;
                c_busy <= !c2h_tlast;
                if (!c_busy) begin c_src <= c_pick; c_last_src <= c_pick; end
            end
            if (pkt_id_wr) pkt_id <= pkt_id_wdata;
            else if (k_in_valid && k_in_ready) pkt_id <= pkt_id + 1'b1;
        end
    end

    // ------------------------------------------------------------ counters
    logic [63:0] cycles, h2c_noport, h2c_zero, sts_ok, sts_drop, sts_err, cmpt_sent;
    logic [63:0] c_txf [NP], c_txb [NP], c_rxf [NP], c_rxb [NP], c_host [NP], c_filt [NP];
    logic [63:0] c_long [NP], c_hp [NP], c_txh [NP], c_txdrop [NP], c_c2hdrop [NP], c_bad [NP];
    logic        evt_ovf, framing;
    logic [NP-1:0] r_cnt_sop;

    always_ff @(posedge clk) begin
        if (!rstn || clear) begin
            cycles <= '0; h2c_noport <= '0; h2c_zero <= '0; sts_ok <= '0; sts_drop <= '0; sts_err <= '0;
            cmpt_sent <= '0;
            for (int p = 0; p < NP; p++) begin
                c_txf[p] <= '0; c_txb[p] <= '0; c_rxf[p] <= '0; c_rxb[p] <= '0; c_host[p] <= '0;
                c_filt[p] <= '0; c_long[p] <= '0; c_hp[p] <= '0; c_txh[p] <= '0; c_txdrop[p] <= '0;
                c_c2hdrop[p] <= '0; c_bad[p] <= '0;
            end
        end else begin
            cycles <= cycles + 1'b1;
            if (h2c_fire && h_sop && h_sel == 2'd2) begin
                if (hs_zero_byte || hs_err) begin
                    h2c_zero <= h2c_zero + 1'b1;
                end else if (h_match[0] && !tx_en[0]) begin
                    c_txdrop[0] <= c_txdrop[0] + 1'b1;
                end else if (h_match[1] && !tx_en[1]) begin
                    c_txdrop[1] <= c_txdrop[1] + 1'b1;
                end else begin
                    h2c_noport <= h2c_noport + 1'b1;
                end
            end
            if (cmpt_tvalid && cmpt_tready) cmpt_sent <= cmpt_sent + 1'b1;
            if (c2h_sts_valid) begin
                if (c2h_sts_drop) begin
                    sts_drop <= sts_drop + 1'b1;
                    for (int p = 0; p < NP; p++)
                        if (c2h_sts_qid == rx_qid[p]) c_c2hdrop[p] <= c_c2hdrop[p] + 1'b1;
                end else if (c2h_sts_error) sts_err <= sts_err + 1'b1;
                else sts_ok <= sts_ok + 1'b1;
            end
            for (int p = 0; p < NP; p++) begin
                if (tx_tvalid[p] && tx_tready[p]) begin
                    c_txb[p] <= c_txb[p] + keep_bytes128(tx_tkeep[p]);
                    if (tx_tlast[p]) begin
                        c_txf[p] <= c_txf[p] + 1'b1;
                        if (t_cur[p]) c_hp[p] <= c_hp[p] + 1'b1;
                        else          c_txh[p] <= c_txh[p] + 1'b1;
                    end
                end
                if (rx_tvalid[p] && rx_tready[p]) begin
                    c_rxb[p] <= c_rxb[p] + keep_bytes128(rx_tkeep[p]);
                    if (rx_tlast[p]) c_rxf[p] <= c_rxf[p] + 1'b1;
                end
                if (rq_host[p]) c_host[p] <= c_host[p] + 1'b1;
                if (rq_filt[p]) c_filt[p] <= c_filt[p] + 1'b1;
                if (rq_long[p]) c_long[p] <= c_long[p] + 1'b1;
                if (rq_bad[p])  c_bad[p]  <= c_bad[p] + 1'b1;
            end
        end
    end

    always_ff @(posedge clk) begin
        if (!rstn) begin evt_ovf <= 1'b0; framing <= 1'b0; end
        else begin
            if (evt_overflow) evt_ovf <= 1'b1;
            if (noc_status)   framing <= 1'b1;
        end
    end

    // ------------------------------------------------------------ AXI4-Lite
    logic        aw_ok, w_ok;
    logic [15:0] aw_addr;
    logic [31:0] w_data, scratch;
    assign s_axil_awready = !aw_ok && !s_axil_bvalid;
    assign s_axil_wready  = !w_ok && !s_axil_bvalid;
    assign s_axil_bresp   = 2'b00;
    assign s_axil_rresp   = 2'b00;
    logic do_wr;
    assign do_wr = aw_ok && w_ok && !s_axil_bvalid;

    always_ff @(posedge clk) begin
        if (!rstn) begin
            aw_ok <= 1'b0; w_ok <= 1'b0; s_axil_bvalid <= 1'b0; clear <= 1'b0; scratch <= '0;
            max_len <= 16'd4096; cmpt_size_r <= 2'd1; pkt_id_wr <= 1'b0; pkt_id_wdata <= '0;
            rx_en <= '1; tx_en <= '1; promisc <= '0; hairpin <= '0; hp_swap <= '0;
            for (int p = 0; p < NP; p++) begin
                rx_qid[p] <= 12'(p); tx_qbase[p] <= 12'(p); tx_qcnt[p] <= 13'd1;
                mac[p] <= {40'h02_0A_35_00_00, 8'(p)}; phy_ctrl_r[p] <= '0;
            end
        end else begin
            clear <= 1'b0; pkt_id_wr <= 1'b0;
            if (s_axil_awvalid && s_axil_awready) begin aw_ok <= 1'b1; aw_addr <= s_axil_awaddr; end
            if (s_axil_wvalid && s_axil_wready) begin w_ok <= 1'b1; w_data <= s_axil_wdata; end
            if (s_axil_bvalid && s_axil_bready) s_axil_bvalid <= 1'b0;
            if (do_wr) begin
                aw_ok <= 1'b0; w_ok <= 1'b0; s_axil_bvalid <= 1'b1;
                case ({aw_addr[15:2], 2'b00})
                    16'h008: scratch <= w_data;
                    16'h020: clear <= w_data[0];
                    16'h024: begin pkt_id_wr <= 1'b1; pkt_id_wdata <= w_data[15:0]; end
                    16'h028: max_len <= w_data[15:0];
                    16'h02C: cmpt_size_r <= w_data[1:0];
                    default: ;
                endcase
                for (int p = 0; p < NP; p++) begin
                    if (aw_addr[15:8] == 8'(1 + p)) case ({aw_addr[7:2], 2'b00})
                        8'h00: begin
                            rx_en[p] <= w_data[0]; tx_en[p] <= w_data[1]; promisc[p] <= w_data[2];
                            hairpin[p] <= w_data[3]; hp_swap[p] <= w_data[4];
                        end
                        8'h04: rx_qid[p] <= w_data[11:0];
                        8'h08: tx_qbase[p] <= w_data[11:0];
                        8'h0C: tx_qcnt[p] <= w_data[12:0];
                        8'h10: mac[p][31:0] <= w_data;
                        8'h14: mac[p][47:32] <= w_data[15:0];
                        8'h18: phy_ctrl_r[p] <= w_data[15:0];
                        default: ;
                    endcase
                end
            end
        end
    end

    // reads: one register stage
    logic [63:0] rd64;
    logic [31:0] rd32;
    logic        is64;
    always_comb begin
        rd64 = '0; rd32 = '0; is64 = 1'b0;
        if (s_axil_araddr[15:8] == 8'h00) begin
            case ({s_axil_araddr[7:3], 3'b000})
                8'h10: begin rd64 = cycles;     is64 = 1'b1; end
                8'h30: begin rd64 = h2c_noport; is64 = 1'b1; end
                8'h38: begin rd64 = h2c_zero;   is64 = 1'b1; end
                8'h40: begin rd64 = sts_ok;     is64 = 1'b1; end
                8'h48: begin rd64 = sts_drop;   is64 = 1'b1; end
                8'h50: begin rd64 = sts_err;    is64 = 1'b1; end
                8'h58: begin rd64 = cmpt_sent;  is64 = 1'b1; end
                default: ;
            endcase
            case ({s_axil_araddr[7:2], 2'b00})
                8'h00: rd32 = 32'h4E49_4331;
                8'h04: rd32 = 32'h0001_0003;
                8'h08: rd32 = scratch;
                8'h0C: rd32 = BUILD;
                8'h24: rd32 = {16'd0, pkt_id};
                8'h28: rd32 = {16'd0, max_len};
                8'h2C: rd32 = {30'd0, cmpt_size_r};
                8'h60: rd32 = {30'd0, framing, evt_ovf};
                default: ;
            endcase
        end
        for (int p = 0; p < NP; p++) begin
            if (s_axil_araddr[15:8] == 8'(1 + p)) begin
                case ({s_axil_araddr[7:3], 3'b000})
                    8'h40: begin rd64 = c_txf[p];     is64 = 1'b1; end
                    8'h48: begin rd64 = c_txb[p];     is64 = 1'b1; end
                    8'h50: begin rd64 = c_rxf[p];     is64 = 1'b1; end
                    8'h58: begin rd64 = c_rxb[p];     is64 = 1'b1; end
                    8'h60: begin rd64 = c_host[p];    is64 = 1'b1; end
                    8'h68: begin rd64 = c_filt[p];    is64 = 1'b1; end
                    8'h70: begin rd64 = c_long[p];    is64 = 1'b1; end
                    8'h78: begin rd64 = c_hp[p];      is64 = 1'b1; end
                    8'h80: begin rd64 = c_txh[p];     is64 = 1'b1; end
                    8'h88: begin rd64 = c_txdrop[p];  is64 = 1'b1; end
                    8'h90: begin rd64 = c_c2hdrop[p]; is64 = 1'b1; end
                    8'h98: begin rd64 = c_bad[p];     is64 = 1'b1; end
                    default: ;
                endcase
                case ({s_axil_araddr[7:2], 2'b00})
                    8'h00: rd32 = {27'd0, hp_swap[p], hairpin[p], promisc[p], tx_en[p], rx_en[p]};
                    8'h04: rd32 = {20'd0, rx_qid[p]};
                    8'h08: rd32 = {20'd0, tx_qbase[p]};
                    8'h0C: rd32 = {19'd0, tx_qcnt[p]};
                    8'h10: rd32 = mac[p][31:0];
                    8'h14: rd32 = {16'd0, mac[p][47:32]};
                    8'h18: rd32 = {16'd0, phy_ctrl_r[p]};
                    8'h1C: rd32 = {16'd0, phy_status[p]};
                    8'h20: rd32 = {16'd0, rx_drops[p]};
                    default: ;
                endcase
            end
        end
    end

    always_ff @(posedge clk) begin
        if (!rstn) begin
            s_axil_arready <= 1'b0; s_axil_rvalid <= 1'b0; s_axil_rdata <= '0;
        end else begin
            s_axil_arready <= 1'b0;
            if (s_axil_rvalid && s_axil_rready) s_axil_rvalid <= 1'b0;
            if (s_axil_arvalid && !s_axil_arready && !s_axil_rvalid) begin
                s_axil_arready <= 1'b1;
                s_axil_rvalid  <= 1'b1;
                s_axil_rdata   <= is64 ? (s_axil_araddr[2] ? rd64[63:32] : rd64[31:0]) : rd32;
            end
        end
    end
endmodule

// nic_rxq: per-port RX to the host. 512-bit frames in (contiguous keep from
// byte 0), MAC filter on the first beat, length counted, frames longer than
// max_len dropped, store and forward so that the length is known on the first
// C2H beat. Frames shorter than 14 B, or whose beat count is not
// ceil(len / 64) (an empty beat from the MAC), are dropped as malformed: the
// static derives mty and the payload CRC from ctrl_len, and a C2H packet whose
// beats do not match it is a fatal QDMA error (WPL data parity, 2026-11-15). Frames that are dropped are consumed without being stored. The
// input is back-pressured when the buffer is full (dcmac_rx_path absorbs it,
// and its FCS filter drops frames that do not fit).
module nic_rxq #(
    parameter integer BEATS = 512            // 64 B beats, power of two
) (
    input  logic         clk,
    input  logic         rstn,
    input  logic         rx_en,
    input  logic         promisc,
    input  logic [47:0]  mac,                // [47:40] byte 0
    input  logic [15:0]  max_len,

    input  logic [511:0] s_tdata,
    input  logic [63:0]  s_tkeep,
    input  logic [6:0]   s_kb,               // bytes in the beat, nic_kb(s_tkeep), a stage earlier
    input  logic         s_tlast,
    input  logic         s_tvalid,
    output logic         s_tready,

    output logic [511:0] m_tdata,
    output logic         m_tlast,
    output logic         m_tvalid,
    input  logic         m_tready,
    output logic [15:0]  m_len,
    output logic [15:0]  m_len_pad,          // m_len rounded up to whole 64 B beats
    output logic         m_mcast,

    output logic         ev_host,
    output logic         ev_filtered,
    output logic         ev_toolong,
    output logic         ev_malformed
);
    // bytes in a beat: tkeep is contiguous from bit 0
    function automatic [6:0] nic_kb(input [63:0] k);
        nic_kb = 7'd0;
        for (int i = 0; i < 64; i++) if (k[i]) nic_kb = 7'(i + 1);
    endfunction
    localparam integer A = $clog2(BEATS);
    logic [512:0] mem [0:BEATS-1];          // {last, data}
    logic [A:0]   wr, wr_start, rd;         // wr_start: first beat of the frame being written
    logic [A:0]   used;
    assign used = wr - rd;

    // first-beat decisions
    logic         sop, keep_q, mcast_q;
    logic [47:0]  dst;
    always_comb for (int i = 0; i < 6; i++) dst[47 - 8*i -: 8] = s_tdata[8*i +: 8];
    logic accept_now, mcast_now;
    assign mcast_now  = s_tdata[0];
    assign accept_now = rx_en && (promisc || mcast_now || dst == mac);
    logic keep_cur;
    assign keep_cur = sop ? accept_now : keep_q;

    // length so far (with this beat)
    logic [15:0] len_q, len_now;
    assign len_now = (sop ? 16'd0 : len_q) + {9'd0, s_kb};
    logic long_q;
    // 512-bit beats of the frame so far (with this beat); malformed is
    // evaluated on the last beat
    logic [10:0] nb_q, nb_now;
    assign nb_now = (sop ? 11'd0 : nb_q) + 11'd1;

    // The checks below decide commit (the packet FIFO write), so they avoid
    // the 16-bit carry of len_now (build 4 routed at -0.146 ns through it);
    // each is equal to the plain formula in the comment (checked in
    // simulation, below).
    // too long: len_now > max_len. room_q = max_len - len_q (17-bit signed).
    logic [16:0] room_q;
    logic too_long_now;
    assign too_long_now = sop ? ((max_len[15:7] == 9'd0) && (s_kb > max_len[6:0]))
                              : (room_q[16] || ((room_q[15:7] == 9'd0) && (s_kb > room_q[6:0])));
    // beats: nb_now == ceil(len_now / 64). With len_q = 64 a + r,
    // ceil(len_now / 64) = a + c, c = ceil((r + kb) / 64) in 0..2, and
    // e_q = nb_q - a + 1, so the test is e_q == c (sop: 1 == c).
    logic [6:0]  r_now;
    logic [1:0]  c_now;
    logic [10:0] e_q;
    assign r_now = (sop ? 7'd0 : {1'b0, len_q[5:0]}) + s_kb;
    assign c_now = (r_now == 7'd0) ? 2'd0 : (r_now <= 7'd64) ? 2'd1 : 2'd2;
    logic nb_ok;
    assign nb_ok = sop ? (c_now == 2'd1) : (e_q == {9'd0, c_now});
    // short: len_now < 14. lt14_q = len_q < 14.
    logic       lt14_q;
    logic [6:0] sl_now;
    assign sl_now = (sop ? 7'd0 : {3'd0, len_q[3:0]}) + s_kb;
    logic short_now;
    assign short_now = (sop || lt14_q) && (sl_now < 7'd14);
    logic malformed_now;
    assign malformed_now = short_now || !nb_ok;
    logic bad_now;
    assign bad_now = too_long_now || long_q || malformed_now;

    // packet FIFO {mcast, len}
    logic p_in_valid, p_in_ready, p_out_valid, p_out_ready;
    logic [32:0] p_in, p_out;               // {mcast, beats * 64, len}
    qnoc_fifo #(.W(33), .DEPTH(16)) u_pkt (
        .clk(clk), .rstn(rstn), .s_valid(p_in_valid), .s_ready(p_in_ready), .s_data(p_in),
        .m_valid(p_out_valid), .m_ready(p_out_ready), .m_data(p_out));

    // write: a stored beat needs room; the last beat of a kept frame also needs packet FIFO room
    logic store;                              // this beat goes into the buffer
    assign store = keep_cur && !(long_q && !sop);
    assign s_tready = (!store || (used != BEATS[A:0])) && (!s_tlast || !keep_cur || p_in_ready);
    logic s_fire;
    assign s_fire = s_tvalid && s_tready;

    logic commit, discard;
    assign commit  = s_fire && s_tlast && keep_cur && !bad_now;
    assign discard = s_fire && s_tlast && keep_cur && bad_now;
    assign p_in_valid = commit;
    // a committed frame has exactly ceil(len / 64) beats (else it is malformed)
    assign p_in       = {sop ? mcast_now : mcast_q, 16'({nb_now[9:0], 6'd0}), len_now};

    always_ff @(posedge clk) begin
        if (store && s_fire)
            mem[wr[A-1:0]] <= {s_tlast, s_tdata};
    end

    always_ff @(posedge clk) begin
        if (!rstn) begin
            wr <= '0; wr_start <= '0; sop <= 1'b1; keep_q <= 1'b0; mcast_q <= 1'b0; len_q <= '0;
            long_q <= 1'b0; ev_host <= 1'b0; ev_filtered <= 1'b0; ev_toolong <= 1'b0;
            ev_malformed <= 1'b0; nb_q <= '0; room_q <= '0; e_q <= '0; lt14_q <= 1'b1;
        end else begin
            ev_host <= 1'b0; ev_filtered <= 1'b0; ev_toolong <= 1'b0; ev_malformed <= 1'b0;
            if (s_fire) begin
                sop   <= s_tlast;
                len_q <= len_now;
                nb_q  <= nb_now;
                room_q <= {1'b0, max_len} - {1'b0, len_now};
                e_q    <= nb_now - {1'b0, len_now[15:6]} + 11'd1;
                lt14_q <= len_now < 16'd14;
                if (sop) begin keep_q <= accept_now; mcast_q <= mcast_now; end
                // once too long, stop storing the rest of the frame
                if (keep_cur && too_long_now) long_q <= 1'b1;
                if (s_tlast) long_q <= 1'b0;
                if (store) wr <= wr + 1'b1;
                if (commit) begin
                    wr_start <= wr + 1'b1;
                    ev_host <= 1'b1;
                end
                if (discard) begin
                    wr <= wr_start;              // drop what was stored of the frame
                    if (too_long_now || long_q) ev_toolong <= 1'b1;
                    else                        ev_malformed <= 1'b1;
                end
                if (s_tlast && !keep_cur) ev_filtered <= 1'b1;
            end
        end
    end

    // read side: block RAM read (latency 1) into a 2-entry output FIFO
    logic [A:0]   committed;                  // write pointer of complete frames
    always_ff @(posedge clk) begin
        if (!rstn) committed <= '0;
        else if (commit) committed <= wr + 1'b1;
    end
    logic         o_ready;
    logic [512:0] o_data_in, o_data;
    logic         o_valid;
    logic         rd_pend;
    logic [1:0]   o_cnt;
    logic         can_read;
    assign can_read = (rd != committed) && (o_cnt + rd_pend < 2'd2);
    always_ff @(posedge clk) begin
        if (!rstn) begin
            rd <= '0; rd_pend <= 1'b0;
        end else begin
            rd_pend <= can_read;
            if (can_read) rd <= rd + 1'b1;
        end
    end
    always_ff @(posedge clk) o_data_in <= mem[rd[A-1:0]];
    qnoc_fifo #(.W(513), .DEPTH(2)) u_out (
        .clk(clk), .rstn(rstn), .s_valid(rd_pend), .s_ready(), .s_data(o_data_in),
        .m_valid(o_valid), .m_ready(o_ready), .m_data(o_data));
    always_ff @(posedge clk) begin
        if (!rstn) o_cnt <= '0;
        else o_cnt <= o_cnt + (rd_pend ? 2'd1 : 2'd0) - ((o_valid && o_ready) ? 2'd1 : 2'd0);
    end

    // a frame leaves only when its packet entry is there (always true once committed)
    assign m_tvalid    = o_valid && p_out_valid;
    assign m_tdata     = o_data[511:0];
    assign m_tlast     = o_data[512];
    assign m_len       = p_out[15:0];
    assign m_len_pad   = p_out[31:16];
    assign m_mcast     = p_out[32];
    assign o_ready     = m_tready && p_out_valid;
    assign p_out_ready = m_tready && o_valid && o_data[512];

    // synthesis translate_off
    // the timing-friendly checks above against the plain formulas
    always @(posedge clk) begin
        if (rstn && s_tvalid && s_tready) begin
            if (s_kb !== nic_kb(s_tkeep) ||
                too_long_now !== (len_now > max_len) ||
                malformed_now !== ((len_now < 16'd14) || (nb_now != 11'(({1'b0, len_now} + 17'd63) >> 6)))) begin
                $display("FAIL: nic_rxq check mismatch at %0t: kb %0d/%0d len %0d nb %0d max %0d too_long %b malformed %b",
                         $time, s_kb, nic_kb(s_tkeep), len_now, nb_now, max_len, too_long_now, malformed_now);
                $fatal(1);
            end
        end
    end
    // synthesis translate_on
endmodule

// nic_h2c_sf: H2C store and forward. The shell (qdma_noc_rm) gives qid, err,
// zero_byte and mty with the tlast beat only, so whole packets are buffered and
// each leaves with those fields valid on all its beats. A packet longer than
// MAXB beats (libqdma never sends one: one descriptor of at most 4 KiB per
// packet; the default leaves room for 8 KiB frames) is not stored: it leaves
// as its tlast beat alone with err set, so nic_core drops and counts it
// (H2C_ZERO) instead of the buffer filling up.
module nic_h2c_sf #(
    parameter integer BEATS = 256,          // buffer, 64 B beats (power of two)
    parameter integer MAXB  = 128           // longest packet stored (8 KiB), below BEATS
) (
    input  logic         clk,
    input  logic         rstn,
    input  logic [511:0] s_tdata,
    input  logic         s_tlast,
    input  logic         s_tvalid,
    output logic         s_tready,
    input  logic [11:0]  s_qid,
    input  logic         s_err,
    input  logic         s_zero_byte,
    input  logic [5:0]   s_mty,
    output logic [511:0] m_tdata,
    output logic         m_tlast,
    output logic         m_tvalid,
    input  logic         m_tready,
    output logic [11:0]  m_qid,
    output logic         m_err,
    output logic         m_zero_byte,
    output logic [5:0]   m_mty
);
    localparam integer A = $clog2(BEATS);
    localparam integer NB = $clog2(MAXB + 1);
    logic [512:0] mem [0:BEATS-1];          // {last, data}
    logic [A:0]   wr, wr_start, rd, committed;
    logic [A:0]   used;
    assign used = wr - rd;

    // beats of the packet being written; too long: drop it, keep only its tlast beat
    logic [NB-1:0] nb_q;                    // beats so far (saturates at MAXB)
    logic         sop, long_q, long_now;
    assign long_now = long_q || (!sop && nb_q == NB'(MAXB));

    // packet FIFO {zero_byte, err, mty, qid}
    logic p_in_ready, p_out_valid, p_out_ready, p_in_valid;
    logic [19:0] p_in, p_out;
    qnoc_fifo #(.W(20), .DEPTH(16)) u_pkt (
        .clk(clk), .rstn(rstn), .s_valid(p_in_valid), .s_ready(p_in_ready), .s_data(p_in),
        .m_valid(p_out_valid), .m_ready(p_out_ready), .m_data(p_out));

    logic store, s_fire;
    assign store    = !long_now || s_tlast;
    assign s_tready = (!store || (used != BEATS[A:0])) && (!s_tlast || p_in_ready);
    assign s_fire   = s_tvalid && s_tready;
    assign p_in_valid = s_fire && s_tlast;
    assign p_in       = {s_zero_byte, s_err || long_now, s_mty, s_qid};

    logic [A:0] wa;                          // a too-long packet's tlast beat goes to its start
    assign wa = long_now ? wr_start : wr;
    always_ff @(posedge clk) begin
        if (s_fire && store)
            mem[wa[A-1:0]] <= {s_tlast, s_tdata};
    end

    always_ff @(posedge clk) begin
        if (!rstn) begin
            wr <= '0; wr_start <= '0; committed <= '0; sop <= 1'b1; long_q <= 1'b0; nb_q <= '0;
        end else if (s_fire) begin
            sop  <= s_tlast;
            nb_q <= sop ? NB'(1) : (nb_q == NB'(MAXB) ? nb_q : nb_q + 1'b1);
            if (s_tlast) begin
                long_q    <= 1'b0;
                wr        <= wa + 1'b1;
                wr_start  <= wa + 1'b1;
                committed <= wa + 1'b1;
            end else if (long_now) begin
                long_q <= 1'b1;
                wr     <= wr_start;              // free what was stored of it
            end else begin
                wr <= wr + 1'b1;
            end
        end
    end

    // read side: block RAM read (latency 1) into a 4-entry output FIFO
    logic         o_ready, o_valid, rd_pend, can_read;
    logic [512:0] o_data_in, o_data;
    logic [2:0]   o_cnt;
    assign can_read = (rd != committed) && (o_cnt + {2'd0, rd_pend} < 3'd3);
    always_ff @(posedge clk) begin
        if (!rstn) begin
            rd <= '0; rd_pend <= 1'b0;
        end else begin
            rd_pend <= can_read;
            if (can_read) rd <= rd + 1'b1;
        end
    end
    always_ff @(posedge clk) o_data_in <= mem[rd[A-1:0]];
    qnoc_fifo #(.W(513), .DEPTH(4)) u_out (
        .clk(clk), .rstn(rstn), .s_valid(rd_pend), .s_ready(), .s_data(o_data_in),
        .m_valid(o_valid), .m_ready(o_ready), .m_data(o_data));
    always_ff @(posedge clk) begin
        if (!rstn) o_cnt <= '0;
        else o_cnt <= o_cnt + (rd_pend ? 3'd1 : 3'd0) - ((o_valid && o_ready) ? 3'd1 : 3'd0);
    end

    // a packet's beats are read only once it is committed, with its entry already queued
    assign m_tvalid    = o_valid && p_out_valid;
    assign m_tdata     = o_data[511:0];
    assign m_tlast     = o_data[512];
    assign m_qid       = p_out[11:0];
    assign m_mty       = p_out[17:12];
    assign m_err       = p_out[18];
    assign m_zero_byte = p_out[19];
    assign o_ready     = m_tready && p_out_valid;
    assign p_out_ready = m_tready && o_valid && o_data[512];
endmodule
