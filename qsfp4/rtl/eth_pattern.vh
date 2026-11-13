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

// Frame format shared by eth_gen and eth_chk. Frame bytes on the 1024-bit
// stream, byte 0 in tdata[7:0], FCS not included (the DCMAC adds and strips it):
//
//    0.. 5  destination MAC (byte 0 first on the wire)
//    6..11  source MAC
//   12..13  EtherType, big endian
//   14      id of the sending port
//   15      0
//   16..23  sequence number, little endian
//   24..25  frame length in bytes, little endian
//   26..31  0
//   32..    payload
//
// The header fills lanes 0..3 of the sixteen 64-bit lanes of the first beat.
// Payload: a 64-bit xorshift state per beat, in lane l rotated by 5*l and
// XORed with a lane constant, so swapped lanes show. The state of the first
// beat comes from SEED and the frame's own sequence number, so a lost frame
// does not disturb the check of the next.
function automatic [63:0] pat_first(input [63:0] seed, input [63:0] seq);
    pat_first = seed ^ {~seq[31:0], seq[31:0]};
endfunction

function automatic [63:0] pat_next(input [63:0] x);
    reg [63:0] a, b;
    begin
        a = x ^ (x << 13);
        b = a ^ (a >> 7);
        pat_next = b ^ (b << 17);
    end
endfunction

function automatic [1023:0] pat_beat(input [63:0] x);
    integer l, r;
    reg [127:0] d;
    begin
        for (l = 0; l < 16; l = l + 1) begin
            r = (5 * l) % 64;
            d = {x, x} << r;
            pat_beat[l*64 +: 64] = d[127:64] ^ {4{l[3:0], l[3:0], l[3:0], l[3:0]}};
        end
    end
endfunction

function automatic [191:0] pat_hdr(input [47:0] dst, input [47:0] src, input [15:0] etype,
                                   input [7:0] id);
    pat_hdr = {64'd0,                                        // lane 2: sequence number goes here
               8'd0, id, etype[7:0], etype[15:8],            // lane 1 high half
               src[7:0], src[15:8], src[23:16], src[31:24],  // lane 1 low half
               src[39:32], src[47:40],                       // lane 0 top
               dst[7:0], dst[15:8], dst[23:16], dst[31:24], dst[39:32], dst[47:40]};
endfunction

// tkeep of a beat with n bytes left in the frame (n >= 1)
function automatic [127:0] pat_keep(input [15:0] n);
    pat_keep = (n >= 16'd128) ? {128{1'b1}} : ~({128{1'b1}} << n[6:0]);
endfunction
