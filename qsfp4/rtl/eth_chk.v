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

// Ethernet frame checker (eth_pattern.vh), in the node clock, takes a beat every
// cycle. dcmac_rx_path in front has already dropped bad-FCS frames; those show
// here as lost frames and in its drop counter.
//
//   frames, bytes   frames seen, their length fields
//   lost            sequence numbers skipped
//   reorder         sequence numbers that went backwards
//   hdr_err         frames with a wrong MAC, EtherType or port id
//   len_err         beats whose tkeep / tlast disagree with the length field
//   pay_err         payload beats with a mismatch
//
// The first error of any kind is latched until clear: kind, lane, sequence
// number, beat, expected and received lane.
//
// Stages: 0 input registers, 1 expected beat, 2 lane compare, 3 count. Frame
// tracking is in stage 0 -> 1, so one-beat frames can follow back to back.
module eth_chk (
    input  wire          clk,
    input  wire          resetn,

    input  wire          clear,
    input  wire [47:0]   dst,
    input  wire [47:0]   src,
    input  wire [15:0]   etype,
    input  wire [7:0]    id,
    input  wire [63:0]   seed,

    output reg  [63:0]   frames,
    output reg  [63:0]   bytes,
    output reg  [63:0]   lost,
    output reg  [31:0]   reorder,
    output reg  [31:0]   hdr_err,
    output reg  [31:0]   len_err,
    output reg  [31:0]   pay_err,
    output reg  [7:0]    last_id,           // of the last frame received
    output reg  [47:0]   last_src,

    output reg           first_valid,
    output reg  [2:0]    first_kind,        // [0] payload [1] header [2] length
    output reg  [3:0]    first_lane,
    output reg  [15:0]   first_beat,
    output reg  [63:0]   first_seq,
    output reg  [63:0]   first_exp,
    output reg  [63:0]   first_got,

    input  wire [1023:0] s_tdata,
    input  wire [127:0]  s_tkeep,
    input  wire          s_tlast,
    input  wire          s_tvalid,
    output wire          s_tready
);
    `include "eth_pattern.vh"

    assign s_tready = 1'b1;

    // ---- stage 0 ----
    reg [1023:0] d0;
    reg [127:0]  k0;
    reg          l0, v0;

    always @(posedge clk) begin
        v0 <= resetn && s_tvalid;
        d0 <= s_tdata;
        k0 <= s_tkeep;
        l0 <= s_tlast;
    end

    // ---- frame tracking, stage 1 ----
    reg         first;                      // next beat starts a frame
    reg [15:0]  rem;
    reg [15:0]  beat_no;
    reg [63:0]  x;
    reg [63:0]  cur_seq;

    wire [63:0]  seq_in = d0[191:128];
    wire [15:0]  len_in = d0[207:192];
    wire [15:0]  cur_len = first ? len_in : rem;
    wire [63:0]  cur_x = first ? pat_first(seed, seq_in) : x;
    wire [1023:0] pay = pat_beat(cur_x);
    wire [191:0] hdr = pat_hdr(dst, src, etype, id);
    wire [127:0] k_exp = pat_keep(cur_len);

    reg [1023:0] d1, e1;
    reg [127:0]  k1;
    reg          v1, f1, le1;               // valid, first beat, length error
    reg [63:0]   s1;
    reg [15:0]   b1, n1;
    reg [64:0]   gap1;                      // first beat: seq - next expected
    reg          sync;                      // a frame was seen since clear
    reg [63:0]   next_seq;

    always @(posedge clk) begin
        v1 <= v0;
        f1 <= v0 && first;
        d1 <= d0;
        k1 <= k0;
        n1 <= cur_len;
        b1 <= first ? 16'd0 : beat_no;
        s1 <= first ? seq_in : cur_seq;
        // seq and length are compared with the frame's own, so their check is
        // the gap count and the length check; the rest of lane 3 must be 0
        e1 <= first ? {pay[1023:256], 48'd0, len_in, seq_in, hdr[127:0]} : pay;
        le1 <= v0 && (l0 != (cur_len <= 16'd128) || k0 != k_exp || cur_len == 16'd0);
        gap1 <= {1'b0, seq_in} - {1'b0, next_seq};

        if (!resetn || clear) begin
            first <= 1'b1;
            sync <= 1'b0;
            next_seq <= 64'd0;
        end else if (v0) begin
            first <= l0;
            beat_no <= first ? 16'd1 : beat_no + 16'd1;
            x <= pat_next(cur_x);
            if (first) begin
                cur_seq <= seq_in;
                next_seq <= seq_in + 64'd1;
                sync <= 1'b1;
                rem <= len_in - 16'd128;
            end else begin
                rem <= rem - 16'd128;
            end
        end
    end

    reg sync1;
    always @(posedge clk)
        sync1 <= sync;

    // ---- stage 2: lane compare ----
    reg [15:0]   lane_err;
    reg [1023:0] d2, e2;
    reg          v2, f2, le2;
    reg [63:0]   s2;
    reg [15:0]   b2, n2;
    reg [64:0]   gap2;
    reg          sync2;

    always @(posedge clk) begin
        v2 <= v1;
        f2 <= f1;
        le2 <= le1;
        d2 <= d1;
        e2 <= e1;
        s2 <= s1;
        b2 <= b1;
        n2 <= n1;
        gap2 <= gap1;
        sync2 <= sync1;
    end

    // bytes without tkeep do not count; a wrong tkeep is a length error
    genvar g, h;
    generate
        for (g = 0; g < 16; g = g + 1) begin : g_lane
            wire [63:0] mask;
            for (h = 0; h < 8; h = h + 1) begin : g_byte
                assign mask[h*8 +: 8] = {8{k1[g*8 + h]}};
            end
            always @(posedge clk)
                lane_err[g] <= |((d1[g*64 +: 64] ^ e1[g*64 +: 64]) & mask);
        end
    endgenerate

    // ---- stage 3: counters, first error ----
    wire        hdr_bad = v2 && f2 && (lane_err[3:0] != 4'd0);
    wire        pay_bad = v2 && (f2 ? (lane_err[15:4] != 12'd0) : (lane_err != 16'd0));
    wire [2:0]  kind = {le2, hdr_bad, pay_bad};
    reg  [3:0]  lane_sel;

    integer n;
    always @* begin
        lane_sel = 4'd0;
        for (n = 15; n >= 0; n = n - 1)
            if (lane_err[n])
                lane_sel = n[3:0];
    end

    always @(posedge clk) begin
        if (!resetn || clear) begin
            frames <= 64'd0;
            bytes <= 64'd0;
            lost <= 64'd0;
            reorder <= 32'd0;
            hdr_err <= 32'd0;
            len_err <= 32'd0;
            pay_err <= 32'd0;
            first_valid <= 1'b0;
            first_kind <= 3'd0;
            first_lane <= 4'd0;
            first_beat <= 16'd0;
            first_seq <= 64'd0;
            first_exp <= 64'd0;
            first_got <= 64'd0;
            last_id <= 8'd0;
            last_src <= 48'd0;
        end else begin
            if (v2 && f2) begin
                frames <= frames + 64'd1;
                bytes <= bytes + n2;
                last_id <= d2[119:112];
                last_src <= {d2[55:48], d2[63:56], d2[71:64], d2[79:72], d2[87:80], d2[95:88]};
                if (sync2 && gap2 != 65'd0) begin
                    if (gap2[64])
                        reorder <= reorder + 32'd1;
                    else
                        lost <= lost + gap2[63:0];
                end
            end
            if (hdr_bad)
                hdr_err <= hdr_err + 32'd1;
            if (pay_bad)
                pay_err <= pay_err + 32'd1;
            if (le2)
                len_err <= len_err + 32'd1;
            if (|kind && !first_valid) begin
                first_valid <= 1'b1;
                first_kind <= kind;
                first_lane <= lane_sel;
                first_beat <= b2;
                first_seq <= s2;
                first_exp <= e2[lane_sel*64 +: 64];
                first_got <= d2[lane_sel*64 +: 64];
            end
        end
    end
endmodule
