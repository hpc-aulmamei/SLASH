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

// RX segments 2 and 3 of a DCMAC port at 100G. A 100CAUI-4 port owns two
// segments (port 0: 0-1, port 2: 4-5); the next two belong to the disabled
// port after it, and the DCMAC does not clear their ena / sop / eop. They keep
// the values of the last 200G cycle, so the seg_to_unseg converter (built for
// four segments, valid = ena & tvalid) splices those frozen segments into every
// 100G cycle: 32 good bytes, then stale ones, beats doubled, and extra frames
// when a stale eop is stuck. Seen on hardware with build qsfp6. Masked here
// while the DCMAC is at 100G (half, from PHY_CTRL[8] of its port-0 tester).
module dcmac_rx_seg_mask (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk CLK" *)
    input  wire clk,                // the converter's segment clock
    input  wire half,               // any clock, changes only during a rate switch
    input  wire ena2_in, sop2_in, eop2_in,
    input  wire ena3_in, sop3_in, eop3_in,
    output wire ena2, sop2, eop2,
    output wire ena3, sop3, eop3
);
    wire half_s;
    xpm_cdc_single #(.DEST_SYNC_FF(4), .INIT_SYNC_FF(0), .SIM_ASSERT_CHK(0), .SRC_INPUT_REG(0)) inst_half_cdc (
        .src_clk(1'b0), .src_in(half), .dest_clk(clk), .dest_out(half_s));

    assign ena2 = ena2_in && !half_s;
    assign sop2 = sop2_in && !half_s;
    assign eop2 = eop2_in && !half_s;
    assign ena3 = ena3_in && !half_s;
    assign sop3 = sop3_in && !half_s;
    assign eop3 = eop3_in && !half_s;
endmodule
