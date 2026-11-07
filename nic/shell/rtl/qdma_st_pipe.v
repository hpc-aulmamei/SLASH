// QDMA (CPM5) AXI-Stream pipeline between the CIPS dma1 streaming ports and the
// service-layer partition of the V80 NIC static shell.
//
// The CPM sits at the bottom left of SLR0, the service-layer pblock starts
// well above it, and the DMA clock is 390 MHz. Every interface crosses here
// through STAGES fully registered stages (valid/ready skid buffers, plain
// registers for the valid-only c2h, descriptor and queue status), so the
// static owns the long routes and the partition boundary is register to
// register.
//
// usr_irq: the CPM takes a request while valid is high and answers with a
// one-cycle ack or fail; valid must then drop. Through a pipeline the RM
// cannot drop valid in time, so the stage next to the CPM gates it: after
// ack or fail it holds valid low until the RM side releases valid. RM rule:
// raise valid with vec/fnc stable, keep it until ack or fail comes back, then
// drop it for at least one cycle.
//
// clk = dma1_intrfc_clk, rstn = dma1_axi_aresetn (synchronous to clk).
`timescale 1ns / 1ps

module qdma_st_pipe #(
    parameter integer STAGES = 2
) (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF cpm_h2c:cpm_c2h:cpm_cmpt:cpm_c2h_sts:cpm_irq:cpm_dsc_sts:cpm_qsts:cpm_crdt:rm_h2c:rm_c2h:rm_cmpt:rm_c2h_sts:rm_irq:rm_dsc_sts:rm_qsts:rm_crdt, ASSOCIATED_RESET rstn" *)
    input  wire         clk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 rstn RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire         rstn,

    // ---- h2c: CPM -> RM ------------------------------------------------
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
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 rm_h2c tdata" *)     output wire [511:0] rm_h2c_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 rm_h2c tcrc" *)      output wire [31:0]  rm_h2c_tcrc,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 rm_h2c tvalid" *)    output wire         rm_h2c_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 rm_h2c tlast" *)     output wire         rm_h2c_tlast,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 rm_h2c tready" *)    input  wire         rm_h2c_tready,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 rm_h2c qid" *)       output wire [11:0]  rm_h2c_qid,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 rm_h2c port_id" *)   output wire [2:0]   rm_h2c_port_id,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 rm_h2c err" *)       output wire         rm_h2c_err,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 rm_h2c mdata" *)     output wire [31:0]  rm_h2c_mdata,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 rm_h2c mty" *)       output wire [5:0]   rm_h2c_mty,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:m_axis_h2c:1.0 rm_h2c zero_byte" *) output wire         rm_h2c_zero_byte,

    // ---- c2h: RM -> CPM ------------------------------------------------
    (* X_INTERFACE_MODE = "slave" *)
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 rm_c2h tdata" *)         input  wire [511:0] rm_c2h_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 rm_c2h tcrc" *)          input  wire [31:0]  rm_c2h_tcrc,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 rm_c2h ctrl_marker" *)   input  wire         rm_c2h_ctrl_marker,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 rm_c2h ctrl_port_id" *)  input  wire [2:0]   rm_c2h_ctrl_port_id,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 rm_c2h ctrl_len" *)      input  wire [15:0]  rm_c2h_ctrl_len,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 rm_c2h ctrl_qid" *)      input  wire [11:0]  rm_c2h_ctrl_qid,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 rm_c2h ctrl_has_cmpt" *) input  wire         rm_c2h_ctrl_has_cmpt,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 rm_c2h mty" *)           input  wire [5:0]   rm_c2h_mty,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 rm_c2h tvalid" *)        input  wire         rm_c2h_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 rm_c2h tlast" *)         input  wire         rm_c2h_tlast,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 rm_c2h tready" *)        output wire         rm_c2h_tready,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h:1.0 rm_c2h ecc" *)           input  wire [6:0]   rm_c2h_ecc,

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

    // ---- c2h completion: RM -> CPM -------------------------------------
    (* X_INTERFACE_MODE = "slave" *)
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 rm_cmpt data" *)            input  wire [511:0] rm_cmpt_data,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 rm_cmpt dpar" *)            input  wire [15:0]  rm_cmpt_dpar,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 rm_cmpt tvalid" *)          input  wire         rm_cmpt_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 rm_cmpt tready" *)          output wire         rm_cmpt_tready,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 rm_cmpt size" *)            input  wire [1:0]   rm_cmpt_size,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 rm_cmpt col_idx" *)         input  wire [2:0]   rm_cmpt_col_idx,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 rm_cmpt err_idx" *)         input  wire [2:0]   rm_cmpt_err_idx,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 rm_cmpt qid" *)             input  wire [12:0]  rm_cmpt_qid,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 rm_cmpt cmpt_type" *)       input  wire [1:0]   rm_cmpt_cmpt_type,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 rm_cmpt wait_pld_pkt_id" *) input  wire [15:0]  rm_cmpt_wait_pld_pkt_id,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 rm_cmpt port_id" *)         input  wire [2:0]   rm_cmpt_port_id,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 rm_cmpt marker" *)          input  wire         rm_cmpt_marker,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 rm_cmpt user_trig" *)       input  wire         rm_cmpt_user_trig,
    (* X_INTERFACE_INFO = "xilinx.com:display_eqdma:s_axis_c2h_cmpt:1.0 rm_cmpt no_wrb_marker" *)   input  wire         rm_cmpt_no_wrb_marker,

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

    // ---- c2h status: CPM -> RM, valid only -----------------------------
    (* X_INTERFACE_MODE = "slave" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_c2h_status:1.0 cpm_c2h_sts valid" *)      input  wire        cpm_c2h_sts_valid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_c2h_status:1.0 cpm_c2h_sts drop" *)       input  wire        cpm_c2h_sts_drop,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_c2h_status:1.0 cpm_c2h_sts qid" *)        input  wire [11:0] cpm_c2h_sts_qid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_c2h_status:1.0 cpm_c2h_sts last" *)       input  wire        cpm_c2h_sts_last,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_c2h_status:1.0 cpm_c2h_sts status_cmp" *) input  wire        cpm_c2h_sts_status_cmp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_c2h_status:1.0 cpm_c2h_sts error" *)      input  wire        cpm_c2h_sts_error,

    (* X_INTERFACE_MODE = "master" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_c2h_status:1.0 rm_c2h_sts valid" *)      output wire        rm_c2h_sts_valid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_c2h_status:1.0 rm_c2h_sts drop" *)       output wire        rm_c2h_sts_drop,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_c2h_status:1.0 rm_c2h_sts qid" *)        output wire [11:0] rm_c2h_sts_qid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_c2h_status:1.0 rm_c2h_sts last" *)       output wire        rm_c2h_sts_last,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_c2h_status:1.0 rm_c2h_sts status_cmp" *) output wire        rm_c2h_sts_status_cmp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_c2h_status:1.0 rm_c2h_sts error" *)      output wire        rm_c2h_sts_error,

    // ---- user interrupt: RM -> CPM -------------------------------------
    (* X_INTERFACE_MODE = "slave" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_usr_irq:1.0 rm_irq valid" *) input  wire        rm_irq_valid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_usr_irq:1.0 rm_irq vec" *)   input  wire [10:0] rm_irq_vec,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_usr_irq:1.0 rm_irq fnc" *)   input  wire [12:0] rm_irq_fnc,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_usr_irq:1.0 rm_irq ack" *)   output wire        rm_irq_ack,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_usr_irq:1.0 rm_irq fail" *)  output wire        rm_irq_fail,

    (* X_INTERFACE_MODE = "master" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_usr_irq:1.0 cpm_irq valid" *) output reg         cpm_irq_valid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_usr_irq:1.0 cpm_irq vec" *)   output reg  [10:0] cpm_irq_vec,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_usr_irq:1.0 cpm_irq fnc" *)   output reg  [12:0] cpm_irq_fnc,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_usr_irq:1.0 cpm_irq ack" *)   input  wire        cpm_irq_ack,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_usr_irq:1.0 cpm_irq fail" *)  input  wire        cpm_irq_fail,

    // ---- descriptor status (traffic manager): CPM -> RM ----------------
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

    (* X_INTERFACE_MODE = "master" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 rm_dsc_sts valid" *)   output wire        rm_dsc_sts_valid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 rm_dsc_sts rdy" *)     input  wire        rm_dsc_sts_rdy,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 rm_dsc_sts byp" *)     output wire        rm_dsc_sts_byp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 rm_dsc_sts dir" *)     output wire        rm_dsc_sts_dir,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 rm_dsc_sts mm" *)      output wire        rm_dsc_sts_mm,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 rm_dsc_sts qid" *)     output wire [11:0] rm_dsc_sts_qid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 rm_dsc_sts avl" *)     output wire [15:0] rm_dsc_sts_avl,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 rm_dsc_sts pidx" *)    output wire [15:0] rm_dsc_sts_pidx,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 rm_dsc_sts port_id" *) output wire [2:0]  rm_dsc_sts_port_id,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 rm_dsc_sts qinv" *)    output wire        rm_dsc_sts_qinv,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 rm_dsc_sts irq_arm" *) output wire        rm_dsc_sts_irq_arm,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 rm_dsc_sts error" *)   output wire        rm_dsc_sts_error,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_tm_dsc_sts:1.0 rm_dsc_sts qen" *)     output wire        rm_dsc_sts_qen,

    // ---- queue status: CPM -> RM ---------------------------------------
    (* X_INTERFACE_MODE = "slave" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:eqdma_qsts:1.0 cpm_qsts vld" *)     input  wire        cpm_qsts_vld,
    (* X_INTERFACE_INFO = "xilinx.com:interface:eqdma_qsts:1.0 cpm_qsts rdy" *)     output wire        cpm_qsts_rdy,
    (* X_INTERFACE_INFO = "xilinx.com:interface:eqdma_qsts:1.0 cpm_qsts op" *)      input  wire [7:0]  cpm_qsts_op,
    (* X_INTERFACE_INFO = "xilinx.com:interface:eqdma_qsts:1.0 cpm_qsts data" *)    input  wire [63:0] cpm_qsts_data,
    (* X_INTERFACE_INFO = "xilinx.com:interface:eqdma_qsts:1.0 cpm_qsts port_id" *) input  wire [2:0]  cpm_qsts_port_id,
    (* X_INTERFACE_INFO = "xilinx.com:interface:eqdma_qsts:1.0 cpm_qsts qid" *)     input  wire [12:0] cpm_qsts_qid,

    (* X_INTERFACE_MODE = "master" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:eqdma_qsts:1.0 rm_qsts vld" *)     output wire        rm_qsts_vld,
    (* X_INTERFACE_INFO = "xilinx.com:interface:eqdma_qsts:1.0 rm_qsts rdy" *)     input  wire        rm_qsts_rdy,
    (* X_INTERFACE_INFO = "xilinx.com:interface:eqdma_qsts:1.0 rm_qsts op" *)      output wire [7:0]  rm_qsts_op,
    (* X_INTERFACE_INFO = "xilinx.com:interface:eqdma_qsts:1.0 rm_qsts data" *)    output wire [63:0] rm_qsts_data,
    (* X_INTERFACE_INFO = "xilinx.com:interface:eqdma_qsts:1.0 rm_qsts port_id" *) output wire [2:0]  rm_qsts_port_id,
    (* X_INTERFACE_INFO = "xilinx.com:interface:eqdma_qsts:1.0 rm_qsts qid" *)     output wire [12:0] rm_qsts_qid,

    // ---- descriptor credits: RM -> CPM ---------------------------------
    (* X_INTERFACE_MODE = "slave" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_dsc_crdt_in:1.0 rm_crdt valid" *) input  wire        rm_crdt_valid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_dsc_crdt_in:1.0 rm_crdt rdy" *)   output wire        rm_crdt_rdy,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_dsc_crdt_in:1.0 rm_crdt dir" *)   input  wire        rm_crdt_dir,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_dsc_crdt_in:1.0 rm_crdt crdt" *)  input  wire [15:0] rm_crdt_crdt,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_dsc_crdt_in:1.0 rm_crdt qid" *)   input  wire [11:0] rm_crdt_qid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_dsc_crdt_in:1.0 rm_crdt fence" *) input  wire        rm_crdt_fence,

    (* X_INTERFACE_MODE = "master" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_dsc_crdt_in:1.0 cpm_crdt valid" *) output wire        cpm_crdt_valid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_dsc_crdt_in:1.0 cpm_crdt rdy" *)   input  wire        cpm_crdt_rdy,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_dsc_crdt_in:1.0 cpm_crdt dir" *)   output wire        cpm_crdt_dir,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_dsc_crdt_in:1.0 cpm_crdt crdt" *)  output wire [15:0] cpm_crdt_crdt,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_dsc_crdt_in:1.0 cpm_crdt qid" *)   output wire [11:0] cpm_crdt_qid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:qdma_dsc_crdt_in:1.0 cpm_crdt fence" *) output wire        cpm_crdt_fence
);

    // ---- h2c ----
    localparam integer W_H2C = 512 + 32 + 1 + 12 + 3 + 1 + 32 + 6 + 1;
    qsp_pipe #(.W(W_H2C), .N(STAGES)) u_h2c (
        .clk(clk), .rstn(rstn),
        .s_valid(cpm_h2c_tvalid), .s_ready(cpm_h2c_tready),
        .s_data({cpm_h2c_tdata, cpm_h2c_tcrc, cpm_h2c_tlast, cpm_h2c_qid, cpm_h2c_port_id,
                 cpm_h2c_err, cpm_h2c_mdata, cpm_h2c_mty, cpm_h2c_zero_byte}),
        .m_valid(rm_h2c_tvalid), .m_ready(rm_h2c_tready),
        .m_data({rm_h2c_tdata, rm_h2c_tcrc, rm_h2c_tlast, rm_h2c_qid, rm_h2c_port_id,
                 rm_h2c_err, rm_h2c_mdata, rm_h2c_mty, rm_h2c_zero_byte}));

    // ---- c2h ----
    localparam integer W_C2H = 512 + 32 + 1 + 3 + 16 + 12 + 1 + 6 + 1 + 7;
    qsp_pipe #(.W(W_C2H), .N(STAGES)) u_c2h (
        .clk(clk), .rstn(rstn),
        .s_valid(rm_c2h_tvalid), .s_ready(rm_c2h_tready),
        .s_data({rm_c2h_tdata, rm_c2h_tcrc, rm_c2h_ctrl_marker, rm_c2h_ctrl_port_id, rm_c2h_ctrl_len,
                 rm_c2h_ctrl_qid, rm_c2h_ctrl_has_cmpt, rm_c2h_mty, rm_c2h_tlast, rm_c2h_ecc}),
        .m_valid(cpm_c2h_tvalid), .m_ready(cpm_c2h_tready),
        .m_data({cpm_c2h_tdata, cpm_c2h_tcrc, cpm_c2h_ctrl_marker, cpm_c2h_ctrl_port_id, cpm_c2h_ctrl_len,
                 cpm_c2h_ctrl_qid, cpm_c2h_ctrl_has_cmpt, cpm_c2h_mty, cpm_c2h_tlast, cpm_c2h_ecc}));

    // ---- c2h completion ----
    localparam integer W_CMPT = 512 + 16 + 2 + 3 + 3 + 13 + 2 + 16 + 3 + 1 + 1 + 1;
    qsp_pipe #(.W(W_CMPT), .N(STAGES)) u_cmpt (
        .clk(clk), .rstn(rstn),
        .s_valid(rm_cmpt_tvalid), .s_ready(rm_cmpt_tready),
        .s_data({rm_cmpt_data, rm_cmpt_dpar, rm_cmpt_size, rm_cmpt_col_idx, rm_cmpt_err_idx, rm_cmpt_qid,
                 rm_cmpt_cmpt_type, rm_cmpt_wait_pld_pkt_id, rm_cmpt_port_id, rm_cmpt_marker,
                 rm_cmpt_user_trig, rm_cmpt_no_wrb_marker}),
        .m_valid(cpm_cmpt_tvalid), .m_ready(cpm_cmpt_tready),
        .m_data({cpm_cmpt_data, cpm_cmpt_dpar, cpm_cmpt_size, cpm_cmpt_col_idx, cpm_cmpt_err_idx, cpm_cmpt_qid,
                 cpm_cmpt_cmpt_type, cpm_cmpt_wait_pld_pkt_id, cpm_cmpt_port_id, cpm_cmpt_marker,
                 cpm_cmpt_user_trig, cpm_cmpt_no_wrb_marker}));

    // ---- c2h status (no back-pressure) ----
    qsp_delay #(.W(16), .N(STAGES)) u_c2h_sts (
        .clk(clk), .rstn(rstn),
        .s_valid(cpm_c2h_sts_valid),
        .s_data({cpm_c2h_sts_drop, cpm_c2h_sts_qid, cpm_c2h_sts_last, cpm_c2h_sts_status_cmp, cpm_c2h_sts_error}),
        .m_valid(rm_c2h_sts_valid),
        .m_data({rm_c2h_sts_drop, rm_c2h_sts_qid, rm_c2h_sts_last, rm_c2h_sts_status_cmp, rm_c2h_sts_error}));

    // ---- descriptor status and queue status (no back-pressure) ----
    // The CPM's context engine emits these on context programming (queue
    // enable / invalidate) and holds them until rdy. PG347: when unused, rdy
    // must be tied to 1. A partition that ties its rdy to 0 (the default RM)
    // or is being reconfigured would otherwise stall every context command
    // (IND_CTXT_CMD busy, driver "init ctxt write failed"). So the static
    // always accepts and forwards them valid-only: the RM must take a word
    // every cycle valid is high or ignore the channel. rm_*_rdy is unused.
    localparam integer W_DSC = 1 + 1 + 1 + 12 + 16 + 16 + 3 + 1 + 1 + 1 + 1;
    assign cpm_dsc_sts_rdy = 1'b1;
    qsp_delay #(.W(W_DSC), .N(STAGES)) u_dsc_sts (
        .clk(clk), .rstn(rstn),
        .s_valid(cpm_dsc_sts_valid),
        .s_data({cpm_dsc_sts_byp, cpm_dsc_sts_dir, cpm_dsc_sts_mm, cpm_dsc_sts_qid, cpm_dsc_sts_avl,
                 cpm_dsc_sts_pidx, cpm_dsc_sts_port_id, cpm_dsc_sts_qinv, cpm_dsc_sts_irq_arm,
                 cpm_dsc_sts_error, cpm_dsc_sts_qen}),
        .m_valid(rm_dsc_sts_valid),
        .m_data({rm_dsc_sts_byp, rm_dsc_sts_dir, rm_dsc_sts_mm, rm_dsc_sts_qid, rm_dsc_sts_avl,
                 rm_dsc_sts_pidx, rm_dsc_sts_port_id, rm_dsc_sts_qinv, rm_dsc_sts_irq_arm,
                 rm_dsc_sts_error, rm_dsc_sts_qen}));

    assign cpm_qsts_rdy = 1'b1;
    qsp_delay #(.W(8 + 64 + 3 + 13), .N(STAGES)) u_qsts (
        .clk(clk), .rstn(rstn),
        .s_valid(cpm_qsts_vld),
        .s_data({cpm_qsts_op, cpm_qsts_data, cpm_qsts_port_id, cpm_qsts_qid}),
        .m_valid(rm_qsts_vld),
        .m_data({rm_qsts_op, rm_qsts_data, rm_qsts_port_id, rm_qsts_qid}));

    // ---- descriptor credits ----
    qsp_pipe #(.W(1 + 16 + 12 + 1), .N(STAGES)) u_crdt (
        .clk(clk), .rstn(rstn),
        .s_valid(rm_crdt_valid), .s_ready(rm_crdt_rdy),
        .s_data({rm_crdt_dir, rm_crdt_crdt, rm_crdt_qid, rm_crdt_fence}),
        .m_valid(cpm_crdt_valid), .m_ready(cpm_crdt_rdy),
        .m_data({cpm_crdt_dir, cpm_crdt_crdt, cpm_crdt_qid, cpm_crdt_fence}));

    // ---- user interrupt ----
    // request towards the CPM: STAGES-1 plain stages, then the gating stage
    wire        irq_v;
    wire [23:0] irq_d;
    qsp_delay #(.W(24), .N(STAGES - 1)) u_irq_req (
        .clk(clk), .rstn(rstn),
        .s_valid(rm_irq_valid), .s_data({rm_irq_vec, rm_irq_fnc}),
        .m_valid(irq_v), .m_data(irq_d));

    reg irq_blocked;    // ack/fail seen; wait for the RM to drop valid
    always @(posedge clk) begin
        if (!rstn) begin
            cpm_irq_valid <= 1'b0;
            irq_blocked   <= 1'b0;
        end else begin
            if (cpm_irq_valid && (cpm_irq_ack || cpm_irq_fail))
                irq_blocked <= 1'b1;
            else if (!irq_v)
                irq_blocked <= 1'b0;
            cpm_irq_valid <= irq_v && !irq_blocked && !(cpm_irq_valid && (cpm_irq_ack || cpm_irq_fail));
        end
    end
    always @(posedge clk) begin
        if (!cpm_irq_valid)            // hold vec/fnc while the CPM sees valid
            {cpm_irq_vec, cpm_irq_fnc} <= irq_d;
    end

    // ack/fail back to the RM
    reg [STAGES-1:0] ack_sr, fail_sr;
    always @(posedge clk) begin
        if (!rstn) begin
            ack_sr  <= {STAGES{1'b0}};
            fail_sr <= {STAGES{1'b0}};
        end else begin
            ack_sr  <= (ack_sr  << 1) | (cpm_irq_valid & cpm_irq_ack);
            fail_sr <= (fail_sr << 1) | (cpm_irq_valid & cpm_irq_fail);
        end
    end
    assign rm_irq_ack  = ack_sr[STAGES-1];
    assign rm_irq_fail = fail_sr[STAGES-1];

endmodule

// N fully registered valid/ready stages.
module qsp_pipe #(
    parameter integer W = 8,
    parameter integer N = 2
) (
    input  wire         clk,
    input  wire         rstn,
    input  wire         s_valid,
    output wire         s_ready,
    input  wire [W-1:0] s_data,
    output wire         m_valid,
    input  wire         m_ready,
    output wire [W-1:0] m_data
);
    wire         v [0:N];
    wire         r [0:N];
    wire [W-1:0] d [0:N];
    assign v[0] = s_valid;
    assign d[0] = s_data;
    assign s_ready = r[0];
    assign m_valid = v[N];
    assign m_data  = d[N];
    assign r[N] = m_ready;
    genvar i;
    generate
        for (i = 0; i < N; i = i + 1) begin : g_stage
            qsp_skid #(.W(W)) u_skid (
                .clk(clk), .rstn(rstn),
                .s_valid(v[i]), .s_ready(r[i]), .s_data(d[i]),
                .m_valid(v[i+1]), .m_ready(r[i+1]), .m_data(d[i+1]));
        end
    endgenerate
endmodule

// Skid buffer: output and ready both registered, full throughput.
module qsp_skid #(
    parameter integer W = 8
) (
    input  wire         clk,
    input  wire         rstn,
    input  wire         s_valid,
    output wire         s_ready,
    input  wire [W-1:0] s_data,
    output wire         m_valid,
    input  wire         m_ready,
    output wire [W-1:0] m_data
);
    reg         v0, v1;     // v0: output register, v1: skid register
    reg [W-1:0] d0, d1;
    wire        take = m_ready || !v0;   // output register free this cycle
    assign m_valid = v0;
    assign m_data  = d0;
    assign s_ready = !v1;
    always @(posedge clk) begin
        if (!rstn) begin
            v0 <= 1'b0;
            v1 <= 1'b0;
        end else if (take) begin
            v0 <= v1 || s_valid;
            v1 <= 1'b0;
        end else if (s_valid && !v1) begin
            v1 <= 1'b1;
        end
    end
    always @(posedge clk) begin
        if (take)
            d0 <= v1 ? d1 : s_data;
        if (!take && !v1)
            d1 <= s_data;
    end
endmodule

// N plain register stages, valid reset (for interfaces without ready).
module qsp_delay #(
    parameter integer W = 8,
    parameter integer N = 2
) (
    input  wire         clk,
    input  wire         rstn,
    input  wire         s_valid,
    input  wire [W-1:0] s_data,
    output wire         m_valid,
    output wire [W-1:0] m_data
);
    generate
        if (N == 0) begin : g_wire
            assign m_valid = s_valid;
            assign m_data  = s_data;
        end else begin : g_regs
            reg [N-1:0] v;
            reg [W-1:0] d [0:N-1];
            integer k;
            always @(posedge clk) begin
                if (!rstn) v <= {N{1'b0}};
                else       v <= (v << 1) | s_valid;
            end
            always @(posedge clk) begin
                d[0] <= s_data;
                for (k = 1; k < N; k = k + 1) d[k] <= d[k-1];
            end
            assign m_valid = v[N-1];
            assign m_data  = d[N-1];
        end
    endgenerate
endmodule
