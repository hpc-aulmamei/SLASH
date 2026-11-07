// QDMA streaming bridge of the V80 NIC static: CPM5 dma1 streaming ports ->
// qdma_st_pipe (registered stages next to the CPM) -> qdma_noc_static ->
// NoC AXI4-Stream ports (axis_noc NMU/NSU) that cross into the service layer.
// Formats: qdma_noc_static.v. clk = DMA clock (dma1_intrfc_clk),
// rstn = dma1_axi_aresetn.
`timescale 1ns / 1ps

module qdma_noc_bridge #(
    parameter integer STAGES = 2
) (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF cpm_h2c:cpm_c2h:cpm_cmpt:cpm_c2h_sts:cpm_irq:cpm_dsc_sts:cpm_qsts:cpm_crdt:m_h2c0:m_h2c1:m_evt:s_c2h0:s_c2h1:s_cmpt:s_cmd, ASSOCIATED_RESET rstn" *)
    input  wire         clk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 rstn RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire         rstn,

    (* X_INTERFACE_MODE = "slave" *)
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 cpm_h2c tdata" *)     input  wire [511:0] cpm_h2c_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 cpm_h2c tcrc" *)      input  wire [31:0]  cpm_h2c_tcrc,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 cpm_h2c tvalid" *)    input  wire         cpm_h2c_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 cpm_h2c tlast" *)     input  wire         cpm_h2c_tlast,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 cpm_h2c tready" *)    output wire         cpm_h2c_tready,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 cpm_h2c qid" *)       input  wire [11:0]  cpm_h2c_qid,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 cpm_h2c port_id" *)   input  wire [2:0]   cpm_h2c_port_id,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 cpm_h2c err" *)       input  wire         cpm_h2c_err,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 cpm_h2c mdata" *)     input  wire [31:0]  cpm_h2c_mdata,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 cpm_h2c mty" *)       input  wire [5:0]   cpm_h2c_mty,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 cpm_h2c zero_byte" *) input  wire         cpm_h2c_zero_byte,
    (* X_INTERFACE_MODE = "master" *)
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 cpm_c2h tdata" *)         output wire [511:0] cpm_c2h_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 cpm_c2h tcrc" *)          output wire [31:0]  cpm_c2h_tcrc,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 cpm_c2h ctrl_marker" *)   output wire         cpm_c2h_ctrl_marker,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 cpm_c2h ctrl_port_id" *)  output wire [2:0]   cpm_c2h_ctrl_port_id,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 cpm_c2h ctrl_len" *)      output wire [15:0]  cpm_c2h_ctrl_len,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 cpm_c2h ctrl_qid" *)      output wire [11:0]  cpm_c2h_ctrl_qid,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 cpm_c2h ctrl_has_cmpt" *) output wire         cpm_c2h_ctrl_has_cmpt,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 cpm_c2h mty" *)           output wire [5:0]   cpm_c2h_mty,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 cpm_c2h tvalid" *)        output wire         cpm_c2h_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 cpm_c2h tlast" *)         output wire         cpm_c2h_tlast,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 cpm_c2h tready" *)        input  wire         cpm_c2h_tready,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 cpm_c2h ecc" *)           output wire [6:0]   cpm_c2h_ecc,
    (* X_INTERFACE_MODE = "master" *)
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 cpm_cmpt data" *)            output wire [511:0] cpm_cmpt_data,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 cpm_cmpt dpar" *)            output wire [15:0]  cpm_cmpt_dpar,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 cpm_cmpt tvalid" *)          output wire         cpm_cmpt_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 cpm_cmpt tready" *)          input  wire         cpm_cmpt_tready,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 cpm_cmpt size" *)            output wire [1:0]   cpm_cmpt_size,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 cpm_cmpt col_idx" *)         output wire [2:0]   cpm_cmpt_col_idx,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 cpm_cmpt err_idx" *)         output wire [2:0]   cpm_cmpt_err_idx,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 cpm_cmpt qid" *)             output wire [12:0]  cpm_cmpt_qid,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 cpm_cmpt cmpt_type" *)       output wire [1:0]   cpm_cmpt_cmpt_type,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 cpm_cmpt wait_pld_pkt_id" *) output wire [15:0]  cpm_cmpt_wait_pld_pkt_id,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 cpm_cmpt port_id" *)         output wire [2:0]   cpm_cmpt_port_id,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 cpm_cmpt marker" *)          output wire         cpm_cmpt_marker,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 cpm_cmpt user_trig" *)       output wire         cpm_cmpt_user_trig,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 cpm_cmpt no_wrb_marker" *)   output wire         cpm_cmpt_no_wrb_marker,
    (* X_INTERFACE_MODE = "slave" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_c2h_status:1.0 cpm_c2h_sts valid" *)      input  wire        cpm_c2h_sts_valid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_c2h_status:1.0 cpm_c2h_sts drop" *)       input  wire        cpm_c2h_sts_drop,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_c2h_status:1.0 cpm_c2h_sts qid" *)        input  wire [11:0] cpm_c2h_sts_qid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_c2h_status:1.0 cpm_c2h_sts last" *)       input  wire        cpm_c2h_sts_last,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_c2h_status:1.0 cpm_c2h_sts status_cmp" *) input  wire        cpm_c2h_sts_status_cmp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_c2h_status:1.0 cpm_c2h_sts error" *)      input  wire        cpm_c2h_sts_error,
    (* X_INTERFACE_MODE = "master" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_usr_irq:1.0 cpm_irq valid" *) output wire        cpm_irq_valid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_usr_irq:1.0 cpm_irq vec" *)   output wire [10:0] cpm_irq_vec,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_usr_irq:1.0 cpm_irq fnc" *)   output wire [12:0] cpm_irq_fnc,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_usr_irq:1.0 cpm_irq ack" *)   input  wire        cpm_irq_ack,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_usr_irq:1.0 cpm_irq fail" *)  input  wire        cpm_irq_fail,
    (* X_INTERFACE_MODE = "slave" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 cpm_dsc_sts valid" *)   input  wire        cpm_dsc_sts_valid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 cpm_dsc_sts rdy" *)     output wire        cpm_dsc_sts_rdy,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 cpm_dsc_sts byp" *)     input  wire        cpm_dsc_sts_byp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 cpm_dsc_sts dir" *)     input  wire        cpm_dsc_sts_dir,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 cpm_dsc_sts mm" *)      input  wire        cpm_dsc_sts_mm,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 cpm_dsc_sts qid" *)     input  wire [11:0] cpm_dsc_sts_qid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 cpm_dsc_sts avl" *)     input  wire [15:0] cpm_dsc_sts_avl,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 cpm_dsc_sts pidx" *)    input  wire [15:0] cpm_dsc_sts_pidx,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 cpm_dsc_sts port_id" *) input  wire [2:0]  cpm_dsc_sts_port_id,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 cpm_dsc_sts qinv" *)    input  wire        cpm_dsc_sts_qinv,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 cpm_dsc_sts irq_arm" *) input  wire        cpm_dsc_sts_irq_arm,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 cpm_dsc_sts error" *)   input  wire        cpm_dsc_sts_error,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 cpm_dsc_sts qen" *)     input  wire        cpm_dsc_sts_qen,
    (* X_INTERFACE_MODE = "slave" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:eqdma_qsts:1.0 cpm_qsts vld" *)     input  wire        cpm_qsts_vld,
    (* X_INTERFACE_INFO = "xilinx.com:interface:eqdma_qsts:1.0 cpm_qsts rdy" *)     output wire        cpm_qsts_rdy,
    (* X_INTERFACE_INFO = "xilinx.com:interface:eqdma_qsts:1.0 cpm_qsts op" *)      input  wire [7:0]  cpm_qsts_op,
    (* X_INTERFACE_INFO = "xilinx.com:interface:eqdma_qsts:1.0 cpm_qsts data" *)    input  wire [63:0] cpm_qsts_data,
    (* X_INTERFACE_INFO = "xilinx.com:interface:eqdma_qsts:1.0 cpm_qsts port_id" *) input  wire [2:0]  cpm_qsts_port_id,
    (* X_INTERFACE_INFO = "xilinx.com:interface:eqdma_qsts:1.0 cpm_qsts qid" *)     input  wire [12:0] cpm_qsts_qid,
    (* X_INTERFACE_MODE = "master" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_dsc_crdt_in:1.0 cpm_crdt valid" *) output wire        cpm_crdt_valid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_dsc_crdt_in:1.0 cpm_crdt rdy" *)   input  wire        cpm_crdt_rdy,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_dsc_crdt_in:1.0 cpm_crdt dir" *)   output wire        cpm_crdt_dir,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_dsc_crdt_in:1.0 cpm_crdt crdt" *)  output wire [15:0] cpm_crdt_crdt,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_dsc_crdt_in:1.0 cpm_crdt qid" *)   output wire [11:0] cpm_crdt_qid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_dsc_crdt_in:1.0 cpm_crdt fence" *) output wire        cpm_crdt_fence,

    (* X_INTERFACE_MODE = "master" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME m_h2c0, TDATA_NUM_BYTES 64, HAS_TKEEP 1, HAS_TLAST 1, HAS_TREADY 1" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_h2c0 TVALID" *) output wire         m_h2c0_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_h2c0 TREADY" *) input  wire         m_h2c0_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_h2c0 TDATA" *)  output wire [511:0] m_h2c0_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_h2c0 TKEEP" *)  output wire [63:0]  m_h2c0_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_h2c0 TLAST" *)  output wire         m_h2c0_tlast,
    (* X_INTERFACE_MODE = "master" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME m_h2c1, TDATA_NUM_BYTES 64, HAS_TKEEP 1, HAS_TLAST 1, HAS_TREADY 1" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_h2c1 TVALID" *) output wire         m_h2c1_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_h2c1 TREADY" *) input  wire         m_h2c1_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_h2c1 TDATA" *)  output wire [511:0] m_h2c1_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_h2c1 TKEEP" *)  output wire [63:0]  m_h2c1_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_h2c1 TLAST" *)  output wire         m_h2c1_tlast,
    (* X_INTERFACE_MODE = "master" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME m_evt, TDATA_NUM_BYTES 32, HAS_TKEEP 1, HAS_TLAST 1, HAS_TREADY 1" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_evt TVALID" *) output wire         m_evt_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_evt TREADY" *) input  wire         m_evt_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_evt TDATA" *)  output wire [255:0] m_evt_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_evt TKEEP" *)  output wire [31:0]  m_evt_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_evt TLAST" *)  output wire         m_evt_tlast,
    (* X_INTERFACE_MODE = "slave" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME s_c2h0, TDATA_NUM_BYTES 64, HAS_TKEEP 1, HAS_TLAST 1, HAS_TREADY 1" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_c2h0 TVALID" *) input  wire         s_c2h0_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_c2h0 TREADY" *) output wire         s_c2h0_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_c2h0 TDATA" *)  input  wire [511:0] s_c2h0_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_c2h0 TKEEP" *)  input  wire [63:0]  s_c2h0_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_c2h0 TLAST" *)  input  wire         s_c2h0_tlast,
    (* X_INTERFACE_MODE = "slave" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME s_c2h1, TDATA_NUM_BYTES 64, HAS_TKEEP 1, HAS_TLAST 1, HAS_TREADY 1" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_c2h1 TVALID" *) input  wire         s_c2h1_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_c2h1 TREADY" *) output wire         s_c2h1_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_c2h1 TDATA" *)  input  wire [511:0] s_c2h1_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_c2h1 TKEEP" *)  input  wire [63:0]  s_c2h1_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_c2h1 TLAST" *)  input  wire         s_c2h1_tlast,
    (* X_INTERFACE_MODE = "slave" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME s_cmpt, TDATA_NUM_BYTES 64, HAS_TKEEP 1, HAS_TLAST 1, HAS_TREADY 1" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_cmpt TVALID" *) input  wire         s_cmpt_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_cmpt TREADY" *) output wire         s_cmpt_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_cmpt TDATA" *)  input  wire [511:0] s_cmpt_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_cmpt TKEEP" *)  input  wire [63:0]  s_cmpt_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_cmpt TLAST" *)  input  wire         s_cmpt_tlast,
    (* X_INTERFACE_MODE = "slave" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME s_cmd, TDATA_NUM_BYTES 32, HAS_TKEEP 1, HAS_TLAST 1, HAS_TREADY 1" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_cmd TVALID" *) input  wire         s_cmd_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_cmd TREADY" *) output wire         s_cmd_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_cmd TDATA" *)  input  wire [255:0] s_cmd_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_cmd TKEEP" *)  input  wire [31:0]  s_cmd_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_cmd TLAST" *)  input  wire         s_cmd_tlast,
    output wire [3:0]   status
);

    wire [511:0]   rm_h2c_tdata;
    wire [31:0]    rm_h2c_tcrc;
    wire           rm_h2c_tvalid;
    wire           rm_h2c_tlast;
    wire           rm_h2c_tready;
    wire [11:0]    rm_h2c_qid;
    wire [2:0]     rm_h2c_port_id;
    wire           rm_h2c_err;
    wire [31:0]    rm_h2c_mdata;
    wire [5:0]     rm_h2c_mty;
    wire           rm_h2c_zero_byte;
    wire [511:0]   rm_c2h_tdata;
    wire [31:0]    rm_c2h_tcrc;
    wire           rm_c2h_ctrl_marker;
    wire [2:0]     rm_c2h_ctrl_port_id;
    wire [15:0]    rm_c2h_ctrl_len;
    wire [11:0]    rm_c2h_ctrl_qid;
    wire           rm_c2h_ctrl_has_cmpt;
    wire [5:0]     rm_c2h_mty;
    wire           rm_c2h_tvalid;
    wire           rm_c2h_tlast;
    wire           rm_c2h_tready;
    wire [6:0]     rm_c2h_ecc;
    wire [511:0]   rm_cmpt_data;
    wire [15:0]    rm_cmpt_dpar;
    wire           rm_cmpt_tvalid;
    wire           rm_cmpt_tready;
    wire [1:0]     rm_cmpt_size;
    wire [2:0]     rm_cmpt_col_idx;
    wire [2:0]     rm_cmpt_err_idx;
    wire [12:0]    rm_cmpt_qid;
    wire [1:0]     rm_cmpt_cmpt_type;
    wire [15:0]    rm_cmpt_wait_pld_pkt_id;
    wire [2:0]     rm_cmpt_port_id;
    wire           rm_cmpt_marker;
    wire           rm_cmpt_user_trig;
    wire           rm_cmpt_no_wrb_marker;
    wire           rm_c2h_sts_valid;
    wire           rm_c2h_sts_drop;
    wire [11:0]    rm_c2h_sts_qid;
    wire           rm_c2h_sts_last;
    wire           rm_c2h_sts_status_cmp;
    wire           rm_c2h_sts_error;
    wire           rm_irq_valid;
    wire [10:0]    rm_irq_vec;
    wire [12:0]    rm_irq_fnc;
    wire           rm_irq_ack;
    wire           rm_irq_fail;
    wire           rm_dsc_sts_valid;
    wire           rm_dsc_sts_rdy;
    wire           rm_dsc_sts_byp;
    wire           rm_dsc_sts_dir;
    wire           rm_dsc_sts_mm;
    wire [11:0]    rm_dsc_sts_qid;
    wire [15:0]    rm_dsc_sts_avl;
    wire [15:0]    rm_dsc_sts_pidx;
    wire [2:0]     rm_dsc_sts_port_id;
    wire           rm_dsc_sts_qinv;
    wire           rm_dsc_sts_irq_arm;
    wire           rm_dsc_sts_error;
    wire           rm_dsc_sts_qen;
    wire           rm_qsts_vld;
    wire           rm_qsts_rdy;
    wire [7:0]     rm_qsts_op;
    wire [63:0]    rm_qsts_data;
    wire [2:0]     rm_qsts_port_id;
    wire [12:0]    rm_qsts_qid;
    wire           rm_crdt_valid;
    wire           rm_crdt_rdy;
    wire           rm_crdt_dir;
    wire [15:0]    rm_crdt_crdt;
    wire [11:0]    rm_crdt_qid;
    wire           rm_crdt_fence;

    qdma_st_pipe #(.STAGES(STAGES)) u_pipe (
        .clk(clk), .rstn(rstn),
        .cpm_h2c_tdata(cpm_h2c_tdata),
        .cpm_h2c_tcrc(cpm_h2c_tcrc),
        .cpm_h2c_tvalid(cpm_h2c_tvalid),
        .cpm_h2c_tlast(cpm_h2c_tlast),
        .cpm_h2c_tready(cpm_h2c_tready),
        .cpm_h2c_qid(cpm_h2c_qid),
        .cpm_h2c_port_id(cpm_h2c_port_id),
        .cpm_h2c_err(cpm_h2c_err),
        .cpm_h2c_mdata(cpm_h2c_mdata),
        .cpm_h2c_mty(cpm_h2c_mty),
        .cpm_h2c_zero_byte(cpm_h2c_zero_byte),
        .cpm_c2h_tdata(cpm_c2h_tdata),
        .cpm_c2h_tcrc(cpm_c2h_tcrc),
        .cpm_c2h_ctrl_marker(cpm_c2h_ctrl_marker),
        .cpm_c2h_ctrl_port_id(cpm_c2h_ctrl_port_id),
        .cpm_c2h_ctrl_len(cpm_c2h_ctrl_len),
        .cpm_c2h_ctrl_qid(cpm_c2h_ctrl_qid),
        .cpm_c2h_ctrl_has_cmpt(cpm_c2h_ctrl_has_cmpt),
        .cpm_c2h_mty(cpm_c2h_mty),
        .cpm_c2h_tvalid(cpm_c2h_tvalid),
        .cpm_c2h_tlast(cpm_c2h_tlast),
        .cpm_c2h_tready(cpm_c2h_tready),
        .cpm_c2h_ecc(cpm_c2h_ecc),
        .cpm_cmpt_data(cpm_cmpt_data),
        .cpm_cmpt_dpar(cpm_cmpt_dpar),
        .cpm_cmpt_tvalid(cpm_cmpt_tvalid),
        .cpm_cmpt_tready(cpm_cmpt_tready),
        .cpm_cmpt_size(cpm_cmpt_size),
        .cpm_cmpt_col_idx(cpm_cmpt_col_idx),
        .cpm_cmpt_err_idx(cpm_cmpt_err_idx),
        .cpm_cmpt_qid(cpm_cmpt_qid),
        .cpm_cmpt_cmpt_type(cpm_cmpt_cmpt_type),
        .cpm_cmpt_wait_pld_pkt_id(cpm_cmpt_wait_pld_pkt_id),
        .cpm_cmpt_port_id(cpm_cmpt_port_id),
        .cpm_cmpt_marker(cpm_cmpt_marker),
        .cpm_cmpt_user_trig(cpm_cmpt_user_trig),
        .cpm_cmpt_no_wrb_marker(cpm_cmpt_no_wrb_marker),
        .cpm_c2h_sts_valid(cpm_c2h_sts_valid),
        .cpm_c2h_sts_drop(cpm_c2h_sts_drop),
        .cpm_c2h_sts_qid(cpm_c2h_sts_qid),
        .cpm_c2h_sts_last(cpm_c2h_sts_last),
        .cpm_c2h_sts_status_cmp(cpm_c2h_sts_status_cmp),
        .cpm_c2h_sts_error(cpm_c2h_sts_error),
        .cpm_irq_valid(cpm_irq_valid),
        .cpm_irq_vec(cpm_irq_vec),
        .cpm_irq_fnc(cpm_irq_fnc),
        .cpm_irq_ack(cpm_irq_ack),
        .cpm_irq_fail(cpm_irq_fail),
        .cpm_dsc_sts_valid(cpm_dsc_sts_valid),
        .cpm_dsc_sts_rdy(cpm_dsc_sts_rdy),
        .cpm_dsc_sts_byp(cpm_dsc_sts_byp),
        .cpm_dsc_sts_dir(cpm_dsc_sts_dir),
        .cpm_dsc_sts_mm(cpm_dsc_sts_mm),
        .cpm_dsc_sts_qid(cpm_dsc_sts_qid),
        .cpm_dsc_sts_avl(cpm_dsc_sts_avl),
        .cpm_dsc_sts_pidx(cpm_dsc_sts_pidx),
        .cpm_dsc_sts_port_id(cpm_dsc_sts_port_id),
        .cpm_dsc_sts_qinv(cpm_dsc_sts_qinv),
        .cpm_dsc_sts_irq_arm(cpm_dsc_sts_irq_arm),
        .cpm_dsc_sts_error(cpm_dsc_sts_error),
        .cpm_dsc_sts_qen(cpm_dsc_sts_qen),
        .cpm_qsts_vld(cpm_qsts_vld),
        .cpm_qsts_rdy(cpm_qsts_rdy),
        .cpm_qsts_op(cpm_qsts_op),
        .cpm_qsts_data(cpm_qsts_data),
        .cpm_qsts_port_id(cpm_qsts_port_id),
        .cpm_qsts_qid(cpm_qsts_qid),
        .cpm_crdt_valid(cpm_crdt_valid),
        .cpm_crdt_rdy(cpm_crdt_rdy),
        .cpm_crdt_dir(cpm_crdt_dir),
        .cpm_crdt_crdt(cpm_crdt_crdt),
        .cpm_crdt_qid(cpm_crdt_qid),
        .cpm_crdt_fence(cpm_crdt_fence),
        .rm_h2c_tdata(rm_h2c_tdata),
        .rm_h2c_tcrc(rm_h2c_tcrc),
        .rm_h2c_tvalid(rm_h2c_tvalid),
        .rm_h2c_tlast(rm_h2c_tlast),
        .rm_h2c_tready(rm_h2c_tready),
        .rm_h2c_qid(rm_h2c_qid),
        .rm_h2c_port_id(rm_h2c_port_id),
        .rm_h2c_err(rm_h2c_err),
        .rm_h2c_mdata(rm_h2c_mdata),
        .rm_h2c_mty(rm_h2c_mty),
        .rm_h2c_zero_byte(rm_h2c_zero_byte),
        .rm_c2h_tdata(rm_c2h_tdata),
        .rm_c2h_tcrc(rm_c2h_tcrc),
        .rm_c2h_ctrl_marker(rm_c2h_ctrl_marker),
        .rm_c2h_ctrl_port_id(rm_c2h_ctrl_port_id),
        .rm_c2h_ctrl_len(rm_c2h_ctrl_len),
        .rm_c2h_ctrl_qid(rm_c2h_ctrl_qid),
        .rm_c2h_ctrl_has_cmpt(rm_c2h_ctrl_has_cmpt),
        .rm_c2h_mty(rm_c2h_mty),
        .rm_c2h_tvalid(rm_c2h_tvalid),
        .rm_c2h_tlast(rm_c2h_tlast),
        .rm_c2h_tready(rm_c2h_tready),
        .rm_c2h_ecc(rm_c2h_ecc),
        .rm_cmpt_data(rm_cmpt_data),
        .rm_cmpt_dpar(rm_cmpt_dpar),
        .rm_cmpt_tvalid(rm_cmpt_tvalid),
        .rm_cmpt_tready(rm_cmpt_tready),
        .rm_cmpt_size(rm_cmpt_size),
        .rm_cmpt_col_idx(rm_cmpt_col_idx),
        .rm_cmpt_err_idx(rm_cmpt_err_idx),
        .rm_cmpt_qid(rm_cmpt_qid),
        .rm_cmpt_cmpt_type(rm_cmpt_cmpt_type),
        .rm_cmpt_wait_pld_pkt_id(rm_cmpt_wait_pld_pkt_id),
        .rm_cmpt_port_id(rm_cmpt_port_id),
        .rm_cmpt_marker(rm_cmpt_marker),
        .rm_cmpt_user_trig(rm_cmpt_user_trig),
        .rm_cmpt_no_wrb_marker(rm_cmpt_no_wrb_marker),
        .rm_c2h_sts_valid(rm_c2h_sts_valid),
        .rm_c2h_sts_drop(rm_c2h_sts_drop),
        .rm_c2h_sts_qid(rm_c2h_sts_qid),
        .rm_c2h_sts_last(rm_c2h_sts_last),
        .rm_c2h_sts_status_cmp(rm_c2h_sts_status_cmp),
        .rm_c2h_sts_error(rm_c2h_sts_error),
        .rm_irq_valid(rm_irq_valid),
        .rm_irq_vec(rm_irq_vec),
        .rm_irq_fnc(rm_irq_fnc),
        .rm_irq_ack(rm_irq_ack),
        .rm_irq_fail(rm_irq_fail),
        .rm_dsc_sts_valid(rm_dsc_sts_valid),
        .rm_dsc_sts_rdy(rm_dsc_sts_rdy),
        .rm_dsc_sts_byp(rm_dsc_sts_byp),
        .rm_dsc_sts_dir(rm_dsc_sts_dir),
        .rm_dsc_sts_mm(rm_dsc_sts_mm),
        .rm_dsc_sts_qid(rm_dsc_sts_qid),
        .rm_dsc_sts_avl(rm_dsc_sts_avl),
        .rm_dsc_sts_pidx(rm_dsc_sts_pidx),
        .rm_dsc_sts_port_id(rm_dsc_sts_port_id),
        .rm_dsc_sts_qinv(rm_dsc_sts_qinv),
        .rm_dsc_sts_irq_arm(rm_dsc_sts_irq_arm),
        .rm_dsc_sts_error(rm_dsc_sts_error),
        .rm_dsc_sts_qen(rm_dsc_sts_qen),
        .rm_qsts_vld(rm_qsts_vld),
        .rm_qsts_rdy(rm_qsts_rdy),
        .rm_qsts_op(rm_qsts_op),
        .rm_qsts_data(rm_qsts_data),
        .rm_qsts_port_id(rm_qsts_port_id),
        .rm_qsts_qid(rm_qsts_qid),
        .rm_crdt_valid(rm_crdt_valid),
        .rm_crdt_rdy(rm_crdt_rdy),
        .rm_crdt_dir(rm_crdt_dir),
        .rm_crdt_crdt(rm_crdt_crdt),
        .rm_crdt_qid(rm_crdt_qid),
        .rm_crdt_fence(rm_crdt_fence)
    );

    qdma_noc_static u_noc (
        .clk(clk), .rstn(rstn),
        .q_h2c_tdata(rm_h2c_tdata),
        .q_h2c_tcrc(rm_h2c_tcrc),
        .q_h2c_tvalid(rm_h2c_tvalid),
        .q_h2c_tlast(rm_h2c_tlast),
        .q_h2c_tready(rm_h2c_tready),
        .q_h2c_qid(rm_h2c_qid),
        .q_h2c_port_id(rm_h2c_port_id),
        .q_h2c_err(rm_h2c_err),
        .q_h2c_mdata(rm_h2c_mdata),
        .q_h2c_mty(rm_h2c_mty),
        .q_h2c_zero_byte(rm_h2c_zero_byte),
        .q_c2h_tdata(rm_c2h_tdata),
        .q_c2h_tcrc(rm_c2h_tcrc),
        .q_c2h_ctrl_marker(rm_c2h_ctrl_marker),
        .q_c2h_ctrl_port_id(rm_c2h_ctrl_port_id),
        .q_c2h_ctrl_len(rm_c2h_ctrl_len),
        .q_c2h_ctrl_qid(rm_c2h_ctrl_qid),
        .q_c2h_ctrl_has_cmpt(rm_c2h_ctrl_has_cmpt),
        .q_c2h_mty(rm_c2h_mty),
        .q_c2h_tvalid(rm_c2h_tvalid),
        .q_c2h_tlast(rm_c2h_tlast),
        .q_c2h_tready(rm_c2h_tready),
        .q_c2h_ecc(rm_c2h_ecc),
        .q_cmpt_data(rm_cmpt_data),
        .q_cmpt_dpar(rm_cmpt_dpar),
        .q_cmpt_tvalid(rm_cmpt_tvalid),
        .q_cmpt_tready(rm_cmpt_tready),
        .q_cmpt_size(rm_cmpt_size),
        .q_cmpt_col_idx(rm_cmpt_col_idx),
        .q_cmpt_err_idx(rm_cmpt_err_idx),
        .q_cmpt_qid(rm_cmpt_qid),
        .q_cmpt_cmpt_type(rm_cmpt_cmpt_type),
        .q_cmpt_wait_pld_pkt_id(rm_cmpt_wait_pld_pkt_id),
        .q_cmpt_port_id(rm_cmpt_port_id),
        .q_cmpt_marker(rm_cmpt_marker),
        .q_cmpt_user_trig(rm_cmpt_user_trig),
        .q_cmpt_no_wrb_marker(rm_cmpt_no_wrb_marker),
        .q_c2h_sts_valid(rm_c2h_sts_valid),
        .q_c2h_sts_drop(rm_c2h_sts_drop),
        .q_c2h_sts_qid(rm_c2h_sts_qid),
        .q_c2h_sts_last(rm_c2h_sts_last),
        .q_c2h_sts_status_cmp(rm_c2h_sts_status_cmp),
        .q_c2h_sts_error(rm_c2h_sts_error),
        .q_irq_valid(rm_irq_valid),
        .q_irq_vec(rm_irq_vec),
        .q_irq_fnc(rm_irq_fnc),
        .q_irq_ack(rm_irq_ack),
        .q_irq_fail(rm_irq_fail),
        .q_dsc_sts_valid(rm_dsc_sts_valid),
        .q_dsc_sts_rdy(rm_dsc_sts_rdy),
        .q_dsc_sts_byp(rm_dsc_sts_byp),
        .q_dsc_sts_dir(rm_dsc_sts_dir),
        .q_dsc_sts_mm(rm_dsc_sts_mm),
        .q_dsc_sts_qid(rm_dsc_sts_qid),
        .q_dsc_sts_avl(rm_dsc_sts_avl),
        .q_dsc_sts_pidx(rm_dsc_sts_pidx),
        .q_dsc_sts_port_id(rm_dsc_sts_port_id),
        .q_dsc_sts_qinv(rm_dsc_sts_qinv),
        .q_dsc_sts_irq_arm(rm_dsc_sts_irq_arm),
        .q_dsc_sts_error(rm_dsc_sts_error),
        .q_dsc_sts_qen(rm_dsc_sts_qen),
        .q_qsts_vld(rm_qsts_vld),
        .q_qsts_rdy(rm_qsts_rdy),
        .q_qsts_op(rm_qsts_op),
        .q_qsts_data(rm_qsts_data),
        .q_qsts_port_id(rm_qsts_port_id),
        .q_qsts_qid(rm_qsts_qid),
        .q_crdt_valid(rm_crdt_valid),
        .q_crdt_rdy(rm_crdt_rdy),
        .q_crdt_dir(rm_crdt_dir),
        .q_crdt_crdt(rm_crdt_crdt),
        .q_crdt_qid(rm_crdt_qid),
        .q_crdt_fence(rm_crdt_fence),
        .m_h2c0_tvalid(m_h2c0_tvalid),
        .m_h2c0_tready(m_h2c0_tready),
        .m_h2c0_tdata(m_h2c0_tdata),
        .m_h2c0_tkeep(m_h2c0_tkeep),
        .m_h2c0_tlast(m_h2c0_tlast),
        .m_h2c1_tvalid(m_h2c1_tvalid),
        .m_h2c1_tready(m_h2c1_tready),
        .m_h2c1_tdata(m_h2c1_tdata),
        .m_h2c1_tkeep(m_h2c1_tkeep),
        .m_h2c1_tlast(m_h2c1_tlast),
        .m_evt_tvalid(m_evt_tvalid),
        .m_evt_tready(m_evt_tready),
        .m_evt_tdata(m_evt_tdata),
        .m_evt_tkeep(m_evt_tkeep),
        .m_evt_tlast(m_evt_tlast),
        .s_c2h0_tvalid(s_c2h0_tvalid),
        .s_c2h0_tready(s_c2h0_tready),
        .s_c2h0_tdata(s_c2h0_tdata),
        .s_c2h0_tkeep(s_c2h0_tkeep),
        .s_c2h0_tlast(s_c2h0_tlast),
        .s_c2h1_tvalid(s_c2h1_tvalid),
        .s_c2h1_tready(s_c2h1_tready),
        .s_c2h1_tdata(s_c2h1_tdata),
        .s_c2h1_tkeep(s_c2h1_tkeep),
        .s_c2h1_tlast(s_c2h1_tlast),
        .s_cmpt_tvalid(s_cmpt_tvalid),
        .s_cmpt_tready(s_cmpt_tready),
        .s_cmpt_tdata(s_cmpt_tdata),
        .s_cmpt_tkeep(s_cmpt_tkeep),
        .s_cmpt_tlast(s_cmpt_tlast),
        .s_cmd_tvalid(s_cmd_tvalid),
        .s_cmd_tready(s_cmd_tready),
        .s_cmd_tdata(s_cmd_tdata),
        .s_cmd_tkeep(s_cmd_tkeep),
        .s_cmd_tlast(s_cmd_tlast),
        .status(status)
    );
endmodule
