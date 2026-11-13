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

// Verilog top of lane_test for the service-layer block design: one AXI4-Stream
// interface per NoC lane (sNN_axis from the user region, mNN_axis to it), so
// they connect to the dummy_noc_NN / dummy_noc_m_NN boundary cells.
module lane_test_bd (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axil:s00_axis:s01_axis:s02_axis:s03_axis:s04_axis:s05_axis:s06_axis:s07_axis:m00_axis:m01_axis:m02_axis:m03_axis:m04_axis:m05_axis:m06_axis:m07_axis, ASSOCIATED_RESET resetn" *)
    input  wire         clk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 resetn RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire         resetn,

    input  wire [15:0]  s_axil_awaddr,
    input  wire [2:0]   s_axil_awprot,
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
    input  wire [2:0]   s_axil_arprot,
    input  wire         s_axil_arvalid,
    output wire         s_axil_arready,
    output wire [31:0]  s_axil_rdata,
    output wire [1:0]   s_axil_rresp,
    output wire         s_axil_rvalid,
    input  wire         s_axil_rready,

    input  wire [511:0] s00_axis_tdata,
    input  wire [63:0]  s00_axis_tkeep,
    input  wire         s00_axis_tlast,
    input  wire         s00_axis_tvalid,
    output wire         s00_axis_tready,
    input  wire [511:0] s01_axis_tdata,
    input  wire [63:0]  s01_axis_tkeep,
    input  wire         s01_axis_tlast,
    input  wire         s01_axis_tvalid,
    output wire         s01_axis_tready,
    input  wire [511:0] s02_axis_tdata,
    input  wire [63:0]  s02_axis_tkeep,
    input  wire         s02_axis_tlast,
    input  wire         s02_axis_tvalid,
    output wire         s02_axis_tready,
    input  wire [511:0] s03_axis_tdata,
    input  wire [63:0]  s03_axis_tkeep,
    input  wire         s03_axis_tlast,
    input  wire         s03_axis_tvalid,
    output wire         s03_axis_tready,
    input  wire [511:0] s04_axis_tdata,
    input  wire [63:0]  s04_axis_tkeep,
    input  wire         s04_axis_tlast,
    input  wire         s04_axis_tvalid,
    output wire         s04_axis_tready,
    input  wire [511:0] s05_axis_tdata,
    input  wire [63:0]  s05_axis_tkeep,
    input  wire         s05_axis_tlast,
    input  wire         s05_axis_tvalid,
    output wire         s05_axis_tready,
    input  wire [511:0] s06_axis_tdata,
    input  wire [63:0]  s06_axis_tkeep,
    input  wire         s06_axis_tlast,
    input  wire         s06_axis_tvalid,
    output wire         s06_axis_tready,
    input  wire [511:0] s07_axis_tdata,
    input  wire [63:0]  s07_axis_tkeep,
    input  wire         s07_axis_tlast,
    input  wire         s07_axis_tvalid,
    output wire         s07_axis_tready,
    output wire [511:0] m00_axis_tdata,
    output wire [63:0]  m00_axis_tkeep,
    output wire         m00_axis_tlast,
    output wire         m00_axis_tvalid,
    input  wire         m00_axis_tready,
    output wire [511:0] m01_axis_tdata,
    output wire [63:0]  m01_axis_tkeep,
    output wire         m01_axis_tlast,
    output wire         m01_axis_tvalid,
    input  wire         m01_axis_tready,
    output wire [511:0] m02_axis_tdata,
    output wire [63:0]  m02_axis_tkeep,
    output wire         m02_axis_tlast,
    output wire         m02_axis_tvalid,
    input  wire         m02_axis_tready,
    output wire [511:0] m03_axis_tdata,
    output wire [63:0]  m03_axis_tkeep,
    output wire         m03_axis_tlast,
    output wire         m03_axis_tvalid,
    input  wire         m03_axis_tready,
    output wire [511:0] m04_axis_tdata,
    output wire [63:0]  m04_axis_tkeep,
    output wire         m04_axis_tlast,
    output wire         m04_axis_tvalid,
    input  wire         m04_axis_tready,
    output wire [511:0] m05_axis_tdata,
    output wire [63:0]  m05_axis_tkeep,
    output wire         m05_axis_tlast,
    output wire         m05_axis_tvalid,
    input  wire         m05_axis_tready,
    output wire [511:0] m06_axis_tdata,
    output wire [63:0]  m06_axis_tkeep,
    output wire         m06_axis_tlast,
    output wire         m06_axis_tvalid,
    input  wire         m06_axis_tready,
    output wire [511:0] m07_axis_tdata,
    output wire [63:0]  m07_axis_tkeep,
    output wire         m07_axis_tlast,
    output wire         m07_axis_tvalid,
    input  wire         m07_axis_tready,

    // DAQ lanes 4 and 6 (lane_test TAP0 / TAP1): to and from a DAQ node (unused in the four-port test)
    output wire [1:0]    tap_valid,
    output wire [1:0]    tap_last,
    output wire [127:0]  tap_keep,
    output wire [1023:0] tap_data,
    input  wire [1:0]    inj_valid,
    output wire [1:0]    inj_ready,
    input  wire [1023:0] inj_data
);
    lane_test #(.LANES(8), .TAP0(4), .TAP1(6)) inst (
        .clk(clk), .resetn(resetn),
        .s_axil_awaddr(s_axil_awaddr), .s_axil_awvalid(s_axil_awvalid), .s_axil_awready(s_axil_awready),
        .s_axil_wdata(s_axil_wdata), .s_axil_wstrb(s_axil_wstrb), .s_axil_wvalid(s_axil_wvalid),
        .s_axil_wready(s_axil_wready), .s_axil_bresp(s_axil_bresp), .s_axil_bvalid(s_axil_bvalid),
        .s_axil_bready(s_axil_bready), .s_axil_araddr(s_axil_araddr), .s_axil_arvalid(s_axil_arvalid),
        .s_axil_arready(s_axil_arready), .s_axil_rdata(s_axil_rdata), .s_axil_rresp(s_axil_rresp),
        .s_axil_rvalid(s_axil_rvalid), .s_axil_rready(s_axil_rready),
        .s_tdata({s07_axis_tdata, s06_axis_tdata, s05_axis_tdata, s04_axis_tdata, s03_axis_tdata, s02_axis_tdata, s01_axis_tdata, s00_axis_tdata}),
        .s_tkeep({s07_axis_tkeep, s06_axis_tkeep, s05_axis_tkeep, s04_axis_tkeep, s03_axis_tkeep, s02_axis_tkeep, s01_axis_tkeep, s00_axis_tkeep}),
        .s_tlast({s07_axis_tlast, s06_axis_tlast, s05_axis_tlast, s04_axis_tlast, s03_axis_tlast, s02_axis_tlast, s01_axis_tlast, s00_axis_tlast}),
        .s_tvalid({s07_axis_tvalid, s06_axis_tvalid, s05_axis_tvalid, s04_axis_tvalid, s03_axis_tvalid, s02_axis_tvalid, s01_axis_tvalid, s00_axis_tvalid}),
        .s_tready({s07_axis_tready, s06_axis_tready, s05_axis_tready, s04_axis_tready, s03_axis_tready, s02_axis_tready, s01_axis_tready, s00_axis_tready}),
        .m_tdata({m07_axis_tdata, m06_axis_tdata, m05_axis_tdata, m04_axis_tdata, m03_axis_tdata, m02_axis_tdata, m01_axis_tdata, m00_axis_tdata}),
        .m_tkeep({m07_axis_tkeep, m06_axis_tkeep, m05_axis_tkeep, m04_axis_tkeep, m03_axis_tkeep, m02_axis_tkeep, m01_axis_tkeep, m00_axis_tkeep}),
        .m_tlast({m07_axis_tlast, m06_axis_tlast, m05_axis_tlast, m04_axis_tlast, m03_axis_tlast, m02_axis_tlast, m01_axis_tlast, m00_axis_tlast}),
        .m_tvalid({m07_axis_tvalid, m06_axis_tvalid, m05_axis_tvalid, m04_axis_tvalid, m03_axis_tvalid, m02_axis_tvalid, m01_axis_tvalid, m00_axis_tvalid}),
        .m_tready({m07_axis_tready, m06_axis_tready, m05_axis_tready, m04_axis_tready, m03_axis_tready, m02_axis_tready, m01_axis_tready, m00_axis_tready}),
        .tap_valid(tap_valid), .tap_last(tap_last), .tap_keep(tap_keep), .tap_data(tap_data),
        .inj_valid(inj_valid), .inj_ready(inj_ready), .inj_data(inj_data)
    );
endmodule
