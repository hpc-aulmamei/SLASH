// Block-design wrapper of the nictest RM's QDMA streaming test: qdma_noc_rm
// (the RM end of the QDMA NoC streams, shell/rtl) and qnoc_lb (sink, loopback,
// generator; registers on s_axil). The NoC stream interfaces carry the names
// of shell/rtl/qdma_noc_rm_stub.v, so gen_service_layer.py wires either one.
// Everything runs on clk (the RM's service_clk, 250 MHz; the NoC crosses to
// the static's 390 MHz DMA clock). rstn is registered here three times, with
// replication, so no RM path starts at a reset flop of the static.
`timescale 1ns / 1ps
module qnoc_lb_bd (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_h2c0:s_h2c1:s_evt:m_c2h0:m_c2h1:m_cmpt:m_cmd:s_axil, ASSOCIATED_RESET rstn" *)
    input  wire         clk,
    (* X_INTERFACE_MODE = "slave" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME s_h2c0, TDATA_NUM_BYTES 64, HAS_TKEEP 1, HAS_TLAST 1, HAS_TREADY 1" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_h2c0 TVALID" *) input  wire         s_h2c0_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_h2c0 TREADY" *) output wire         s_h2c0_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_h2c0 TDATA" *)  input  wire [511:0] s_h2c0_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_h2c0 TKEEP" *)  input  wire [63:0]  s_h2c0_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_h2c0 TLAST" *)  input  wire         s_h2c0_tlast,
    (* X_INTERFACE_MODE = "slave" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME s_h2c1, TDATA_NUM_BYTES 64, HAS_TKEEP 1, HAS_TLAST 1, HAS_TREADY 1" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_h2c1 TVALID" *) input  wire         s_h2c1_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_h2c1 TREADY" *) output wire         s_h2c1_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_h2c1 TDATA" *)  input  wire [511:0] s_h2c1_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_h2c1 TKEEP" *)  input  wire [63:0]  s_h2c1_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_h2c1 TLAST" *)  input  wire         s_h2c1_tlast,
    (* X_INTERFACE_MODE = "slave" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME s_evt, TDATA_NUM_BYTES 32, HAS_TKEEP 1, HAS_TLAST 1, HAS_TREADY 1" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_evt TVALID" *) input  wire         s_evt_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_evt TREADY" *) output wire         s_evt_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_evt TDATA" *)  input  wire [255:0] s_evt_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_evt TKEEP" *)  input  wire [31:0]  s_evt_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_evt TLAST" *)  input  wire         s_evt_tlast,
    (* X_INTERFACE_MODE = "master" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME m_c2h0, TDATA_NUM_BYTES 64, HAS_TKEEP 1, HAS_TLAST 1, HAS_TREADY 1" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_c2h0 TVALID" *) output wire         m_c2h0_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_c2h0 TREADY" *) input  wire         m_c2h0_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_c2h0 TDATA" *)  output wire [511:0] m_c2h0_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_c2h0 TKEEP" *)  output wire [63:0]  m_c2h0_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_c2h0 TLAST" *)  output wire         m_c2h0_tlast,
    (* X_INTERFACE_MODE = "master" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME m_c2h1, TDATA_NUM_BYTES 64, HAS_TKEEP 1, HAS_TLAST 1, HAS_TREADY 1" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_c2h1 TVALID" *) output wire         m_c2h1_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_c2h1 TREADY" *) input  wire         m_c2h1_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_c2h1 TDATA" *)  output wire [511:0] m_c2h1_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_c2h1 TKEEP" *)  output wire [63:0]  m_c2h1_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_c2h1 TLAST" *)  output wire         m_c2h1_tlast,
    (* X_INTERFACE_MODE = "master" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME m_cmpt, TDATA_NUM_BYTES 64, HAS_TKEEP 1, HAS_TLAST 1, HAS_TREADY 1" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_cmpt TVALID" *) output wire         m_cmpt_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_cmpt TREADY" *) input  wire         m_cmpt_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_cmpt TDATA" *)  output wire [511:0] m_cmpt_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_cmpt TKEEP" *)  output wire [63:0]  m_cmpt_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_cmpt TLAST" *)  output wire         m_cmpt_tlast,
    (* X_INTERFACE_MODE = "master" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME m_cmd, TDATA_NUM_BYTES 32, HAS_TKEEP 1, HAS_TLAST 1, HAS_TREADY 1" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_cmd TVALID" *) output wire         m_cmd_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_cmd TREADY" *) input  wire         m_cmd_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_cmd TDATA" *)  output wire [255:0] m_cmd_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_cmd TKEEP" *)  output wire [31:0]  m_cmd_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_cmd TLAST" *)  output wire         m_cmd_tlast,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 rstn RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire         rstn,
    input  wire [15:0]  s_axil_awaddr,
    input  wire         s_axil_awvalid,
    output wire         s_axil_awready,
    input  wire [31:0]  s_axil_wdata,
    input  wire [3:0]   s_axil_wstrb,
    input  wire         s_axil_wvalid,
    output wire         s_axil_wready,
    output wire [1:0]   s_axil_bresp,
    output wire         s_axil_bvalid,
    input  wire         s_axil_bready,
    input  wire [15:0]  s_axil_araddr,
    input  wire         s_axil_arvalid,
    output wire         s_axil_arready,
    output wire [31:0]  s_axil_rdata,
    output wire [1:0]   s_axil_rresp,
    output wire         s_axil_rvalid,
    input  wire         s_axil_rready

);
    (* max_fanout = 200 *) reg [2:0] rst_pipe = 3'b000;
    always @(posedge clk) rst_pipe <= {rst_pipe[1:0], rstn};
    wire rst_n = rst_pipe[2];

    // qdma_noc_rm <-> qnoc_lb
    wire [511:0] h2c_tdata;  wire h2c_tvalid, h2c_tready, h2c_tlast, h2c_err, h2c_zero_byte;
    wire [11:0]  h2c_qid;    wire [2:0] h2c_port_id;  wire [31:0] h2c_mdata, h2c_tcrc;  wire [5:0] h2c_mty;
    wire [511:0] c2h_tdata;  wire c2h_tvalid, c2h_tready, c2h_tlast, c2h_has_cmpt;
    wire [15:0]  c2h_len;    wire [11:0] c2h_qid;
    wire [511:0] cmpt_data;  wire cmpt_tvalid, cmpt_tready;  wire [1:0] cmpt_size;
    wire [12:0]  cmpt_qid;   wire [15:0] cmpt_pkt_id;
    wire sts_valid, sts_drop, sts_last, sts_cmp, sts_error;  wire [11:0] sts_qid;
    wire dsc_valid, dsc_byp, dsc_dir, dsc_mm, dsc_qinv, dsc_irq_arm, dsc_error, dsc_qen;
    wire [11:0] dsc_qid;  wire [15:0] dsc_avl, dsc_pidx;  wire [2:0] dsc_port_id;
    wire qsts_vld;  wire [7:0] qsts_op;  wire [63:0] qsts_data;  wire [2:0] qsts_port_id;  wire [12:0] qsts_qid;
    wire evt_overflow;  wire irq_ack, irq_fail, crdt_rdy;  wire [0:0] noc_status;

    qdma_noc_rm u_noc (
        .clk(clk), .rstn(rst_n),
        .u_h2c_tdata(h2c_tdata), .u_h2c_tvalid(h2c_tvalid), .u_h2c_tready(h2c_tready), .u_h2c_tlast(h2c_tlast),
        .u_h2c_qid(h2c_qid), .u_h2c_port_id(h2c_port_id), .u_h2c_err(h2c_err), .u_h2c_mdata(h2c_mdata),
        .u_h2c_mty(h2c_mty), .u_h2c_zero_byte(h2c_zero_byte), .u_h2c_tcrc(h2c_tcrc),
        .u_c2h_tdata(c2h_tdata), .u_c2h_tvalid(c2h_tvalid), .u_c2h_tready(c2h_tready), .u_c2h_tlast(c2h_tlast),
        .u_c2h_ctrl_len(c2h_len), .u_c2h_ctrl_qid(c2h_qid), .u_c2h_ctrl_port_id(3'd0),
        .u_c2h_ctrl_marker(1'b0), .u_c2h_ctrl_has_cmpt(c2h_has_cmpt),
        .u_cmpt_data(cmpt_data), .u_cmpt_tvalid(cmpt_tvalid), .u_cmpt_tready(cmpt_tready),
        .u_cmpt_size(cmpt_size), .u_cmpt_col_idx(3'd0), .u_cmpt_err_idx(3'd0), .u_cmpt_qid(cmpt_qid),
        .u_cmpt_cmpt_type(2'd3), .u_cmpt_wait_pld_pkt_id(cmpt_pkt_id), .u_cmpt_port_id(3'd0),
        .u_cmpt_marker(1'b0), .u_cmpt_user_trig(1'b0), .u_cmpt_no_wrb_marker(1'b0),
        .u_c2h_sts_valid(sts_valid), .u_c2h_sts_drop(sts_drop), .u_c2h_sts_qid(sts_qid),
        .u_c2h_sts_last(sts_last), .u_c2h_sts_status_cmp(sts_cmp), .u_c2h_sts_error(sts_error),
        .u_dsc_sts_valid(dsc_valid), .u_dsc_sts_byp(dsc_byp), .u_dsc_sts_dir(dsc_dir), .u_dsc_sts_mm(dsc_mm),
        .u_dsc_sts_qid(dsc_qid), .u_dsc_sts_avl(dsc_avl), .u_dsc_sts_pidx(dsc_pidx),
        .u_dsc_sts_port_id(dsc_port_id), .u_dsc_sts_qinv(dsc_qinv), .u_dsc_sts_irq_arm(dsc_irq_arm),
        .u_dsc_sts_error(dsc_error), .u_dsc_sts_qen(dsc_qen),
        .u_qsts_vld(qsts_vld), .u_qsts_op(qsts_op), .u_qsts_data(qsts_data), .u_qsts_port_id(qsts_port_id),
        .u_qsts_qid(qsts_qid), .u_evt_overflow(evt_overflow),
        .u_irq_valid(1'b0), .u_irq_vec(11'd0), .u_irq_fnc(13'd0), .u_irq_ack(irq_ack), .u_irq_fail(irq_fail),
        .u_crdt_valid(1'b0), .u_crdt_rdy(crdt_rdy), .u_crdt_dir(1'b0), .u_crdt_crdt(16'd0),
        .u_crdt_qid(12'd0), .u_crdt_fence(1'b0),
        .s_h2c0_tvalid(s_h2c0_tvalid), .s_h2c0_tready(s_h2c0_tready), .s_h2c0_tdata(s_h2c0_tdata),
        .s_h2c0_tkeep(s_h2c0_tkeep), .s_h2c0_tlast(s_h2c0_tlast),
        .s_h2c1_tvalid(s_h2c1_tvalid), .s_h2c1_tready(s_h2c1_tready), .s_h2c1_tdata(s_h2c1_tdata),
        .s_h2c1_tkeep(s_h2c1_tkeep), .s_h2c1_tlast(s_h2c1_tlast),
        .s_evt_tvalid(s_evt_tvalid), .s_evt_tready(s_evt_tready), .s_evt_tdata(s_evt_tdata),
        .s_evt_tkeep(s_evt_tkeep), .s_evt_tlast(s_evt_tlast),
        .m_c2h0_tvalid(m_c2h0_tvalid), .m_c2h0_tready(m_c2h0_tready), .m_c2h0_tdata(m_c2h0_tdata),
        .m_c2h0_tkeep(m_c2h0_tkeep), .m_c2h0_tlast(m_c2h0_tlast),
        .m_c2h1_tvalid(m_c2h1_tvalid), .m_c2h1_tready(m_c2h1_tready), .m_c2h1_tdata(m_c2h1_tdata),
        .m_c2h1_tkeep(m_c2h1_tkeep), .m_c2h1_tlast(m_c2h1_tlast),
        .m_cmpt_tvalid(m_cmpt_tvalid), .m_cmpt_tready(m_cmpt_tready), .m_cmpt_tdata(m_cmpt_tdata),
        .m_cmpt_tkeep(m_cmpt_tkeep), .m_cmpt_tlast(m_cmpt_tlast),
        .m_cmd_tvalid(m_cmd_tvalid), .m_cmd_tready(m_cmd_tready), .m_cmd_tdata(m_cmd_tdata),
        .m_cmd_tkeep(m_cmd_tkeep), .m_cmd_tlast(m_cmd_tlast),
        .status(noc_status));

    qnoc_lb u_lb (
        .clk(clk), .rstn(rst_n),
        .h2c_tdata(h2c_tdata), .h2c_tvalid(h2c_tvalid), .h2c_tready(h2c_tready), .h2c_tlast(h2c_tlast),
        .h2c_qid(h2c_qid), .h2c_err(h2c_err), .h2c_mty(h2c_mty), .h2c_zero_byte(h2c_zero_byte),
        .c2h_tdata(c2h_tdata), .c2h_tvalid(c2h_tvalid), .c2h_tready(c2h_tready), .c2h_tlast(c2h_tlast),
        .c2h_ctrl_len(c2h_len), .c2h_ctrl_qid(c2h_qid), .c2h_ctrl_has_cmpt(c2h_has_cmpt),
        .cmpt_data(cmpt_data), .cmpt_tvalid(cmpt_tvalid), .cmpt_tready(cmpt_tready), .cmpt_size(cmpt_size),
        .cmpt_qid(cmpt_qid), .cmpt_wait_pld_pkt_id(cmpt_pkt_id),
        .c2h_sts_valid(sts_valid), .c2h_sts_drop(sts_drop), .c2h_sts_error(sts_error), .c2h_sts_qid(sts_qid),
        .dsc_sts_valid(dsc_valid), .qsts_vld(qsts_vld), .evt_overflow(evt_overflow), .noc_status(noc_status[0]),
        .s_axil_awaddr(s_axil_awaddr), .s_axil_awvalid(s_axil_awvalid), .s_axil_awready(s_axil_awready),
        .s_axil_wdata(s_axil_wdata), .s_axil_wvalid(s_axil_wvalid), .s_axil_wready(s_axil_wready),
        .s_axil_bresp(s_axil_bresp), .s_axil_bvalid(s_axil_bvalid), .s_axil_bready(s_axil_bready),
        .s_axil_araddr(s_axil_araddr), .s_axil_arvalid(s_axil_arvalid), .s_axil_arready(s_axil_arready),
        .s_axil_rdata(s_axil_rdata), .s_axil_rresp(s_axil_rresp), .s_axil_rvalid(s_axil_rvalid),
        .s_axil_rready(s_axil_rready));
endmodule
