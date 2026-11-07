// End-to-end test of the QDMA-over-NoC path:
//   CPM model <-> qdma_noc_bridge (qdma_st_pipe + qdma_noc_static, DMA clock)
//             <-> NoC model (7 in-order channels, own latency/stalls/rate cap)
//             <-> qdma_noc_rm (RM clock) <-> user model.
// Checks h2c data and trailer fields, c2h data/ctrl/mty/tcrc/ecc (tcrc against
// a serial CRC, ecc against the CPM5 example design's encoder qdma_ecc_enc),
// cmpt entries and dpar, c2h_sts/dsc_sts/qsts events, the irq handshake and
// credits, and measures throughput.
//   +MODE=stress (default): random stalls everywhere, RM clock 300 MHz
//   +MODE=perf: no random stalls, RM clock 450 MHz, NoC ports capped at
//               16 GB/s, 4 KB and 64 B packets; reports beats per DMA cycle
`timescale 1ns / 1ps

module noc_ch #(parameter integer W = 512) (
    input  wire         clk_a,
    input  wire         rstn,
    input  wire         s_valid,
    output reg          s_ready,
    input  wire [W-1:0] s_data,
    input  wire         s_last,
    input  wire         clk_b,
    output reg          m_valid,
    input  wire         m_ready,
    output reg  [W-1:0] m_data,
    output reg          m_last
);
    real    lat_ns  = 80.0;     // transit time
    real    gap_ns  = 4.0;      // one beat per gap_ns on average (64 B at 16 GB/s)
    integer p_stall = 0;        // percent of cycles each side stalls at random
    typedef struct { logic [W-1:0] d; logic l; realtime t; } ent_t;
    ent_t    q[$];
    real     credit = 0.0;
    realtime prev_a = 0.0;
    real     dt;
    always @(posedge clk_a) begin
        if (!rstn) begin
            s_ready <= 1'b0;
            q.delete();
            credit = 0.0;
            prev_a = $realtime;
        end else begin
            dt     = $realtime - prev_a;
            prev_a = $realtime;
            credit = credit + dt / gap_ns;
            if (credit > 2.0) credit = 2.0;
            if (s_valid && s_ready) begin
                q.push_back('{s_data, s_last, $realtime});
                credit = credit - 1.0;
            end
            s_ready <= (q.size() < 48) && (credit + dt / gap_ns >= 1.0) && (($urandom % 100) >= p_stall);
        end
    end
    always @(posedge clk_b) begin
        if (!rstn) begin
            m_valid <= 1'b0;
        end else begin
            if (m_valid && m_ready)
                void'(q.pop_front());
            if (!m_valid || m_ready) begin
                if (q.size() > 0 && $realtime - q[0].t >= lat_ns && ($urandom % 100) >= p_stall) begin
                    m_valid <= 1'b1;
                    m_data  <= q[0].d;
                    m_last  <= q[0].l;
                end else begin
                    m_valid <= 1'b0;
                end
            end
        end
    end
endmodule

module tb_qdma_noc;
    localparam [31:0] POLY = 32'h04C11DB7;
    string  mode;
    bit     perf;
    integer n_pkt;
    integer p_st;               // percent stall of the CPM and user models
    real    rm_half;

    reg dma_clk = 0, rm_clk = 0, rstn = 0;
    always #1.282 dma_clk = ~dma_clk;          // 390 MHz
    initial begin
        if (!$value$plusargs("MODE=%s", mode)) mode = "stress";
        perf    = (mode == "perf");
        rm_half = perf ? 1.111 : 1.667;
        n_pkt   = perf ? 0 : 3000;
        p_st    = perf ? 0 : 30;
    end
    always #(rm_half) rm_clk = ~rm_clk;

    // ------------------------------------------------------------------
    // DUT wires
    // ------------------------------------------------------------------
    // CPM side
    reg  [511:0] cpm_h2c_tdata;  reg [31:0] cpm_h2c_tcrc;  reg cpm_h2c_tvalid = 0, cpm_h2c_tlast;
    wire         cpm_h2c_tready; reg [11:0] cpm_h2c_qid;   reg [2:0] cpm_h2c_port_id; reg cpm_h2c_err;
    reg  [31:0]  cpm_h2c_mdata;  reg [5:0]  cpm_h2c_mty;   reg cpm_h2c_zero_byte;
    wire [511:0] cpm_c2h_tdata;  wire [31:0] cpm_c2h_tcrc; wire cpm_c2h_ctrl_marker; wire [2:0] cpm_c2h_ctrl_port_id;
    wire [15:0]  cpm_c2h_ctrl_len; wire [11:0] cpm_c2h_ctrl_qid; wire cpm_c2h_ctrl_has_cmpt; wire [5:0] cpm_c2h_mty;
    wire         cpm_c2h_tvalid, cpm_c2h_tlast; reg cpm_c2h_tready = 0; wire [6:0] cpm_c2h_ecc;
    wire [511:0] cpm_cmpt_data;  wire [15:0] cpm_cmpt_dpar; wire cpm_cmpt_tvalid; reg cpm_cmpt_tready = 0;
    wire [1:0]   cpm_cmpt_size;  wire [2:0] cpm_cmpt_col_idx, cpm_cmpt_err_idx; wire [12:0] cpm_cmpt_qid;
    wire [1:0]   cpm_cmpt_cmpt_type; wire [15:0] cpm_cmpt_wait_pld_pkt_id; wire [2:0] cpm_cmpt_port_id;
    wire         cpm_cmpt_marker, cpm_cmpt_user_trig, cpm_cmpt_no_wrb_marker;
    reg          cpm_c2h_sts_valid = 0, cpm_c2h_sts_drop, cpm_c2h_sts_last, cpm_c2h_sts_status_cmp, cpm_c2h_sts_error;
    reg  [11:0]  cpm_c2h_sts_qid;
    wire         cpm_irq_valid; wire [10:0] cpm_irq_vec; wire [12:0] cpm_irq_fnc; reg cpm_irq_ack = 0, cpm_irq_fail = 0;
    reg          cpm_dsc_sts_valid = 0, cpm_dsc_sts_byp, cpm_dsc_sts_dir, cpm_dsc_sts_mm, cpm_dsc_sts_qinv,
                 cpm_dsc_sts_irq_arm, cpm_dsc_sts_error, cpm_dsc_sts_qen;
    wire         cpm_dsc_sts_rdy;
    reg  [11:0]  cpm_dsc_sts_qid; reg [15:0] cpm_dsc_sts_avl, cpm_dsc_sts_pidx; reg [2:0] cpm_dsc_sts_port_id;
    reg          cpm_qsts_vld = 0; wire cpm_qsts_rdy; reg [7:0] cpm_qsts_op; reg [63:0] cpm_qsts_data;
    reg  [2:0]   cpm_qsts_port_id; reg [12:0] cpm_qsts_qid;
    wire         cpm_crdt_valid; reg cpm_crdt_rdy = 0; wire cpm_crdt_dir; wire [15:0] cpm_crdt_crdt;
    wire [11:0]  cpm_crdt_qid; wire cpm_crdt_fence;
    wire [3:0]   st_status;
    // user side
    wire [511:0] u_h2c_tdata; wire u_h2c_tvalid, u_h2c_tlast; reg u_h2c_tready = 0;
    wire [11:0]  u_h2c_qid; wire [2:0] u_h2c_port_id; wire u_h2c_err; wire [31:0] u_h2c_mdata;
    wire [5:0]   u_h2c_mty; wire u_h2c_zero_byte; wire [31:0] u_h2c_tcrc;
    reg  [511:0] u_c2h_tdata; reg u_c2h_tvalid = 0, u_c2h_tlast; wire u_c2h_tready;
    reg  [15:0]  u_c2h_ctrl_len; reg [11:0] u_c2h_ctrl_qid; reg [2:0] u_c2h_ctrl_port_id;
    reg          u_c2h_ctrl_marker, u_c2h_ctrl_has_cmpt;
    reg  [511:0] u_cmpt_data; reg u_cmpt_tvalid = 0; wire u_cmpt_tready; reg [1:0] u_cmpt_size;
    reg  [2:0]   u_cmpt_col_idx, u_cmpt_err_idx; reg [12:0] u_cmpt_qid; reg [1:0] u_cmpt_cmpt_type;
    reg  [15:0]  u_cmpt_wait_pld_pkt_id; reg [2:0] u_cmpt_port_id; reg u_cmpt_marker, u_cmpt_user_trig, u_cmpt_no_wrb_marker;
    wire         u_c2h_sts_valid, u_c2h_sts_drop, u_c2h_sts_last, u_c2h_sts_status_cmp, u_c2h_sts_error;
    wire [11:0]  u_c2h_sts_qid;
    wire         u_dsc_sts_valid, u_dsc_sts_byp, u_dsc_sts_dir, u_dsc_sts_mm, u_dsc_sts_qinv, u_dsc_sts_irq_arm,
                 u_dsc_sts_error, u_dsc_sts_qen;
    wire [11:0]  u_dsc_sts_qid; wire [15:0] u_dsc_sts_avl, u_dsc_sts_pidx; wire [2:0] u_dsc_sts_port_id;
    wire         u_qsts_vld; wire [7:0] u_qsts_op; wire [63:0] u_qsts_data; wire [2:0] u_qsts_port_id; wire [12:0] u_qsts_qid;
    wire         u_evt_overflow;
    reg          u_irq_valid = 0; reg [10:0] u_irq_vec; reg [12:0] u_irq_fnc; wire u_irq_ack, u_irq_fail;
    reg          u_crdt_valid = 0; wire u_crdt_rdy; reg u_crdt_dir; reg [15:0] u_crdt_crdt; reg [11:0] u_crdt_qid; reg u_crdt_fence;
    wire [0:0]   rm_status;
    // NoC ports: static side (st_*) and RM side (rm_*)
    wire st_h2c0_v, st_h2c0_r, st_h2c0_l, st_h2c1_v, st_h2c1_r, st_h2c1_l, st_evt_v, st_evt_r, st_evt_l;
    wire [511:0] st_h2c0_d, st_h2c1_d; wire [255:0] st_evt_d;
    wire st_c2h0_v, st_c2h0_r, st_c2h0_l, st_c2h1_v, st_c2h1_r, st_c2h1_l, st_cmpt_v, st_cmpt_r, st_cmpt_l, st_cmd_v, st_cmd_r, st_cmd_l;
    wire [511:0] st_c2h0_d, st_c2h1_d, st_cmpt_d; wire [255:0] st_cmd_d;
    wire rm_h2c0_v, rm_h2c0_r, rm_h2c0_l, rm_h2c1_v, rm_h2c1_r, rm_h2c1_l, rm_evt_v, rm_evt_r, rm_evt_l;
    wire [511:0] rm_h2c0_d, rm_h2c1_d; wire [255:0] rm_evt_d;
    wire rm_c2h0_v, rm_c2h0_r, rm_c2h0_l, rm_c2h1_v, rm_c2h1_r, rm_c2h1_l, rm_cmpt_v, rm_cmpt_r, rm_cmpt_l, rm_cmd_v, rm_cmd_r, rm_cmd_l;
    wire [511:0] rm_c2h0_d, rm_c2h1_d, rm_cmpt_d; wire [255:0] rm_cmd_d;
    wire [63:0] k64; wire [31:0] k32;

    qdma_noc_bridge #(.STAGES(2)) dut_st (
        .clk(dma_clk), .rstn(rstn),
        .cpm_h2c_tdata(cpm_h2c_tdata), .cpm_h2c_tcrc(cpm_h2c_tcrc), .cpm_h2c_tvalid(cpm_h2c_tvalid),
        .cpm_h2c_tlast(cpm_h2c_tlast), .cpm_h2c_tready(cpm_h2c_tready), .cpm_h2c_qid(cpm_h2c_qid),
        .cpm_h2c_port_id(cpm_h2c_port_id), .cpm_h2c_err(cpm_h2c_err), .cpm_h2c_mdata(cpm_h2c_mdata),
        .cpm_h2c_mty(cpm_h2c_mty), .cpm_h2c_zero_byte(cpm_h2c_zero_byte),
        .cpm_c2h_tdata(cpm_c2h_tdata), .cpm_c2h_tcrc(cpm_c2h_tcrc), .cpm_c2h_ctrl_marker(cpm_c2h_ctrl_marker),
        .cpm_c2h_ctrl_port_id(cpm_c2h_ctrl_port_id), .cpm_c2h_ctrl_len(cpm_c2h_ctrl_len),
        .cpm_c2h_ctrl_qid(cpm_c2h_ctrl_qid), .cpm_c2h_ctrl_has_cmpt(cpm_c2h_ctrl_has_cmpt),
        .cpm_c2h_mty(cpm_c2h_mty), .cpm_c2h_tvalid(cpm_c2h_tvalid), .cpm_c2h_tlast(cpm_c2h_tlast),
        .cpm_c2h_tready(cpm_c2h_tready), .cpm_c2h_ecc(cpm_c2h_ecc),
        .cpm_cmpt_data(cpm_cmpt_data), .cpm_cmpt_dpar(cpm_cmpt_dpar), .cpm_cmpt_tvalid(cpm_cmpt_tvalid),
        .cpm_cmpt_tready(cpm_cmpt_tready), .cpm_cmpt_size(cpm_cmpt_size), .cpm_cmpt_col_idx(cpm_cmpt_col_idx),
        .cpm_cmpt_err_idx(cpm_cmpt_err_idx), .cpm_cmpt_qid(cpm_cmpt_qid), .cpm_cmpt_cmpt_type(cpm_cmpt_cmpt_type),
        .cpm_cmpt_wait_pld_pkt_id(cpm_cmpt_wait_pld_pkt_id), .cpm_cmpt_port_id(cpm_cmpt_port_id),
        .cpm_cmpt_marker(cpm_cmpt_marker), .cpm_cmpt_user_trig(cpm_cmpt_user_trig),
        .cpm_cmpt_no_wrb_marker(cpm_cmpt_no_wrb_marker),
        .cpm_c2h_sts_valid(cpm_c2h_sts_valid), .cpm_c2h_sts_drop(cpm_c2h_sts_drop), .cpm_c2h_sts_qid(cpm_c2h_sts_qid),
        .cpm_c2h_sts_last(cpm_c2h_sts_last), .cpm_c2h_sts_status_cmp(cpm_c2h_sts_status_cmp),
        .cpm_c2h_sts_error(cpm_c2h_sts_error),
        .cpm_irq_valid(cpm_irq_valid), .cpm_irq_vec(cpm_irq_vec), .cpm_irq_fnc(cpm_irq_fnc),
        .cpm_irq_ack(cpm_irq_ack), .cpm_irq_fail(cpm_irq_fail),
        .cpm_dsc_sts_valid(cpm_dsc_sts_valid), .cpm_dsc_sts_rdy(cpm_dsc_sts_rdy), .cpm_dsc_sts_byp(cpm_dsc_sts_byp),
        .cpm_dsc_sts_dir(cpm_dsc_sts_dir), .cpm_dsc_sts_mm(cpm_dsc_sts_mm), .cpm_dsc_sts_qid(cpm_dsc_sts_qid),
        .cpm_dsc_sts_avl(cpm_dsc_sts_avl), .cpm_dsc_sts_pidx(cpm_dsc_sts_pidx), .cpm_dsc_sts_port_id(cpm_dsc_sts_port_id),
        .cpm_dsc_sts_qinv(cpm_dsc_sts_qinv), .cpm_dsc_sts_irq_arm(cpm_dsc_sts_irq_arm),
        .cpm_dsc_sts_error(cpm_dsc_sts_error), .cpm_dsc_sts_qen(cpm_dsc_sts_qen),
        .cpm_qsts_vld(cpm_qsts_vld), .cpm_qsts_rdy(cpm_qsts_rdy), .cpm_qsts_op(cpm_qsts_op),
        .cpm_qsts_data(cpm_qsts_data), .cpm_qsts_port_id(cpm_qsts_port_id), .cpm_qsts_qid(cpm_qsts_qid),
        .cpm_crdt_valid(cpm_crdt_valid), .cpm_crdt_rdy(cpm_crdt_rdy), .cpm_crdt_dir(cpm_crdt_dir),
        .cpm_crdt_crdt(cpm_crdt_crdt), .cpm_crdt_qid(cpm_crdt_qid), .cpm_crdt_fence(cpm_crdt_fence),
        .m_h2c0_tvalid(st_h2c0_v), .m_h2c0_tready(st_h2c0_r), .m_h2c0_tdata(st_h2c0_d), .m_h2c0_tkeep(), .m_h2c0_tlast(st_h2c0_l),
        .m_h2c1_tvalid(st_h2c1_v), .m_h2c1_tready(st_h2c1_r), .m_h2c1_tdata(st_h2c1_d), .m_h2c1_tkeep(), .m_h2c1_tlast(st_h2c1_l),
        .m_evt_tvalid(st_evt_v), .m_evt_tready(st_evt_r), .m_evt_tdata(st_evt_d), .m_evt_tkeep(), .m_evt_tlast(st_evt_l),
        .s_c2h0_tvalid(st_c2h0_v), .s_c2h0_tready(st_c2h0_r), .s_c2h0_tdata(st_c2h0_d), .s_c2h0_tkeep(k64), .s_c2h0_tlast(st_c2h0_l),
        .s_c2h1_tvalid(st_c2h1_v), .s_c2h1_tready(st_c2h1_r), .s_c2h1_tdata(st_c2h1_d), .s_c2h1_tkeep(k64), .s_c2h1_tlast(st_c2h1_l),
        .s_cmpt_tvalid(st_cmpt_v), .s_cmpt_tready(st_cmpt_r), .s_cmpt_tdata(st_cmpt_d), .s_cmpt_tkeep(k64), .s_cmpt_tlast(st_cmpt_l),
        .s_cmd_tvalid(st_cmd_v), .s_cmd_tready(st_cmd_r), .s_cmd_tdata(st_cmd_d), .s_cmd_tkeep(k32), .s_cmd_tlast(st_cmd_l),
        .status(st_status));
    assign k64 = {64{1'b1}};
    assign k32 = {32{1'b1}};

    qdma_noc_rm dut_rm (
        .clk(rm_clk), .rstn(rstn),
        .u_h2c_tdata(u_h2c_tdata), .u_h2c_tvalid(u_h2c_tvalid), .u_h2c_tready(u_h2c_tready), .u_h2c_tlast(u_h2c_tlast),
        .u_h2c_qid(u_h2c_qid), .u_h2c_port_id(u_h2c_port_id), .u_h2c_err(u_h2c_err), .u_h2c_mdata(u_h2c_mdata),
        .u_h2c_mty(u_h2c_mty), .u_h2c_zero_byte(u_h2c_zero_byte), .u_h2c_tcrc(u_h2c_tcrc),
        .u_c2h_tdata(u_c2h_tdata), .u_c2h_tvalid(u_c2h_tvalid), .u_c2h_tready(u_c2h_tready), .u_c2h_tlast(u_c2h_tlast),
        .u_c2h_ctrl_len(u_c2h_ctrl_len), .u_c2h_ctrl_qid(u_c2h_ctrl_qid), .u_c2h_ctrl_port_id(u_c2h_ctrl_port_id),
        .u_c2h_ctrl_marker(u_c2h_ctrl_marker), .u_c2h_ctrl_has_cmpt(u_c2h_ctrl_has_cmpt),
        .u_cmpt_data(u_cmpt_data), .u_cmpt_tvalid(u_cmpt_tvalid), .u_cmpt_tready(u_cmpt_tready), .u_cmpt_size(u_cmpt_size),
        .u_cmpt_col_idx(u_cmpt_col_idx), .u_cmpt_err_idx(u_cmpt_err_idx), .u_cmpt_qid(u_cmpt_qid),
        .u_cmpt_cmpt_type(u_cmpt_cmpt_type), .u_cmpt_wait_pld_pkt_id(u_cmpt_wait_pld_pkt_id),
        .u_cmpt_port_id(u_cmpt_port_id), .u_cmpt_marker(u_cmpt_marker), .u_cmpt_user_trig(u_cmpt_user_trig),
        .u_cmpt_no_wrb_marker(u_cmpt_no_wrb_marker),
        .u_c2h_sts_valid(u_c2h_sts_valid), .u_c2h_sts_drop(u_c2h_sts_drop), .u_c2h_sts_qid(u_c2h_sts_qid),
        .u_c2h_sts_last(u_c2h_sts_last), .u_c2h_sts_status_cmp(u_c2h_sts_status_cmp), .u_c2h_sts_error(u_c2h_sts_error),
        .u_dsc_sts_valid(u_dsc_sts_valid), .u_dsc_sts_byp(u_dsc_sts_byp), .u_dsc_sts_dir(u_dsc_sts_dir),
        .u_dsc_sts_mm(u_dsc_sts_mm), .u_dsc_sts_qid(u_dsc_sts_qid), .u_dsc_sts_avl(u_dsc_sts_avl),
        .u_dsc_sts_pidx(u_dsc_sts_pidx), .u_dsc_sts_port_id(u_dsc_sts_port_id), .u_dsc_sts_qinv(u_dsc_sts_qinv),
        .u_dsc_sts_irq_arm(u_dsc_sts_irq_arm), .u_dsc_sts_error(u_dsc_sts_error), .u_dsc_sts_qen(u_dsc_sts_qen),
        .u_qsts_vld(u_qsts_vld), .u_qsts_op(u_qsts_op), .u_qsts_data(u_qsts_data), .u_qsts_port_id(u_qsts_port_id),
        .u_qsts_qid(u_qsts_qid), .u_evt_overflow(u_evt_overflow),
        .u_irq_valid(u_irq_valid), .u_irq_vec(u_irq_vec), .u_irq_fnc(u_irq_fnc), .u_irq_ack(u_irq_ack), .u_irq_fail(u_irq_fail),
        .u_crdt_valid(u_crdt_valid), .u_crdt_rdy(u_crdt_rdy), .u_crdt_dir(u_crdt_dir), .u_crdt_crdt(u_crdt_crdt),
        .u_crdt_qid(u_crdt_qid), .u_crdt_fence(u_crdt_fence),
        .s_h2c0_tvalid(rm_h2c0_v), .s_h2c0_tready(rm_h2c0_r), .s_h2c0_tdata(rm_h2c0_d), .s_h2c0_tkeep(k64), .s_h2c0_tlast(rm_h2c0_l),
        .s_h2c1_tvalid(rm_h2c1_v), .s_h2c1_tready(rm_h2c1_r), .s_h2c1_tdata(rm_h2c1_d), .s_h2c1_tkeep(k64), .s_h2c1_tlast(rm_h2c1_l),
        .s_evt_tvalid(rm_evt_v), .s_evt_tready(rm_evt_r), .s_evt_tdata(rm_evt_d), .s_evt_tkeep(k32), .s_evt_tlast(rm_evt_l),
        .m_c2h0_tvalid(rm_c2h0_v), .m_c2h0_tready(rm_c2h0_r), .m_c2h0_tdata(rm_c2h0_d), .m_c2h0_tkeep(), .m_c2h0_tlast(rm_c2h0_l),
        .m_c2h1_tvalid(rm_c2h1_v), .m_c2h1_tready(rm_c2h1_r), .m_c2h1_tdata(rm_c2h1_d), .m_c2h1_tkeep(), .m_c2h1_tlast(rm_c2h1_l),
        .m_cmpt_tvalid(rm_cmpt_v), .m_cmpt_tready(rm_cmpt_r), .m_cmpt_tdata(rm_cmpt_d), .m_cmpt_tkeep(), .m_cmpt_tlast(rm_cmpt_l),
        .m_cmd_tvalid(rm_cmd_v), .m_cmd_tready(rm_cmd_r), .m_cmd_tdata(rm_cmd_d), .m_cmd_tkeep(), .m_cmd_tlast(rm_cmd_l),
        .status(rm_status));

    // NoC channels
    noc_ch #(512) n_h2c0 (dma_clk, rstn, st_h2c0_v, st_h2c0_r, st_h2c0_d, st_h2c0_l, rm_clk, rm_h2c0_v, rm_h2c0_r, rm_h2c0_d, rm_h2c0_l);
    noc_ch #(512) n_h2c1 (dma_clk, rstn, st_h2c1_v, st_h2c1_r, st_h2c1_d, st_h2c1_l, rm_clk, rm_h2c1_v, rm_h2c1_r, rm_h2c1_d, rm_h2c1_l);
    noc_ch #(256) n_evt  (dma_clk, rstn, st_evt_v,  st_evt_r,  st_evt_d,  st_evt_l,  rm_clk, rm_evt_v,  rm_evt_r,  rm_evt_d,  rm_evt_l);
    noc_ch #(512) n_c2h0 (rm_clk, rstn, rm_c2h0_v, rm_c2h0_r, rm_c2h0_d, rm_c2h0_l, dma_clk, st_c2h0_v, st_c2h0_r, st_c2h0_d, st_c2h0_l);
    noc_ch #(512) n_c2h1 (rm_clk, rstn, rm_c2h1_v, rm_c2h1_r, rm_c2h1_d, rm_c2h1_l, dma_clk, st_c2h1_v, st_c2h1_r, st_c2h1_d, st_c2h1_l);
    noc_ch #(512) n_cmpt (rm_clk, rstn, rm_cmpt_v, rm_cmpt_r, rm_cmpt_d, rm_cmpt_l, dma_clk, st_cmpt_v, st_cmpt_r, st_cmpt_d, st_cmpt_l);
    noc_ch #(256) n_cmd  (rm_clk, rstn, rm_cmd_v,  rm_cmd_r,  rm_cmd_d,  rm_cmd_l,  dma_clk, st_cmd_v,  st_cmd_r,  st_cmd_d,  st_cmd_l);
    initial begin
        #1;
        if (!perf) begin
            n_h2c0.p_stall = 20; n_h2c1.p_stall = 20; n_evt.p_stall = 10; n_c2h0.p_stall = 20;
            n_c2h1.p_stall = 20; n_cmpt.p_stall = 20; n_cmd.p_stall = 20;
            n_h2c1.lat_ns = 140.0;  n_c2h0.lat_ns = 150.0;   // unequal port latency
        end
        n_evt.gap_ns = 2.0; n_cmd.gap_ns = 2.0;
    end

    // golden ECC: the encoder of the CPM5 QDMA example design
    wire [6:0]  ecc_ref;
    wire [56:0] ecc_dout;
    qdma_ecc_enc #(.C_DATA_WIDTH(57), .C_CHK_BIT_WIDTH(7), .C_REG_INPUT(0), .C_REG_OUTPUT(0), .C_PIPELINE(0), .C_USE_CLKEN(0))
        u_ecc_ref (.ecc_clk(1'b0), .ecc_reset(1'b0), .ecc_enc_clk_en_in(1'b1),
                   .ecc_enc_data_in({24'd0, cpm_c2h_ctrl_has_cmpt, cpm_c2h_ctrl_marker, cpm_c2h_ctrl_port_id,
                                     cpm_c2h_ctrl_qid, cpm_c2h_ctrl_len}),
                   .ecc_enc_data_out(ecc_dout), .ecc_enc_chk_bits_out(ecc_ref));

    // ------------------------------------------------------------------
    // scoreboard
    // ------------------------------------------------------------------
    integer errors = 0;
    task automatic fail(input string s);
        errors = errors + 1;
        if (errors <= 20) $display("[%0t] ERROR %s", $time, s);
    endtask
    function automatic [31:0] crc_beat(input [31:0] c, input [511:0] d);
        integer i; reg fb;
        for (i = 0; i < 512; i = i + 1) begin
            fb = c[31] ^ d[511 - i];
            c  = {c[30:0], 1'b0} ^ (fb ? POLY : 32'd0);
        end
        return c;
    endfunction
    function automatic [511:0] rnd512();
        reg [511:0] r; integer i;
        for (i = 0; i < 16; i = i + 1) r[32*i +: 32] = $urandom;
        return r;
    endfunction

    typedef struct { logic [511:0] d; logic last; logic [11:0] qid; logic [2:0] port; logic err;
                     logic [31:0] mdata; logic [5:0] mty; logic zb; logic [31:0] tcrc; } h2c_t;
    typedef struct { logic [511:0] d; logic last; logic [15:0] len; logic [11:0] qid; logic [2:0] port;
                     logic marker, has_cmpt; logic [5:0] mty; logic [31:0] crc; } c2h_t;
    typedef struct { logic [511:0] d; logic [44:0] f; } cmpt_t;
    h2c_t  h2c_q[$];
    c2h_t  c2h_q[$];
    cmpt_t cmpt_q[$];
    logic [16:0] csts_q[$];
    logic [54:0] dsts_q[$];
    logic [88:0] qsts_q[$];
    logic [23:0] irq_q[$];      // requests the user made, in order
    logic        irqr_q[$];     // CPM answers: 1 ack, 0 fail
    logic [29:0] crdt_q[$];
    integer n_h2c_pkt = 0, n_c2h_pkt = 0, cnt_cmpt = 0, n_csts = 0, n_dsts = 0, n_qsts = 0, n_irq = 0, n_crdt = 0;
    integer h2c_beats = 0, c2h_beats = 0, h2c_cyc = 0, c2h_cyc = 0;
    bit     gen_done = 0;
    integer perf_len = 4096;    // perf mode packet size in bytes

    // ---- CPM h2c source ----
    initial begin : h2c_src
        integer p, nb, k, errat;
        reg errp;
        reg [11:0] qid; reg [2:0] port; reg [31:0] md; reg zb; reg [5:0] mty;
        wait (rstn);
        repeat (20) @(posedge dma_clk);
        for (p = 0; perf ? 1 : (p < n_pkt); p = p + 1) begin
            if (perf && gen_done) break;
            zb   = !perf && ($urandom % 100 < 5);
            nb   = perf ? (perf_len + 63) / 64 : (zb ? 1 : 1 + ($urandom % (($urandom % 4 == 0) ? 40 : 4)));
            mty  = perf ? (64 - perf_len % 64) % 64 : (zb ? 6'd0 : $urandom);
            qid  = $urandom; port = $urandom; md = $urandom;
            errat = (!perf && $urandom % 100 < 10) ? $urandom % nb : -1;
            errp  = $urandom;
            for (k = 0; k < nb; k = k + 1) begin
                while (!perf && ($urandom % 100 < p_st)) begin
                    cpm_h2c_tvalid <= 1'b0;
                    @(posedge dma_clk);
                end
                cpm_h2c_tvalid    <= 1'b1;
                cpm_h2c_tdata     <= rnd512();
                cpm_h2c_tcrc      <= $urandom;
                cpm_h2c_tlast     <= (k == nb - 1);
                cpm_h2c_qid       <= qid;
                cpm_h2c_port_id   <= port;
                cpm_h2c_mdata     <= md;
                // PG347: err stays set to tlast; half the cases pulse it on one beat only
                cpm_h2c_err       <= (errat >= 0) && (errp ? (k == errat) : (k >= errat));
                cpm_h2c_mty       <= (k == nb - 1) ? mty : 6'd0;
                cpm_h2c_zero_byte <= zb;
                @(posedge dma_clk);
                while (!cpm_h2c_tready) @(posedge dma_clk);
                h2c_q.push_back('{cpm_h2c_tdata, cpm_h2c_tlast, qid, port, (errat >= 0), md, mty, zb, cpm_h2c_tcrc});
            end
        end
        cpm_h2c_tvalid <= 1'b0;
    end
    // perf: count accepted beats per DMA cycle while traffic runs
    always @(posedge dma_clk) if (rstn && cpm_h2c_tvalid) begin
        h2c_cyc = h2c_cyc + 1;
        if (cpm_h2c_tready) h2c_beats = h2c_beats + 1;
    end

    // ---- user h2c sink ----
    always @(posedge rm_clk) begin
        u_h2c_tready <= perf ? 1'b1 : (($urandom % 100) >= p_st);
        if (u_h2c_tvalid && u_h2c_tready) begin
            if (h2c_q.size() == 0) fail("h2c beat with nothing expected");
            else begin
                h2c_t e;
                e = h2c_q.pop_front();
                if (u_h2c_tdata !== e.d)      fail("h2c data");
                if (u_h2c_tlast !== e.last)   fail("h2c tlast");
                if (e.last) begin
                    n_h2c_pkt = n_h2c_pkt + 1;
                    if ({u_h2c_qid, u_h2c_port_id, u_h2c_err, u_h2c_mdata, u_h2c_mty, u_h2c_zero_byte, u_h2c_tcrc} !==
                        {e.qid, e.port, e.err, e.mdata, e.mty, e.zb, e.tcrc})
                        fail($sformatf("h2c trailer fields: got qid %h port %h err %b md %h mty %h zb %b crc %h, want %h %h %b %h %h %b %h",
                             u_h2c_qid, u_h2c_port_id, u_h2c_err, u_h2c_mdata, u_h2c_mty, u_h2c_zero_byte, u_h2c_tcrc,
                             e.qid, e.port, e.err, e.mdata, e.mty, e.zb, e.tcrc));
                end
            end
        end
    end

    // ---- user c2h source ----
    initial begin : c2h_src
        integer p, nb, k, len, b;
        reg [511:0] d, dm; reg [31:0] crc; reg [11:0] qid; reg [2:0] port; reg mk, hc; reg [5:0] mty;
        wait (rstn);
        repeat (25) @(posedge rm_clk);
        for (p = 0; perf ? 1 : (p < n_pkt); p = p + 1) begin
            if (perf && gen_done) break;
            len  = perf ? perf_len : (($urandom % 4 == 0) ? 1 + $urandom % 2600 : 1 + $urandom % 200);
            nb   = (len + 63) / 64;
            mty  = (64 - len % 64) % 64;
            qid  = $urandom; port = $urandom; mk = $urandom; hc = $urandom;
            crc  = 32'hFFFFFFFF;
            for (k = 0; k < nb; k = k + 1) begin
                while (!perf && ($urandom % 100 < p_st)) begin
                    u_c2h_tvalid <= 1'b0;
                    @(posedge rm_clk);
                end
                d  = rnd512();
                dm = d;
                if (k == nb - 1)
                    for (b = 0; b < 64; b = b + 1) if (b >= 64 - mty) dm[8*b +: 8] = 8'd0;
                crc = crc_beat(crc, dm);
                u_c2h_tvalid        <= 1'b1;
                u_c2h_tdata         <= d;
                u_c2h_tlast         <= (k == nb - 1);
                u_c2h_ctrl_len      <= (k == 0) ? len[15:0] : 16'hxxxx;
                u_c2h_ctrl_qid      <= (k == 0) ? qid : 12'hxxx;
                u_c2h_ctrl_port_id  <= (k == 0) ? port : 3'bxxx;
                u_c2h_ctrl_marker   <= (k == 0) ? mk : 1'bx;
                u_c2h_ctrl_has_cmpt <= (k == 0) ? hc : 1'bx;
                @(posedge rm_clk);
                while (!u_c2h_tready) @(posedge rm_clk);
                c2h_q.push_back('{dm, (k == nb - 1), len[15:0], qid, port, mk, hc, mty, crc});
            end
        end
        u_c2h_tvalid <= 1'b0;
    end

    // ---- CPM c2h sink ----
    always @(posedge dma_clk) begin
        cpm_c2h_tready <= perf ? 1'b1 : (($urandom % 100) >= p_st);
        if (cpm_c2h_tvalid && cpm_c2h_tready) begin
            c2h_beats = c2h_beats + 1;
            if (c2h_q.size() == 0) fail("c2h beat with nothing expected");
            else begin
                c2h_t e;
                e = c2h_q.pop_front();
                if (cpm_c2h_tdata !== e.d)    fail("c2h data");
                if (cpm_c2h_tlast !== e.last) fail("c2h tlast");
                if ({cpm_c2h_ctrl_len, cpm_c2h_ctrl_qid, cpm_c2h_ctrl_port_id, cpm_c2h_ctrl_marker, cpm_c2h_ctrl_has_cmpt} !==
                    {e.len, e.qid, e.port, e.marker, e.has_cmpt})
                    fail("c2h ctrl fields");
                if (cpm_c2h_ecc !== ecc_ref)  fail($sformatf("c2h ecc %h, reference %h", cpm_c2h_ecc, ecc_ref));
                if (e.last) begin
                    n_c2h_pkt = n_c2h_pkt + 1;
                    if (cpm_c2h_mty !== e.mty)  fail($sformatf("c2h mty %0d want %0d (len %0d)", cpm_c2h_mty, e.mty, e.len));
                    if (cpm_c2h_tcrc !== e.crc) fail($sformatf("c2h tcrc %h want %h (len %0d)", cpm_c2h_tcrc, e.crc, e.len));
                end
            end
        end
    end

    // ---- user cmpt source / CPM cmpt sink ----
    initial begin : cmpt_src
        integer p;
        reg [511:0] d; reg [44:0] f;
        wait (rstn);
        repeat (30) @(posedge rm_clk);
        for (p = 0; p < (perf ? 0 : n_pkt); p = p + 1) begin
            while ($urandom % 100 < 50) begin
                u_cmpt_tvalid <= 1'b0;
                @(posedge rm_clk);
            end
            d = rnd512();
            f = {$urandom, $urandom};
            u_cmpt_tvalid <= 1'b1;
            u_cmpt_data   <= d;
            {u_cmpt_no_wrb_marker, u_cmpt_user_trig, u_cmpt_marker, u_cmpt_port_id, u_cmpt_wait_pld_pkt_id,
             u_cmpt_cmpt_type, u_cmpt_qid, u_cmpt_err_idx, u_cmpt_col_idx, u_cmpt_size} <= f;
            @(posedge rm_clk);
            while (!u_cmpt_tready) @(posedge rm_clk);
            cmpt_q.push_back('{(f[1:0] == 2'd3) ? d : {256'd0, d[255:0]}, f});
        end
        u_cmpt_tvalid <= 1'b0;
    end
    always @(posedge dma_clk) begin
        cpm_cmpt_tready <= ($urandom % 100) >= p_st;
        if (cpm_cmpt_tvalid && cpm_cmpt_tready) begin
            if (cmpt_q.size() == 0) fail("cmpt with nothing expected");
            else begin
                cmpt_t e;
                integer i; reg [15:0] par;
                e = cmpt_q.pop_front();
                for (i = 0; i < 16; i = i + 1) par[i] = ~^e.d[32*i +: 32];
                cnt_cmpt = cnt_cmpt + 1;
                if (cpm_cmpt_data !== e.d) fail("cmpt data");
                if (cpm_cmpt_dpar !== par) fail("cmpt dpar");
                if ({cpm_cmpt_no_wrb_marker, cpm_cmpt_user_trig, cpm_cmpt_marker, cpm_cmpt_port_id, cpm_cmpt_wait_pld_pkt_id,
                     cpm_cmpt_cmpt_type, cpm_cmpt_qid, cpm_cmpt_err_idx, cpm_cmpt_col_idx, cpm_cmpt_size} !== e.f)
                    fail("cmpt fields");
            end
        end
    end

    // ---- CPM status channels (valid-only) / user checks ----
    integer ev_left;
    initial ev_left = 0;
    always @(posedge dma_clk) begin
        if (!rstn || perf) begin
            cpm_c2h_sts_valid <= 1'b0; cpm_dsc_sts_valid <= 1'b0; cpm_qsts_vld <= 1'b0;
        end else begin
            reg [16:0] c; reg [54:0] d; reg [88:0] q;
            cpm_c2h_sts_valid <= 1'b0; cpm_dsc_sts_valid <= 1'b0; cpm_qsts_vld <= 1'b0;
            if (ev_left > 0) begin
                if ($urandom % 100 < 8) begin
                    c = {$urandom, 1'b1};
                    cpm_c2h_sts_valid <= 1'b1;
                    {cpm_c2h_sts_error, cpm_c2h_sts_status_cmp, cpm_c2h_sts_last, cpm_c2h_sts_qid, cpm_c2h_sts_drop} <= c[16:1];
                    csts_q.push_back(c);
                    ev_left = ev_left - 1;
                end
                if ($urandom % 100 < 8) begin
                    d = {$urandom, $urandom, 1'b1};
                    cpm_dsc_sts_valid <= 1'b1;
                    {cpm_dsc_sts_qen, cpm_dsc_sts_error, cpm_dsc_sts_irq_arm, cpm_dsc_sts_qinv, cpm_dsc_sts_port_id,
                     cpm_dsc_sts_pidx, cpm_dsc_sts_avl, cpm_dsc_sts_qid, cpm_dsc_sts_mm, cpm_dsc_sts_dir, cpm_dsc_sts_byp} <= d[54:1];
                    dsts_q.push_back(d);
                end
                if ($urandom % 100 < 8) begin
                    q = {$urandom, $urandom, $urandom, 1'b1};
                    cpm_qsts_vld <= 1'b1;
                    {cpm_qsts_qid, cpm_qsts_port_id, cpm_qsts_data, cpm_qsts_op} <= q[88:1];
                    qsts_q.push_back(q);
                end
            end
        end
    end
    always @(posedge rm_clk) begin
        if (u_c2h_sts_valid) begin
            n_csts = n_csts + 1;
            if (csts_q.size() == 0) fail("c2h_sts not expected");
            else if ({u_c2h_sts_error, u_c2h_sts_status_cmp, u_c2h_sts_last, u_c2h_sts_qid, u_c2h_sts_drop, 1'b1} !== csts_q.pop_front())
                fail("c2h_sts fields");
        end
        if (u_dsc_sts_valid) begin
            n_dsts = n_dsts + 1;
            if (dsts_q.size() == 0) fail("dsc_sts not expected");
            else if ({u_dsc_sts_qen, u_dsc_sts_error, u_dsc_sts_irq_arm, u_dsc_sts_qinv, u_dsc_sts_port_id, u_dsc_sts_pidx,
                      u_dsc_sts_avl, u_dsc_sts_qid, u_dsc_sts_mm, u_dsc_sts_dir, u_dsc_sts_byp, 1'b1} !== dsts_q.pop_front())
                fail("dsc_sts fields");
        end
        if (u_qsts_vld) begin
            n_qsts = n_qsts + 1;
            if (qsts_q.size() == 0) fail("qsts not expected");
            else if ({u_qsts_qid, u_qsts_port_id, u_qsts_data, u_qsts_op, 1'b1} !== qsts_q.pop_front())
                fail("qsts fields");
        end
    end
    // the CPM side must never be told to wait on the status channels
    always @(posedge dma_clk) if (rstn && (cpm_dsc_sts_rdy !== 1'b1 || cpm_qsts_rdy !== 1'b1)) fail("status rdy low towards the CPM");

    // ---- irq: user requests, CPM answers ----
    initial begin : irq_user
        integer p;
        wait (rstn);
        repeat (40) @(posedge rm_clk);
        for (p = 0; p < (perf ? 0 : 200); p = p + 1) begin
            repeat ($urandom % 30) @(posedge rm_clk);
            u_irq_vec   <= $urandom;
            u_irq_fnc   <= $urandom;
            u_irq_valid <= 1'b1;
            @(posedge rm_clk);
            irq_q.push_back({u_irq_fnc, u_irq_vec});
            while (!(u_irq_ack || u_irq_fail)) @(posedge rm_clk);
            n_irq = n_irq + 1;
            if (irqr_q.size() == 0) fail("irq answer before the CPM gave one");
            else if (irqr_q.pop_front() !== u_irq_ack) fail("irq answer ack/fail mismatch");
            u_irq_valid <= 1'b0;
            @(posedge rm_clk);
        end
    end
    initial begin : irq_cpm
        reg ack;
        forever begin
            @(posedge dma_clk);
            if (rstn && cpm_irq_valid) begin
                repeat ($urandom % 12) begin
                    @(posedge dma_clk);
                    if (!cpm_irq_valid) fail("cpm irq valid dropped before ack/fail");
                end
                if (irq_q.size() == 0) fail("cpm irq with no request");
                else if ({cpm_irq_fnc, cpm_irq_vec} !== irq_q.pop_front()) fail("cpm irq vec/fnc");
                ack = ($urandom % 100) < 80;
                cpm_irq_ack  <= ack;
                cpm_irq_fail <= ~ack;
                irqr_q.push_back(ack);
                @(posedge dma_clk);
                cpm_irq_ack  <= 1'b0;
                cpm_irq_fail <= 1'b0;
                // valid must go low and stay low until the next request
                @(posedge dma_clk);
                while (cpm_irq_valid) begin
                    fail("cpm irq valid still high after ack/fail");
                    @(posedge dma_clk);
                end
            end
        end
    end

    // ---- credits ----
    initial begin : crdt_user
        integer p; reg [29:0] c;
        wait (rstn);
        repeat (35) @(posedge rm_clk);
        for (p = 0; p < (perf ? 0 : 1000); p = p + 1) begin
            while ($urandom % 100 < 40) begin u_crdt_valid <= 1'b0; @(posedge rm_clk); end
            c = $urandom;
            u_crdt_valid <= 1'b1;
            {u_crdt_fence, u_crdt_qid, u_crdt_crdt, u_crdt_dir} <= c;
            @(posedge rm_clk);
            while (!u_crdt_rdy) @(posedge rm_clk);
            crdt_q.push_back(c);
        end
        u_crdt_valid <= 1'b0;
    end
    always @(posedge dma_clk) begin
        cpm_crdt_rdy <= ($urandom % 100) >= p_st;
        if (cpm_crdt_valid && cpm_crdt_rdy) begin
            n_crdt = n_crdt + 1;
            if (crdt_q.size() == 0) fail("credit not expected");
            else if ({cpm_crdt_fence, cpm_crdt_qid, cpm_crdt_crdt, cpm_crdt_dir} !== crdt_q.pop_front()) fail("credit fields");
        end
    end

    // ------------------------------------------------------------------
    // run
    // ------------------------------------------------------------------

    initial begin
        integer t;
        rstn = 0;
        repeat (20) @(posedge dma_clk);
        rstn = 1;
        if (!perf) begin
            ev_left = 2000;
            t = 0;
            while (t < 4000000 && (n_h2c_pkt < n_pkt || n_c2h_pkt < n_pkt || cnt_cmpt < n_pkt || n_irq < 200 ||
                                   n_crdt < 1000 || ev_left > 0 || csts_q.size() || dsts_q.size() || qsts_q.size())) begin
                @(posedge dma_clk); t = t + 1;
            end
            repeat (2000) @(posedge dma_clk);
            if (h2c_q.size() || c2h_q.size() || cmpt_q.size() || irq_q.size() || crdt_q.size())
                fail($sformatf("left over: h2c %0d c2h %0d cmpt %0d irq %0d crdt %0d",
                               h2c_q.size(), c2h_q.size(), cmpt_q.size(), irq_q.size(), crdt_q.size()));
            if (st_status !== 4'd0) fail($sformatf("static status %b", st_status));
            if (rm_status !== 1'b0) fail("rm status");
            if (u_evt_overflow)     fail("event overflow");
            $display("STRESS h2c %0d pkts, c2h %0d pkts, cmpt %0d, c2h_sts %0d, dsc_sts %0d, qsts %0d, irq %0d, crdt %0d",
                     n_h2c_pkt, n_c2h_pkt, cnt_cmpt, n_csts, n_dsts, n_qsts, n_irq, n_crdt);
        end else begin
            // 4 KB packets, then 64 B packets; rates measured after the pipe fills
            repeat (3000) @(posedge dma_clk);
            h2c_beats = 0; h2c_cyc = 0; c2h_beats = 0;
            repeat (20000) @(posedge dma_clk);
            $display("PERF 4096 B: h2c %0.3f beats/DMA cycle (%0.1f GB/s), c2h %0.3f beats/DMA cycle (%0.1f GB/s)",
                     h2c_beats / 20000.0, h2c_beats / 20000.0 * 64 * 0.390,
                     c2h_beats / 20000.0, c2h_beats / 20000.0 * 64 * 0.390);
            perf_len = 64;
            repeat (3000) @(posedge dma_clk);
            h2c_beats = 0; c2h_beats = 0;
            repeat (20000) @(posedge dma_clk);
            $display("PERF 64 B: h2c %0.3f pkts/DMA cycle (%0.1f Mpps), c2h %0.3f pkts/DMA cycle (%0.1f Mpps)",
                     h2c_beats / 20000.0, h2c_beats / 20000.0 * 390,
                     c2h_beats / 20000.0, c2h_beats / 20000.0 * 390);
            gen_done = 1;
            repeat (3000) @(posedge dma_clk);
            if (st_status !== 4'd0) fail($sformatf("static status %b", st_status));
            if (rm_status !== 1'b0) fail("rm status");
        end
        if (errors == 0) $display("PASS (%s)", mode);
        else             $display("FAIL (%s): %0d errors", mode, errors);
        $finish;
    end
endmodule
