// QDMA streaming over the NoC, RM side: the counterpart of qdma_noc_static
// for logic in the service-layer RM (formats are documented there).
//
// Runs on any RM clock (the NoC crosses the clock domain). With two 512-bit
// ports per direction, the RM clock needs to be at least about 200 MHz for
// 25 GB/s.
//
// User interface (u_*):
//   h2c   stream out; qid, port_id, err, mdata, mty, zero_byte, tcrc are valid
//         on the tlast beat (they arrive in the trailer).
//   c2h   stream in; ctrl_len, ctrl_qid, ctrl_port_id, ctrl_marker,
//         ctrl_has_cmpt are taken on the first beat. ctrl_len must be at least
//         1 and match the bytes sent; the static derives mty, tcrc and the ECC.
//   cmpt  one entry per handshake, size 0-3 = 8/16/32/64 bytes (data[255:0]
//         for sizes 0-2); the static computes dpar.
//   c2h_sts, dsc_sts, qsts: one-cycle valid pulses; evt_overflow is sticky.
//   irq   raise valid with vec/fnc stable, keep it until ack or fail (one
//         cycle), then drop it for at least one cycle.
//   crdt  valid/rdy.
`timescale 1ns / 1ps

module qdma_noc_rm (
    input  wire         clk,
    input  wire         rstn,

    output wire [511:0] u_h2c_tdata,
    output wire         u_h2c_tvalid,
    input  wire         u_h2c_tready,
    output wire         u_h2c_tlast,
    output wire [11:0]  u_h2c_qid,
    output wire [2:0]   u_h2c_port_id,
    output wire         u_h2c_err,
    output wire [31:0]  u_h2c_mdata,
    output wire [5:0]   u_h2c_mty,
    output wire         u_h2c_zero_byte,
    output wire [31:0]  u_h2c_tcrc,

    input  wire [511:0] u_c2h_tdata,
    input  wire         u_c2h_tvalid,
    output wire         u_c2h_tready,
    input  wire         u_c2h_tlast,
    input  wire [15:0]  u_c2h_ctrl_len,
    input  wire [11:0]  u_c2h_ctrl_qid,
    input  wire [2:0]   u_c2h_ctrl_port_id,
    input  wire         u_c2h_ctrl_marker,
    input  wire         u_c2h_ctrl_has_cmpt,

    input  wire [511:0] u_cmpt_data,
    input  wire         u_cmpt_tvalid,
    output wire         u_cmpt_tready,
    input  wire [1:0]   u_cmpt_size,
    input  wire [2:0]   u_cmpt_col_idx,
    input  wire [2:0]   u_cmpt_err_idx,
    input  wire [12:0]  u_cmpt_qid,
    input  wire [1:0]   u_cmpt_cmpt_type,
    input  wire [15:0]  u_cmpt_wait_pld_pkt_id,
    input  wire [2:0]   u_cmpt_port_id,
    input  wire         u_cmpt_marker,
    input  wire         u_cmpt_user_trig,
    input  wire         u_cmpt_no_wrb_marker,

    output wire         u_c2h_sts_valid,
    output wire         u_c2h_sts_drop,
    output wire [11:0]  u_c2h_sts_qid,
    output wire         u_c2h_sts_last,
    output wire         u_c2h_sts_status_cmp,
    output wire         u_c2h_sts_error,

    output wire         u_dsc_sts_valid,
    output wire         u_dsc_sts_byp,
    output wire         u_dsc_sts_dir,
    output wire         u_dsc_sts_mm,
    output wire [11:0]  u_dsc_sts_qid,
    output wire [15:0]  u_dsc_sts_avl,
    output wire [15:0]  u_dsc_sts_pidx,
    output wire [2:0]   u_dsc_sts_port_id,
    output wire         u_dsc_sts_qinv,
    output wire         u_dsc_sts_irq_arm,
    output wire         u_dsc_sts_error,
    output wire         u_dsc_sts_qen,

    output wire         u_qsts_vld,
    output wire [7:0]   u_qsts_op,
    output wire [63:0]  u_qsts_data,
    output wire [2:0]   u_qsts_port_id,
    output wire [12:0]  u_qsts_qid,

    output wire         u_evt_overflow,

    input  wire         u_irq_valid,
    input  wire [10:0]  u_irq_vec,
    input  wire [12:0]  u_irq_fnc,
    output reg          u_irq_ack,
    output reg          u_irq_fail,

    input  wire         u_crdt_valid,
    output wire         u_crdt_rdy,
    input  wire         u_crdt_dir,
    input  wire [15:0]  u_crdt_crdt,
    input  wire [11:0]  u_crdt_qid,
    input  wire         u_crdt_fence,

    // ---- NoC streams ----
    input  wire         s_h2c0_tvalid,
    output wire         s_h2c0_tready,
    input  wire [511:0] s_h2c0_tdata,
    input  wire [63:0]  s_h2c0_tkeep,
    input  wire         s_h2c0_tlast,
    input  wire         s_h2c1_tvalid,
    output wire         s_h2c1_tready,
    input  wire [511:0] s_h2c1_tdata,
    input  wire [63:0]  s_h2c1_tkeep,
    input  wire         s_h2c1_tlast,
    input  wire         s_evt_tvalid,
    output wire         s_evt_tready,
    input  wire [255:0] s_evt_tdata,
    input  wire [31:0]  s_evt_tkeep,
    input  wire         s_evt_tlast,

    output wire         m_c2h0_tvalid,
    input  wire         m_c2h0_tready,
    output wire [511:0] m_c2h0_tdata,
    output wire [63:0]  m_c2h0_tkeep,
    output wire         m_c2h0_tlast,
    output wire         m_c2h1_tvalid,
    input  wire         m_c2h1_tready,
    output wire [511:0] m_c2h1_tdata,
    output wire [63:0]  m_c2h1_tkeep,
    output wire         m_c2h1_tlast,
    output wire         m_cmpt_tvalid,
    input  wire         m_cmpt_tready,
    output wire [511:0] m_cmpt_tdata,
    output wire [63:0]  m_cmpt_tkeep,
    output wire         m_cmpt_tlast,
    output wire         m_cmd_tvalid,
    input  wire         m_cmd_tready,
    output wire [255:0] m_cmd_tdata,
    output wire [31:0]  m_cmd_tkeep,
    output wire         m_cmd_tlast,

    // sticky: [0] h2c framing error
    output wire [0:0]   status
);
    assign m_c2h0_tkeep = {64{1'b1}};
    assign m_c2h1_tkeep = {64{1'b1}};
    assign m_cmpt_tkeep = {64{1'b1}};
    assign m_cmd_tkeep  = {32{1'b1}};
    assign m_cmd_tlast  = 1'b1;

    // =====================================================================
    // H2C: merge h2c0/h2c1; a data beat leaves once the beat after it is
    // known, so the last one carries the trailer fields
    // =====================================================================
    wire         h0_v, h1_v;
    wire [512:0] h0_d, h1_d;
    wire         h0_pop, h1_pop;
    qnoc_skid #(.W(513)) u_hs0 (.clk(clk), .rstn(rstn),
        .s_valid(s_h2c0_tvalid), .s_ready(s_h2c0_tready), .s_data({s_h2c0_tlast, s_h2c0_tdata}),
        .m_valid(h0_v), .m_ready(h0_pop), .m_data(h0_d));
    qnoc_skid #(.W(513)) u_hs1 (.clk(clk), .rstn(rstn),
        .s_valid(s_h2c1_tvalid), .s_ready(s_h2c1_tready), .s_data({s_h2c1_tlast, s_h2c1_tdata}),
        .m_valid(h1_v), .m_ready(h1_pop), .m_data(h1_d));
    reg          h_sel, h_ferr;
    wire         cur_v = h_sel ? h1_v : h0_v;
    wire [512:0] cur_d = h_sel ? h1_d : h0_d;
    wire         nxt_v = h_sel ? h0_v : h1_v;
    wire [512:0] nxt_d = h_sel ? h0_d : h1_d;
    wire         ho_ready;
    wire         h_stray = cur_v & cur_d[512];          // a trailer where data belongs
    wire         h_out   = cur_v & ~cur_d[512] & nxt_v & ho_ready;
    wire         h_end   = nxt_d[512];
    assign h0_pop = (h_out & (~h_sel | h_end)) | (h_stray & ~h_sel);
    assign h1_pop = (h_out & ( h_sel | h_end)) | (h_stray &  h_sel);
    always @(posedge clk) begin
        if (!rstn) begin
            h_sel  <= 1'b0;
            h_ferr <= 1'b0;
        end else begin
            if (h_out && !h_end)
                h_sel <= ~h_sel;
            if (h_stray) begin
                h_sel  <= ~h_sel;
                h_ferr <= 1'b1;
            end
            if (h_out && h_end && nxt_d[511:508] != 4'hA)
                h_ferr <= 1'b1;
        end
    end
    // {tcrc, zero_byte, mty, mdata, err, port_id, qid} = trailer[87:56], [54:0]
    wire [86:0] h_meta = h_end ? {nxt_d[87:56], nxt_d[54:0]} : 87'd0;
    wire [599:0] ho_d;
    qnoc_skid #(.W(600)) u_ho (.clk(clk), .rstn(rstn),
        .s_valid(h_out), .s_ready(ho_ready), .s_data({h_meta, h_end, cur_d[511:0]}),
        .m_valid(u_h2c_tvalid), .m_ready(u_h2c_tready), .m_data(ho_d));
    assign u_h2c_tdata     = ho_d[511:0];
    assign u_h2c_tlast     = ho_d[512];
    assign u_h2c_qid       = ho_d[524:513];
    assign u_h2c_port_id   = ho_d[527:525];
    assign u_h2c_err       = ho_d[528];
    assign u_h2c_mdata     = ho_d[560:529];
    assign u_h2c_mty       = ho_d[566:561];
    assign u_h2c_zero_byte = ho_d[567];
    assign u_h2c_tcrc      = ho_d[599:568];

    // =====================================================================
    // C2H: header + data, split over c2h0/c2h1
    // =====================================================================
    reg  c_sel, c_first;
    wire cq0_ready, cq1_ready;
    assign u_c2h_tready = cq0_ready & cq1_ready;
    wire c_acc = u_c2h_tvalid & u_c2h_tready;
    wire [512:0] c_hdr = {1'b0, 4'hC, 475'd0, u_c2h_ctrl_has_cmpt, u_c2h_ctrl_marker,
                          u_c2h_ctrl_port_id, u_c2h_ctrl_qid, u_c2h_ctrl_len};
    wire [512:0] c_dat = {u_c2h_tlast, u_c2h_tdata};
    // this beat goes to port c_sel; on a first beat the header takes c_sel
    // and the data beat the other port
    wire cq0_push = c_acc & (~c_sel | c_first);
    wire cq1_push = c_acc & ( c_sel | c_first);
    wire [512:0] cq0_in = c_first ? (c_sel ? c_dat : c_hdr) : c_dat;
    wire [512:0] cq1_in = c_first ? (c_sel ? c_hdr : c_dat) : c_dat;
    always @(posedge clk) begin
        if (!rstn) begin
            c_sel   <= 1'b0;
            c_first <= 1'b1;
        end else if (c_acc) begin
            c_sel   <= c_first ? c_sel : ~c_sel;
            c_first <= u_c2h_tlast;
        end
    end
    wire         cf0_v, cf1_v, cs0_r, cs1_r;
    wire [512:0] cf0_d, cf1_d, cs0_d, cs1_d;
    qnoc_fifo #(.W(513), .DEPTH(4)) u_cq0 (.clk(clk), .rstn(rstn),
        .s_valid(cq0_push), .s_ready(cq0_ready), .s_data(cq0_in),
        .m_valid(cf0_v), .m_ready(cs0_r), .m_data(cf0_d));
    qnoc_fifo #(.W(513), .DEPTH(4)) u_cq1 (.clk(clk), .rstn(rstn),
        .s_valid(cq1_push), .s_ready(cq1_ready), .s_data(cq1_in),
        .m_valid(cf1_v), .m_ready(cs1_r), .m_data(cf1_d));
    qnoc_skid #(.W(513)) u_cs0 (.clk(clk), .rstn(rstn),
        .s_valid(cf0_v), .s_ready(cs0_r), .s_data(cf0_d),
        .m_valid(m_c2h0_tvalid), .m_ready(m_c2h0_tready), .m_data(cs0_d));
    qnoc_skid #(.W(513)) u_cs1 (.clk(clk), .rstn(rstn),
        .s_valid(cf1_v), .s_ready(cs1_r), .s_data(cf1_d),
        .m_valid(m_c2h1_tvalid), .m_ready(m_c2h1_tready), .m_data(cs1_d));
    assign {m_c2h0_tlast, m_c2h0_tdata} = cs0_d;
    assign {m_c2h1_tlast, m_c2h1_tdata} = cs1_d;

    // =====================================================================
    // CMPT: one beat (8-32 B) or two (64 B)
    // =====================================================================
    reg          k_hv, k_ph;
    reg  [511:0] k_hd;
    reg  [44:0]  k_hf;
    wire         ks_ready;
    wire         k_long = (k_hf[1:0] == 2'd3);
    wire         k_emit = k_hv & ks_ready;
    wire         k_done = k_emit & (~k_long | k_ph);
    assign u_cmpt_tready = ~k_hv | k_done;
    wire [44:0] u_f = {u_cmpt_no_wrb_marker, u_cmpt_user_trig, u_cmpt_marker, u_cmpt_port_id,
                       u_cmpt_wait_pld_pkt_id, u_cmpt_cmpt_type, u_cmpt_qid, u_cmpt_err_idx,
                       u_cmpt_col_idx, u_cmpt_size};
    always @(posedge clk) begin
        if (!rstn) begin
            k_hv <= 1'b0;
            k_ph <= 1'b0;
        end else begin
            if (k_emit)
                k_ph <= k_long & ~k_ph;
            if (u_cmpt_tvalid && u_cmpt_tready) begin
                k_hv <= 1'b1;
                k_hd <= u_cmpt_data;
                k_hf <= u_f;
            end else if (k_done) begin
                k_hv <= 1'b0;
            end
        end
    end
    wire [512:0] k_beat = !k_long ? {1'b1, 4'h0, 207'd0, k_hf, k_hd[255:0]}
                        : k_ph    ? {1'b1, 467'd0, k_hf}
                        :           {1'b0, k_hd};
    wire [512:0] ks_d;
    qnoc_skid #(.W(513)) u_ks (.clk(clk), .rstn(rstn),
        .s_valid(k_emit), .s_ready(ks_ready), .s_data(k_beat),
        .m_valid(m_cmpt_tvalid), .m_ready(m_cmpt_tready), .m_data(ks_d));
    assign {m_cmpt_tlast, m_cmpt_tdata} = ks_d;

    // =====================================================================
    // EVT
    // =====================================================================
    reg         e_v;
    reg [163:0] e_d;
    reg         e_ovf;
    assign s_evt_tready = 1'b1;
    always @(posedge clk) begin
        if (!rstn) begin
            e_v   <= 1'b0;
            e_ovf <= 1'b0;
        end else begin
            e_v <= s_evt_tvalid;
            if (s_evt_tvalid && s_evt_tdata[163])
                e_ovf <= 1'b1;
        end
        e_d <= s_evt_tdata[163:0];
    end
    assign u_c2h_sts_valid      = e_v & e_d[0];
    assign u_c2h_sts_drop       = e_d[1];
    assign u_c2h_sts_qid        = e_d[13:2];
    assign u_c2h_sts_last       = e_d[14];
    assign u_c2h_sts_status_cmp = e_d[15];
    assign u_c2h_sts_error      = e_d[16];
    assign u_dsc_sts_valid      = e_v & e_d[17];
    assign u_dsc_sts_byp        = e_d[18];
    assign u_dsc_sts_dir        = e_d[19];
    assign u_dsc_sts_mm         = e_d[20];
    assign u_dsc_sts_qid        = e_d[32:21];
    assign u_dsc_sts_avl        = e_d[48:33];
    assign u_dsc_sts_pidx       = e_d[64:49];
    assign u_dsc_sts_port_id    = e_d[67:65];
    assign u_dsc_sts_qinv       = e_d[68];
    assign u_dsc_sts_irq_arm    = e_d[69];
    assign u_dsc_sts_error      = e_d[70];
    assign u_dsc_sts_qen        = e_d[71];
    assign u_qsts_vld           = e_v & e_d[72];
    assign u_qsts_op            = e_d[80:73];
    assign u_qsts_data          = e_d[144:81];
    assign u_qsts_port_id       = e_d[147:145];
    assign u_qsts_qid           = e_d[160:148];
    assign u_evt_overflow       = e_ovf;
    wire   e_ack  = e_v & e_d[161];
    wire   e_fail = e_v & e_d[162];

    // =====================================================================
    // CMD: credits and interrupt requests
    // =====================================================================
    reg  [1:0] i_st;                   // 0 idle, 1 request pending, 2 waiting for the answer, 3 waiting for valid low
    wire       ms_ready;
    wire       i_req  = (i_st == 2'd1);
    wire       m_send = (u_crdt_valid | i_req) & ms_ready;
    assign u_crdt_rdy = ms_ready;
    always @(posedge clk) begin
        if (!rstn) begin
            i_st       <= 2'd0;
            u_irq_ack  <= 1'b0;
            u_irq_fail <= 1'b0;
        end else begin
            u_irq_ack  <= 1'b0;
            u_irq_fail <= 1'b0;
            case (i_st)
                2'd0: if (u_irq_valid) i_st <= 2'd1;
                2'd1: if (m_send) i_st <= 2'd2;
                2'd2: if (e_ack || e_fail) begin
                    u_irq_ack  <= e_ack;
                    u_irq_fail <= e_fail & ~e_ack;
                    i_st       <= 2'd3;
                end
                default: if (!u_irq_valid) i_st <= 2'd0;
            endcase
        end
    end
    wire [255:0] m_beat = {4'hD, 196'd0, u_irq_fnc, u_irq_vec, i_req,
                           u_crdt_fence, u_crdt_qid, u_crdt_crdt, u_crdt_dir, u_crdt_valid};
    qnoc_skid #(.W(256)) u_ms (.clk(clk), .rstn(rstn),
        .s_valid(m_send), .s_ready(ms_ready), .s_data(m_beat),
        .m_valid(m_cmd_tvalid), .m_ready(m_cmd_tready), .m_data(m_cmd_tdata));

    assign status = h_ferr;
endmodule
