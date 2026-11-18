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

// Local reset pipeline for the service-layer template's AXI register slices
// and passthroughs (nictest_service_layer.tcl). The template drives their
// aresetn from one BUFG_FABRIC net (util_ds_buf_0, about 1270 loads). On the
// BDF static that net crosses from SLR0 to SLR1 and missed timing at 250 MHz
// by up to 124 ps. The last stage of this pipeline replicates (max_fanout),
// so each copy can be placed near its loads. Assertion and release are both
// delayed by three service_clk cycles.
`timescale 1ns / 1ps
module rst_pipe_bd (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_RESET rstn_in:rstn_out" *)
    input  wire clk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 rstn_in RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire rstn_in,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 rstn_out RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    output wire rstn_out
);
    (* shreg_extract = "no" *) reg [1:0] pipe = 2'b00;
    (* shreg_extract = "no", max_fanout = 100 *) reg out = 1'b0;
    always @(posedge clk) begin
        pipe <= {pipe[0], rstn_in};
        out  <= pipe[1];
    end
    assign rstn_out = out;
endmodule
