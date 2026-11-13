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

// Ethernet frame generator (eth_pattern.vh), in the node clock, 1024 bits a beat.
//
//   start   sequence number back to 0, frames until `count` (0: until stop)
//   stop    finishes the frame in progress
//   length  len_min + (random & len_mask), at most len_max; len_mask 0: fixed
//   gap     idle cycles between frames
//
// A start while running is ignored.
module eth_gen (
    input  wire          clk,
    input  wire          resetn,

    input  wire          start,
    input  wire          stop,
    input  wire          clear,             // frame and byte counters
    input  wire [15:0]   len_min,
    input  wire [15:0]   len_max,
    input  wire [15:0]   len_mask,
    input  wire [15:0]   gap,
    input  wire [63:0]   count,
    input  wire [47:0]   dst,
    input  wire [47:0]   src,
    input  wire [15:0]   etype,
    input  wire [7:0]    id,
    input  wire [63:0]   seed,

    output reg           running,
    output reg  [63:0]   frames,
    output reg  [63:0]   bytes,

    output wire [1023:0] m_tdata,
    output wire [127:0]  m_tkeep,
    output wire          m_tlast,
    output wire          m_tvalid,
    input  wire          m_tready
);
    `include "eth_pattern.vh"

    // lengths, one per frame: LFSR -> sum -> clip, always a value ready
    reg  [31:0] lfsr;
    reg  [15:0] len_sum;
    reg  [15:0] len_next;
    wire        take;                       // len_next used this cycle
    wire [31:0] lfsr_n = {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};

    always @(posedge clk) begin
        if (!resetn || !running) begin
            lfsr <= seed[31:0] | 32'd1;
            len_sum <= len_min;
            len_next <= len_min;
        end else if (take) begin
            lfsr <= lfsr_n;
            len_sum <= len_min + (lfsr[15:0] & len_mask);
            len_next <= (len_sum > len_max) ? len_max : len_sum;
        end
    end

    // beat being produced
    reg  [63:0]  seq;
    reg  [63:0]  left;                      // frames left, count != 0
    reg          limited;
    reg  [15:0]  rem;                       // bytes left in the frame
    reg          in_frame;
    reg  [63:0]  x;                         // pattern state of the next beat
    reg  [15:0]  gap_cnt;
    reg          stopping;

    wire [1023:0] s_tdata;
    wire [127:0]  s_tkeep;
    wire          s_tlast;
    wire          s_tvalid;
    wire          s_tready;

    wire        may_start = running && !in_frame && gap_cnt == 16'd0 && !stopping;
    assign      take = may_start && s_tready;
    wire [15:0] cur_len = in_frame ? rem : len_next;
    wire [63:0] cur_x = in_frame ? x : pat_first(seed, seq);
    wire        last_beat = cur_len <= 16'd128;

    wire [1023:0] pay = pat_beat(cur_x);
    wire [191:0]  hdr = pat_hdr(dst, src, etype, id);

    assign s_tvalid = in_frame || may_start;
    assign s_tlast = last_beat;
    assign s_tkeep = pat_keep(cur_len);
    assign s_tdata = in_frame ? pay
                   : {pay[1023:256], 48'd0, len_next, seq, hdr[127:0]};

    wire beat = s_tvalid && s_tready;

    always @(posedge clk) begin
        if (!resetn) begin
            running <= 1'b0;
            in_frame <= 1'b0;
            stopping <= 1'b0;
            gap_cnt <= 16'd0;
            seq <= 64'd0;
            frames <= 64'd0;
            bytes <= 64'd0;
        end else begin
            if (start && !running) begin
                running <= 1'b1;
                stopping <= 1'b0;
                seq <= 64'd0;
                left <= count;
                limited <= count != 64'd0;
                gap_cnt <= 16'd0;
            end
            if (running && stop)
                stopping <= 1'b1;
            if (clear) begin
                frames <= 64'd0;
                bytes <= 64'd0;
            end
            if (gap_cnt != 16'd0)
                gap_cnt <= gap_cnt - 16'd1;

            if (beat) begin
                x <= pat_next(cur_x);
                if (!in_frame) begin
                    bytes <= bytes + len_next;      // the frame's bytes, once
                    seq <= seq + 64'd1;
                end
                if (last_beat) begin
                    in_frame <= 1'b0;
                    frames <= frames + 64'd1;
                    gap_cnt <= gap;
                    if (limited) begin
                        left <= left - 64'd1;
                        if (left == 64'd1)
                            stopping <= 1'b1;
                    end
                end else begin
                    in_frame <= 1'b1;
                    rem <= cur_len - 16'd128;
                end
            end
            if (running && stopping && !in_frame && !s_tvalid)
                running <= 1'b0;
        end
    end

    // the data registers load on tready, from far away
    axis_skid #(
        .DATA_BITS(1024 + 128 + 1),
        .SLICES(8)
    ) inst_skid (
        .clk(clk),
        .resetn(resetn),
        .s_valid(s_tvalid),
        .s_ready(s_tready),
        .s_data({s_tlast, s_tkeep, s_tdata}),
        .m_valid(m_tvalid),
        .m_ready(m_tready),
        .m_data({m_tlast, m_tkeep, m_tdata})
    );
endmodule
