// QDMA streaming over the NoC, static-shell side (V80 NIC static).
//
// Sits between qdma_st_pipe's RM-side ports (q_*, CPM clock, 390 MHz) and the
// NoC AXI4-Stream ports that cross into the service-layer RM. The NoC streams
// carry tdata, tlast and tkeep only (no TUSER), so every CPM sideband travels
// in-band. Formats (bit ranges of tdata; tkeep is always all ones):
//
//   Data ports h2c0/h2c1 (static -> RM), c2h0/c2h1 (RM -> static), 512 bits.
//   The beats of one stream alternate strictly between its two ports, one
//   counter across packets: beat k of the stream goes to port k mod 2. Each
//   port delivers in order, so the receiver merges by the same rule.
//
//   H2C packet: data beats (NoC tlast 0), then one trailer (NoC tlast 1):
//     [11:0] qid  [14:12] port_id  [15] err (OR over the packet)
//     [47:16] mdata  [53:48] mty  [54] zero_byte  [87:56] tcrc of the last
//     beat  [511:508] 4'hA
//   C2H packet: one header (NoC tlast 0), then data beats (NoC tlast 1 on the
//   last):
//     [15:0] len  [27:16] qid  [30:28] port_id  [31] marker  [32] has_cmpt
//     [511:508] 4'hC
//     The static derives mty from len, the packet CRC (tcrc) and the control
//     ECC (PG347 ECC input layout), and holds the control fields on all beats.
//
//   cmpt (RM -> static), 512 bits, one port. Fields F (45 bits):
//     [1:0] size  [4:2] col_idx  [7:5] err_idx  [20:8] qid  [22:21] cmpt_type
//     [38:23] wait_pld_pkt_id  [41:39] port_id  [42] marker  [43] user_trig
//     [44] no_wrb_marker
//     size 0-2 (8/16/32 B): one beat, tlast 1: [255:0] data, [300:256] F.
//     size 3 (64 B): two beats: data[511:0] (tlast 0), then F in [44:0]
//     (tlast 1). The static computes dpar (odd parity per 32 bits).
//
//   evt (static -> RM), 256 bits, one beat per cycle with any event, tlast 1:
//     [0] c2h_sts valid [1] drop [13:2] qid [14] last [15] status_cmp [16] error
//     [17] dsc_sts valid [18] byp [19] dir [20] mm [32:21] qid [48:33] avl
//          [64:49] pidx [67:65] port_id [68] qinv [69] irq_arm [70] error [71] qen
//     [72] qsts valid [80:73] op [144:81] data [147:145] port_id [160:148] qid
//     [161] irq ack  [162] irq fail
//     [163] overflow: events were lost (sticky until reset)
//     [255:252] 4'hE
//   The CPM status channels cannot be held, so events go through a FIFO; when
//   it is full they are dropped and the overflow bit is set.
//
//   cmd (RM -> static), 256 bits, tlast 1:
//     [0] crdt valid [1] dir [17:2] crdt [29:18] qid [30] fence
//     [31] irq valid [42:32] vec [55:43] fnc
//     [255:252] 4'hD
//   One irq request per cmd; the static drives the CPM handshake (valid until
//   ack or fail, then low for a cycle) and returns ack or fail as an event.
//
// clk = DMA clock, rstn = dma1_axi_aresetn (synchronous to clk).
`timescale 1ns / 1ps

module qdma_noc_static (
    input  wire         clk,
    input  wire         rstn,

    // ---- qdma_st_pipe RM side ----
    input  wire [511:0] q_h2c_tdata,
    input  wire [31:0]  q_h2c_tcrc,
    input  wire         q_h2c_tvalid,
    input  wire         q_h2c_tlast,
    output wire         q_h2c_tready,
    input  wire [11:0]  q_h2c_qid,
    input  wire [2:0]   q_h2c_port_id,
    input  wire         q_h2c_err,
    input  wire [31:0]  q_h2c_mdata,
    input  wire [5:0]   q_h2c_mty,
    input  wire         q_h2c_zero_byte,

    output wire [511:0] q_c2h_tdata,
    output wire [31:0]  q_c2h_tcrc,
    output wire         q_c2h_ctrl_marker,
    output wire [2:0]   q_c2h_ctrl_port_id,
    output wire [15:0]  q_c2h_ctrl_len,
    output wire [11:0]  q_c2h_ctrl_qid,
    output wire         q_c2h_ctrl_has_cmpt,
    output wire [5:0]   q_c2h_mty,
    output wire         q_c2h_tvalid,
    output wire         q_c2h_tlast,
    input  wire         q_c2h_tready,
    output wire [6:0]   q_c2h_ecc,

    output wire [511:0] q_cmpt_data,
    output wire [15:0]  q_cmpt_dpar,
    output wire         q_cmpt_tvalid,
    input  wire         q_cmpt_tready,
    output wire [1:0]   q_cmpt_size,
    output wire [2:0]   q_cmpt_col_idx,
    output wire [2:0]   q_cmpt_err_idx,
    output wire [12:0]  q_cmpt_qid,
    output wire [1:0]   q_cmpt_cmpt_type,
    output wire [15:0]  q_cmpt_wait_pld_pkt_id,
    output wire [2:0]   q_cmpt_port_id,
    output wire         q_cmpt_marker,
    output wire         q_cmpt_user_trig,
    output wire         q_cmpt_no_wrb_marker,

    input  wire         q_c2h_sts_valid,
    input  wire         q_c2h_sts_drop,
    input  wire [11:0]  q_c2h_sts_qid,
    input  wire         q_c2h_sts_last,
    input  wire         q_c2h_sts_status_cmp,
    input  wire         q_c2h_sts_error,

    output reg          q_irq_valid,
    output reg  [10:0]  q_irq_vec,
    output reg  [12:0]  q_irq_fnc,
    input  wire         q_irq_ack,
    input  wire         q_irq_fail,

    input  wire         q_dsc_sts_valid,
    output wire         q_dsc_sts_rdy,
    input  wire         q_dsc_sts_byp,
    input  wire         q_dsc_sts_dir,
    input  wire         q_dsc_sts_mm,
    input  wire [11:0]  q_dsc_sts_qid,
    input  wire [15:0]  q_dsc_sts_avl,
    input  wire [15:0]  q_dsc_sts_pidx,
    input  wire [2:0]   q_dsc_sts_port_id,
    input  wire         q_dsc_sts_qinv,
    input  wire         q_dsc_sts_irq_arm,
    input  wire         q_dsc_sts_error,
    input  wire         q_dsc_sts_qen,

    input  wire         q_qsts_vld,
    output wire         q_qsts_rdy,
    input  wire [7:0]   q_qsts_op,
    input  wire [63:0]  q_qsts_data,
    input  wire [2:0]   q_qsts_port_id,
    input  wire [12:0]  q_qsts_qid,

    output wire         q_crdt_valid,
    input  wire         q_crdt_rdy,
    output wire         q_crdt_dir,
    output wire [15:0]  q_crdt_crdt,
    output wire [11:0]  q_crdt_qid,
    output wire         q_crdt_fence,

    // ---- NoC streams ----
    output wire         m_h2c0_tvalid,
    input  wire         m_h2c0_tready,
    output wire [511:0] m_h2c0_tdata,
    output wire [63:0]  m_h2c0_tkeep,
    output wire         m_h2c0_tlast,
    output wire         m_h2c1_tvalid,
    input  wire         m_h2c1_tready,
    output wire [511:0] m_h2c1_tdata,
    output wire [63:0]  m_h2c1_tkeep,
    output wire         m_h2c1_tlast,
    output wire         m_evt_tvalid,
    input  wire         m_evt_tready,
    output wire [255:0] m_evt_tdata,
    output wire [31:0]  m_evt_tkeep,
    output wire         m_evt_tlast,

    input  wire         s_c2h0_tvalid,
    output wire         s_c2h0_tready,
    input  wire [511:0] s_c2h0_tdata,
    input  wire [63:0]  s_c2h0_tkeep,
    input  wire         s_c2h0_tlast,
    input  wire         s_c2h1_tvalid,
    output wire         s_c2h1_tready,
    input  wire [511:0] s_c2h1_tdata,
    input  wire [63:0]  s_c2h1_tkeep,
    input  wire         s_c2h1_tlast,
    input  wire         s_cmpt_tvalid,
    output wire         s_cmpt_tready,
    input  wire [511:0] s_cmpt_tdata,
    input  wire [63:0]  s_cmpt_tkeep,
    input  wire         s_cmpt_tlast,
    input  wire         s_cmd_tvalid,
    output wire         s_cmd_tready,
    input  wire [255:0] s_cmd_tdata,
    input  wire [31:0]  s_cmd_tkeep,
    input  wire         s_cmd_tlast,

    // sticky: [0] c2h framing error [1] cmpt framing error [2] event overflow
    // [3] cmd framing error
    output wire [3:0]   status
);
    assign q_dsc_sts_rdy = 1'b1;
    assign q_qsts_rdy    = 1'b1;
    assign m_h2c0_tkeep  = {64{1'b1}};
    assign m_h2c1_tkeep  = {64{1'b1}};
    assign m_evt_tkeep   = {32{1'b1}};
    assign m_evt_tlast   = 1'b1;

    // =====================================================================
    // H2C: frame (data beats + trailer) and split over h2c0/h2c1
    // =====================================================================
    reg  h_sel, h_err;
    wire hq0_ready, hq1_ready;
    assign q_h2c_tready = hq0_ready & hq1_ready;
    wire h_acc = q_h2c_tvalid & q_h2c_tready;
    wire [511:0] h_trailer = {4'hA, 420'd0, q_h2c_tcrc, 1'b0, q_h2c_zero_byte, q_h2c_mty,
                              q_h2c_mdata, h_err | q_h2c_err, q_h2c_port_id, q_h2c_qid};
    wire [512:0] h_dat = {1'b0, q_h2c_tdata};
    wire [512:0] h_trl = {1'b1, h_trailer};
    wire hq0_push = h_acc & (~h_sel | q_h2c_tlast);
    wire hq1_push = h_acc & ( h_sel | q_h2c_tlast);
    always @(posedge clk) begin
        if (!rstn) begin
            h_sel <= 1'b0;
            h_err <= 1'b0;
        end else if (h_acc) begin
            h_sel <= q_h2c_tlast ? h_sel : ~h_sel;
            h_err <= q_h2c_tlast ? 1'b0 : (h_err | q_h2c_err);
        end
    end
    wire         hf0_v, hf1_v, hs0_r, hs1_r;
    wire [512:0] hf0_d, hf1_d, hs0_d, hs1_d;
    qnoc_fifo #(.W(513), .DEPTH(4)) u_hq0 (.clk(clk), .rstn(rstn),
        .s_valid(hq0_push), .s_ready(hq0_ready), .s_data(h_sel ? h_trl : h_dat),
        .m_valid(hf0_v), .m_ready(hs0_r), .m_data(hf0_d));
    qnoc_fifo #(.W(513), .DEPTH(4)) u_hq1 (.clk(clk), .rstn(rstn),
        .s_valid(hq1_push), .s_ready(hq1_ready), .s_data(h_sel ? h_dat : h_trl),
        .m_valid(hf1_v), .m_ready(hs1_r), .m_data(hf1_d));
    qnoc_skid #(.W(513)) u_hs0 (.clk(clk), .rstn(rstn),
        .s_valid(hf0_v), .s_ready(hs0_r), .s_data(hf0_d),
        .m_valid(m_h2c0_tvalid), .m_ready(m_h2c0_tready), .m_data(hs0_d));
    qnoc_skid #(.W(513)) u_hs1 (.clk(clk), .rstn(rstn),
        .s_valid(hf1_v), .s_ready(hs1_r), .s_data(hf1_d),
        .m_valid(m_h2c1_tvalid), .m_ready(m_h2c1_tready), .m_data(hs1_d));
    assign {m_h2c0_tlast, m_h2c0_tdata} = hs0_d;
    assign {m_h2c1_tlast, m_h2c1_tdata} = hs1_d;

    // =====================================================================
    // C2H: merge c2h0/c2h1, strip the header, CRC + ECC pipeline
    // =====================================================================
    wire         c0_v, c1_v;
    wire [512:0] c0_d, c1_d;
    wire         c0_pop, c1_pop;
    qnoc_skid #(.W(513)) u_cs0 (.clk(clk), .rstn(rstn),
        .s_valid(s_c2h0_tvalid), .s_ready(s_c2h0_tready), .s_data({s_c2h0_tlast, s_c2h0_tdata}),
        .m_valid(c0_v), .m_ready(c0_pop), .m_data(c0_d));
    qnoc_skid #(.W(513)) u_cs1 (.clk(clk), .rstn(rstn),
        .s_valid(s_c2h1_tvalid), .s_ready(s_c2h1_tready), .s_data({s_c2h1_tlast, s_c2h1_tdata}),
        .m_valid(c1_v), .m_ready(c1_pop), .m_data(c1_d));

    reg  c_sel, c_hdr;                 // c_hdr: the next beat of the stream is a header
    reg  [32:0] c_fld;                 // {has_cmpt, marker, port_id, qid, len}
    reg  c_ferr;
    wire         cur_v = c_sel ? c1_v : c0_v;
    wire [512:0] cur_d = c_sel ? c1_d : c0_d;
    wire         nxt_v = c_sel ? c0_v : c1_v;
    wire [512:0] nxt_d = c_sel ? c0_d : c1_d;
    wire en;                           // pipeline advance
    wire c_take = c_hdr ? (cur_v & nxt_v) : cur_v;
    wire c_fire = c_take & en;
    wire [511:0] c_data = c_hdr ? nxt_d[511:0] : cur_d[511:0];
    wire         c_last = c_hdr ? nxt_d[512]   : cur_d[512];
    wire [32:0]  c_f    = c_hdr ? cur_d[32:0]  : c_fld;
    assign c0_pop = c_fire & (~c_sel | c_hdr);
    assign c1_pop = c_fire & ( c_sel | c_hdr);
    always @(posedge clk) begin
        if (!rstn) begin
            c_sel  <= 1'b0;
            c_hdr  <= 1'b1;
            c_ferr <= 1'b0;
        end else if (c_fire) begin
            c_sel <= c_hdr ? c_sel : ~c_sel;
            c_hdr <= c_last;
            if (c_hdr) begin
                c_fld <= cur_d[32:0];
                if (cur_d[512] || cur_d[511:508] != 4'hC)
                    c_ferr <= 1'b1;
            end
        end
    end

    // S1: mask the empty bytes of the last beat
    wire [5:0] c_mty = 6'd0 - c_f[5:0];
    wire [511:0] c_mask;
    genvar b;
    generate
        for (b = 0; b < 64; b = b + 1) begin : g_mask
            assign c_mask[8*b +: 8] = {8{~c_last | (b <= 63 - c_mty)}};
        end
    endgenerate
    reg         s1_v, s1_l, s1_sop;
    reg [511:0] s1_d;
    reg [32:0]  s1_f;
    reg [5:0]   s1_m;
    // S2: partial parities, ECC
    reg         s2_v, s2_l, s2_sop;
    reg [511:0] s2_d;
    reg [255:0] s2_p;
    reg [32:0]  s2_f;
    reg [5:0]   s2_m;
    reg [6:0]   s2_ecc;
    // S3: B*data
    reg         s3_v, s3_l, s3_sop;
    reg [511:0] s3_d;
    reg [31:0]  s3_b;
    reg [32:0]  s3_f;
    reg [5:0]   s3_m;
    reg [6:0]   s3_ecc;
    // S4: CRC state and output
    reg         s4_v, s4_l;
    reg [511:0] s4_d;
    reg [31:0]  s4_crc;
    reg [32:0]  s4_f;
    reg [5:0]   s4_m;
    reg [6:0]   s4_ecc;
    assign en = ~s4_v | q_c2h_tready;

    wire [255:0] p_w;
    wire [31:0]  a_w, sop_w;
    wire [6:0]   ecc_w;
    qnoc_crc512_dpart u_dpart (.d(s1_d), .p(p_w));
    qnoc_crc512_amul  u_amul  (.c(s4_crc), .a(a_w), .sop(sop_w));
    qnoc_ecc57 u_ecc (.d({24'd0, s1_f}), .c(ecc_w));
    wire [31:0] b_w;
    genvar j;
    generate
        for (j = 0; j < 32; j = j + 1) begin : g_b
            assign b_w[j] = ^s2_p[8*j +: 8];
        end
    endgenerate

    always @(posedge clk) begin
        if (!rstn) begin
            s1_v <= 1'b0; s2_v <= 1'b0; s3_v <= 1'b0; s4_v <= 1'b0;
        end else if (en) begin
            s1_v <= c_take; s2_v <= s1_v; s3_v <= s2_v; s4_v <= s3_v;
        end
    end
    always @(posedge clk) begin
        if (en) begin
            s1_d <= c_data & c_mask; s1_l <= c_last; s1_sop <= c_hdr; s1_f <= c_f; s1_m <= c_mty;
            s2_d <= s1_d; s2_p <= p_w; s2_l <= s1_l; s2_sop <= s1_sop; s2_f <= s1_f; s2_m <= s1_m; s2_ecc <= ecc_w;
            s3_d <= s2_d; s3_b <= b_w; s3_l <= s2_l; s3_sop <= s2_sop; s3_f <= s2_f; s3_m <= s2_m; s3_ecc <= s2_ecc;
            s4_d <= s3_d; s4_l <= s3_l; s4_f <= s3_f; s4_m <= s3_m; s4_ecc <= s3_ecc;
            if (s3_v)
                s4_crc <= (s3_sop ? sop_w : a_w) ^ s3_b;
        end
    end
    assign q_c2h_tdata         = s4_d;
    assign q_c2h_tcrc          = s4_crc;
    assign q_c2h_tvalid        = s4_v;
    assign q_c2h_tlast         = s4_l;
    assign q_c2h_mty           = s4_m;
    assign q_c2h_ecc           = s4_ecc;
    assign q_c2h_ctrl_len      = s4_f[15:0];
    assign q_c2h_ctrl_qid      = s4_f[27:16];
    assign q_c2h_ctrl_port_id  = s4_f[30:28];
    assign q_c2h_ctrl_marker   = s4_f[31];
    assign q_c2h_ctrl_has_cmpt = s4_f[32];

    // =====================================================================
    // CMPT: one or two beats per entry
    // =====================================================================
    wire         k_v;
    wire [512:0] k_d;
    wire         k_pop;
    qnoc_skid #(.W(513)) u_ks (.clk(clk), .rstn(rstn),
        .s_valid(s_cmpt_tvalid), .s_ready(s_cmpt_tready), .s_data({s_cmpt_tlast, s_cmpt_tdata}),
        .m_valid(k_v), .m_ready(k_pop), .m_data(k_d));
    reg          k_ph, k_ferr;
    reg  [511:0] k_hold;
    wire         ko_ready;
    wire         k_out  = k_v & (k_ph | k_d[512]) & ko_ready;     // an entry is complete
    assign k_pop = k_out | (k_v & ~k_ph & ~k_d[512]);
    wire [511:0] k_data = k_ph ? k_hold : {256'd0, k_d[255:0]};
    wire [44:0]  k_f    = k_ph ? k_d[44:0] : k_d[300:256];
    wire [15:0]  k_par;
    generate
        for (j = 0; j < 16; j = j + 1) begin : g_par
            assign k_par[j] = ~^k_data[32*j +: 32];
        end
    endgenerate
    always @(posedge clk) begin
        if (!rstn) begin
            k_ph   <= 1'b0;
            k_ferr <= 1'b0;
        end else if (k_pop) begin
            if (!k_ph && !k_d[512]) begin
                k_hold <= k_d[511:0];
                k_ph   <= 1'b1;
            end else begin
                k_ph <= 1'b0;
                if (k_ph && !k_d[512])
                    k_ferr <= 1'b1;
            end
        end
    end
    wire [572:0] ko_d;
    qnoc_skid #(.W(573)) u_ko (.clk(clk), .rstn(rstn),
        .s_valid(k_out), .s_ready(ko_ready), .s_data({k_f, k_par, k_data}),
        .m_valid(q_cmpt_tvalid), .m_ready(q_cmpt_tready), .m_data(ko_d));
    assign q_cmpt_data            = ko_d[511:0];
    assign q_cmpt_dpar            = ko_d[527:512];
    assign q_cmpt_size            = ko_d[529:528];
    assign q_cmpt_col_idx         = ko_d[532:530];
    assign q_cmpt_err_idx         = ko_d[535:533];
    assign q_cmpt_qid             = ko_d[548:536];
    assign q_cmpt_cmpt_type       = ko_d[550:549];
    assign q_cmpt_wait_pld_pkt_id = ko_d[566:551];
    assign q_cmpt_port_id         = ko_d[569:567];
    assign q_cmpt_marker          = ko_d[570];
    assign q_cmpt_user_trig       = ko_d[571];
    assign q_cmpt_no_wrb_marker   = ko_d[572];

    // =====================================================================
    // CMD: credits and interrupt requests
    // =====================================================================
    wire         m_v;
    wire [255:0] m_d;
    wire         cf_ready, if_ready, m_pop;
    reg          m_ferr;
    qnoc_skid #(.W(256)) u_ms (.clk(clk), .rstn(rstn),
        .s_valid(s_cmd_tvalid), .s_ready(s_cmd_tready), .s_data(s_cmd_tdata),
        .m_valid(m_v), .m_ready(m_pop), .m_data(m_d));
    assign m_pop = m_v & (~m_d[0] | cf_ready) & (~m_d[31] | if_ready);
    always @(posedge clk) begin
        if (!rstn)
            m_ferr <= 1'b0;
        else if (m_pop && m_d[255:252] != 4'hD)
            m_ferr <= 1'b1;
    end
    wire [29:0] cf_d;
    qnoc_fifo #(.W(30), .DEPTH(8)) u_cf (.clk(clk), .rstn(rstn),
        .s_valid(m_pop & m_d[0]), .s_ready(cf_ready), .s_data(m_d[30:1]),
        .m_valid(q_crdt_valid), .m_ready(q_crdt_rdy), .m_data(cf_d));
    assign {q_crdt_fence, q_crdt_qid, q_crdt_crdt, q_crdt_dir} = cf_d;

    wire        if_v;
    wire [23:0] if_d;
    reg  [1:0]  irq_st;
    reg         irq_ack_p, irq_fail_p;
    wire        if_pop = (irq_st == 2'd1) & (q_irq_ack | q_irq_fail);
    qnoc_fifo #(.W(24), .DEPTH(8)) u_if (.clk(clk), .rstn(rstn),
        .s_valid(m_pop & m_d[31]), .s_ready(if_ready), .s_data(m_d[55:32]),
        .m_valid(if_v), .m_ready(if_pop), .m_data(if_d));
    always @(posedge clk) begin
        if (!rstn) begin
            irq_st      <= 2'd0;
            q_irq_valid <= 1'b0;
            irq_ack_p   <= 1'b0;
            irq_fail_p  <= 1'b0;
        end else begin
            irq_ack_p  <= 1'b0;
            irq_fail_p <= 1'b0;
            case (irq_st)
                2'd0: if (if_v) begin
                    q_irq_valid <= 1'b1;
                    {q_irq_fnc, q_irq_vec} <= if_d;
                    irq_st <= 2'd1;
                end
                2'd1: if (q_irq_ack || q_irq_fail) begin
                    q_irq_valid <= 1'b0;
                    irq_ack_p   <= q_irq_ack;
                    irq_fail_p  <= q_irq_fail & ~q_irq_ack;
                    irq_st      <= 2'd2;
                end
                default: irq_st <= 2'd0;
            endcase
        end
    end

    // =====================================================================
    // EVT: CPM status channels and irq answers
    // =====================================================================
    reg         e_v, e_ovf;
    reg [162:0] e_d;
    always @(posedge clk) begin
        if (!rstn)
            e_v <= 1'b0;
        else
            e_v <= q_c2h_sts_valid | q_dsc_sts_valid | q_qsts_vld | irq_ack_p | irq_fail_p;
        e_d <= {irq_fail_p, irq_ack_p,
                q_qsts_qid, q_qsts_port_id, q_qsts_data, q_qsts_op, q_qsts_vld,
                q_dsc_sts_qen, q_dsc_sts_error, q_dsc_sts_irq_arm, q_dsc_sts_qinv,
                q_dsc_sts_port_id, q_dsc_sts_pidx, q_dsc_sts_avl, q_dsc_sts_qid,
                q_dsc_sts_mm, q_dsc_sts_dir, q_dsc_sts_byp, q_dsc_sts_valid,
                q_c2h_sts_error, q_c2h_sts_status_cmp, q_c2h_sts_last,
                q_c2h_sts_qid, q_c2h_sts_drop, q_c2h_sts_valid};
    end
    wire         ef_ready, ef_v, es_r;
    wire [163:0] ef_d, es_d;
    always @(posedge clk) begin
        if (!rstn)
            e_ovf <= 1'b0;
        else if (e_v && !ef_ready)
            e_ovf <= 1'b1;
    end
    qnoc_fifo #(.W(164), .DEPTH(32)) u_ef (.clk(clk), .rstn(rstn),
        .s_valid(e_v), .s_ready(ef_ready), .s_data({e_ovf, e_d}),
        .m_valid(ef_v), .m_ready(es_r), .m_data(ef_d));
    qnoc_skid #(.W(164)) u_es (.clk(clk), .rstn(rstn),
        .s_valid(ef_v), .s_ready(es_r), .s_data(ef_d),
        .m_valid(m_evt_tvalid), .m_ready(m_evt_tready), .m_data(es_d));
    assign m_evt_tdata = {4'hE, 88'd0, es_d};

    assign status = {m_ferr, e_ovf, k_ferr, c_ferr};
endmodule
