// Default service-layer RM: ends of the QDMA NoC streams with nothing behind
// them. Streams from the static are drained (tready 1), streams to the static
// stay idle (tvalid 0). See qdma_noc_static.v for the formats.
`timescale 1ns / 1ps
module qdma_noc_rm_stub (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_h2c0:s_h2c1:s_evt:m_c2h0:m_c2h1:m_cmpt:m_cmd" *)
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
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_cmd TLAST" *)  output wire         m_cmd_tlast
);
    assign s_h2c0_tready = 1'b1;
    assign s_h2c1_tready = 1'b1;
    assign s_evt_tready = 1'b1;
    assign m_c2h0_tvalid = 1'b0;
    assign m_c2h0_tdata  = 512'd0;
    assign m_c2h0_tkeep  = {64{1'b1}};
    assign m_c2h0_tlast  = 1'b1;
    assign m_c2h1_tvalid = 1'b0;
    assign m_c2h1_tdata  = 512'd0;
    assign m_c2h1_tkeep  = {64{1'b1}};
    assign m_c2h1_tlast  = 1'b1;
    assign m_cmpt_tvalid = 1'b0;
    assign m_cmpt_tdata  = 512'd0;
    assign m_cmpt_tkeep  = {64{1'b1}};
    assign m_cmpt_tlast  = 1'b1;
    assign m_cmd_tvalid = 1'b0;
    assign m_cmd_tdata  = 256'd0;
    assign m_cmd_tkeep  = {32{1'b1}};
    assign m_cmd_tlast  = 1'b1;
endmodule
