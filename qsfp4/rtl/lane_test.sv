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

// Traffic test of the NoC stream lanes between the user region and the service
// layer (S_DCMAC_INISk / M_DCMAC_INISk, 512-bit AXI4-Stream), service-layer end,
// on service_clk (250 MHz).
//
// Per lane k:
//   generator  -> m_axis[k] (to the user region): beats of lane_pattern(k, seq),
//                 seq counting from 0, frames of GEN_LEN beats with GEN_GAP idle
//                 cycles between them, GEN_COUNT beats (0: until stopped)
//   checker    <- s_axis[k] (from the user region), always ready: every beat
//                 must be lane_pattern(k, s) for its own word 0 s, with full
//                 TKEEP, and s must be the previous beat's s + 1 (0 after a
//                 clear). A mismatch counts one error and the next beat is
//                 expected to follow the one received.
// Both ends keep the first and last beat cycle, so the rate is
// beats * 512 / ((last - first + 1) / f).
//
// The DAQ lanes TAP0 and TAP1 (version 3): their NSU registers also go out on
// tap_* (to daq_lane in the RoCE node), and beats on inj_* (daq_lane's
// control beats to the user-region daq_gen) go to the user region in front of
// that lane's generator: an injected beat takes the next free cycle, the
// generator waits (counted as stall). Do not run the lane test generator on a
// lane while a daq_gen there is in use.
//
// Timing: at 250 MHz a path between fabric and a NoC NMU/NSU pin has about
// 3 ns of route budget (the NoC clock arrives ~0.8 ns early), so the NSU
// outputs land in registers with no enable and no logic, s_tready is constant,
// and the NMU is fed by a register slice whose only other input is its own
// m_tready. Version 1 (build 7) had logic on both sides and failed by 0.6 ns.
//
// Registers (AXI4-Lite, 32 bit, whole-word writes only; 64-bit values are LO,
// HI and only stable when the lane is idle):
//   0x000 ID 0x4C414E45 ("LANE")    0x004 [7:0] lanes, [31:16] version
//   0x008 CYCLES                    0x010 START (W, lane mask)
//   0x014 STOP (W, lane mask, at the next frame boundary)
//   0x018 CLEAR (W, lane mask: generator and checker counters)
//   0x100 + 0x80 k: lane k
//     +0x00 GEN_STATE [0] running   +0x08 GEN_COUNT   +0x10 GEN_LEN   +0x14 GEN_GAP
//     +0x18 GEN_SENT   +0x20 GEN_STALL (cycles valid and not ready)
//     +0x28 GEN_FIRST  +0x30 GEN_LAST
//     +0x40 CHK_BEATS  +0x48 CHK_FRAMES  +0x50 CHK_ERRORS
//     +0x54 CHK_ERR_BEAT (beat number of the first mismatch, low 32 bits)
//     +0x58 CHK_ERR_GOT / +0x5C CHK_ERR_EXP (word 0 of it and the expected
//           sequence number, low 32 bits)
//     +0x60 CHK_FIRST  +0x68 CHK_LAST
module lane_test #(
    parameter integer LANES = 8,
    parameter integer TAP0 = 4,
    parameter integer TAP1 = 6
) (
    input  wire                    clk,
    input  wire                    resetn,

    input  wire [15:0]             s_axil_awaddr,
    input  wire                    s_axil_awvalid,
    output reg                     s_axil_awready,
    input  wire [31:0]             s_axil_wdata,
    input  wire [3:0]              s_axil_wstrb,
    input  wire                    s_axil_wvalid,
    output reg                     s_axil_wready,
    output wire [1:0]              s_axil_bresp,
    output reg                     s_axil_bvalid,
    input  wire                    s_axil_bready,
    input  wire [15:0]             s_axil_araddr,
    input  wire                    s_axil_arvalid,
    output reg                     s_axil_arready,
    output reg  [31:0]             s_axil_rdata,
    output wire [1:0]              s_axil_rresp,
    output reg                     s_axil_rvalid,
    input  wire                    s_axil_rready,

    input  wire [LANES*512-1:0]    s_tdata,
    input  wire [LANES*64-1:0]     s_tkeep,
    input  wire [LANES-1:0]        s_tlast,
    input  wire [LANES-1:0]        s_tvalid,
    output wire [LANES-1:0]        s_tready,

    output wire [LANES*512-1:0]    m_tdata,
    output wire [LANES*64-1:0]     m_tkeep,
    output wire [LANES-1:0]        m_tlast,
    output wire [LANES-1:0]        m_tvalid,
    input  wire [LANES-1:0]        m_tready,

    // DAQ lanes TAP0, TAP1: received beats (no ready), beats to inject
    output wire [1:0]              tap_valid,
    output wire [1:0]              tap_last,
    output wire [127:0]            tap_keep,
    output wire [1023:0]           tap_data,
    input  wire [1:0]              inj_valid,
    output wire [1:0]              inj_ready,
    input  wire [1023:0]           inj_data
);
    localparam [31:0] ID = 32'h4C414E45;
    localparam [15:0] VERSION = 16'd3;

    // the user-region end must generate and check the same pattern
    function automatic [511:0] lane_pattern(input [7:0] lane, input [63:0] seq);
        lane_pattern[63:0] = seq;
        for (int j = 1; j < 8; j++)
            lane_pattern[64 * j +: 64] = {lane, j[7:0], seq[47:0]};
    endfunction

    reg [63:0] cycles;
    always @(posedge clk) cycles <= resetn ? cycles + 1 : 64'd0;

    // ---------------------------------------------------------------- control registers
    reg [LANES-1:0] start_mask, stop_mask, clear_mask;
    reg [63:0] gen_count [LANES];
    reg [15:0] gen_len [LANES];
    reg [31:0] gen_gap [LANES];

    // lane state
    reg [LANES-1:0] gen_run, gen_stopping, gen_go, gen_counted, gen_started, chk_started;
    reg [63:0] gen_sent [LANES], gen_stall [LANES], gen_first [LANES], gen_last [LANES];
    reg [63:0] gen_left [LANES];
    reg [15:0] gen_beat [LANES];
    reg [31:0] gen_wait [LANES];
    reg [63:0] chk_beats [LANES], chk_frames [LANES], chk_next [LANES];
    reg [63:0] chk_first [LANES], chk_last [LANES];
    reg [31:0] chk_errors [LANES], chk_err_beat [LANES], chk_err_got [LANES], chk_err_exp [LANES];

    assign s_tready = {LANES{1'b1}};

    genvar k;
    generate
        for (k = 0; k < LANES; k = k + 1) begin : g_lane
            // ------------------------------------------------ generator
            // gen_go: running and the inter-frame gap is over (a register);
            // gen_beat counts the beats left in the frame down to 0
            wire        gen_ready;
            wire        gen_valid = gen_go[k];
            wire        gen_fire = gen_valid && gen_ready;
            wire        gen_eof = (gen_beat[k] == 16'd0);
            wire        gen_end = gen_eof && (gen_stopping[k] || stop_mask[k]);
            wire        gen_out = gen_counted[k] && (gen_left[k] == 64'd1);

            always @(posedge clk) begin
                if (!resetn) begin
                    gen_run[k] <= 1'b0;
                    gen_go[k] <= 1'b0;
                    gen_stopping[k] <= 1'b0;
                end else begin
                    if (start_mask[k] && !gen_run[k]) begin
                        gen_run[k] <= 1'b1;
                        gen_go[k] <= 1'b1;
                        gen_stopping[k] <= 1'b0;
                        gen_beat[k] <= gen_len[k] - 16'd1;
                        gen_wait[k] <= 0;
                        gen_left[k] <= gen_count[k];
                        gen_counted[k] <= (gen_count[k] != 0);
                    end else if (gen_run[k]) begin
                        if (stop_mask[k])
                            gen_stopping[k] <= 1'b1;
                        if (gen_wait[k] != 0) begin
                            // inter-frame gap (gen_go is low)
                            gen_wait[k] <= gen_wait[k] - 1;
                            if (gen_wait[k] == 1) begin
                                if (gen_stopping[k] || stop_mask[k])
                                    gen_run[k] <= 1'b0;
                                else
                                    gen_go[k] <= 1'b1;
                            end
                        end
                        if (gen_fire) begin
                            gen_left[k] <= gen_left[k] - 1;
                            gen_beat[k] <= gen_eof ? gen_len[k] - 16'd1 : gen_beat[k] - 16'd1;
                            if (gen_out || gen_end) begin
                                gen_run[k] <= 1'b0;
                                gen_go[k] <= 1'b0;
                            end else if (gen_eof && gen_gap[k] != 0) begin
                                gen_wait[k] <= gen_gap[k];
                                gen_go[k] <= 1'b0;
                            end
                        end else if (gen_go[k] && gen_beat[k] == gen_len[k] - 16'd1 &&
                                     (gen_stopping[k] || stop_mask[k])) begin
                            // stopped between frames while the output was stalled
                            gen_run[k] <= 1'b0;
                            gen_go[k] <= 1'b0;
                        end
                    end
                end
                if (!resetn || clear_mask[k]) begin
                    gen_sent[k] <= 0;
                    gen_stall[k] <= 0;
                    gen_first[k] <= 0;
                    gen_last[k] <= 0;
                    gen_started[k] <= 1'b0;
                end else begin
                    if (gen_valid && !gen_ready)
                        gen_stall[k] <= gen_stall[k] + 1;
                    if (gen_fire) begin
                        gen_sent[k] <= gen_sent[k] + 1;
                        gen_started[k] <= 1'b1;
                        if (!gen_started[k])
                            gen_first[k] <= cycles;
                        gen_last[k] <= cycles;
                    end
                end
            end

            // a DAQ lane: injected beats first
            localparam integer TI = (k == TAP1) ? 1 : 0;
            localparam integer IS_TAP = (k == TAP0) || (k == TAP1);
            wire         in_inj = IS_TAP && inj_valid[TI];
            wire         slice_ready;
            assign gen_ready = slice_ready && !in_inj;
            if (IS_TAP) begin : g_inj
                assign inj_ready[TI] = slice_ready;
            end

            // two register slices to the NMU: the second one sits at the NoC and
            // has nothing but m_tready besides its registered inputs
            wire         mid_valid, mid_ready;
            wire [576:0] mid_data;
            axis_skid #(.DATA_BITS(512 + 64 + 1)) inst_gen (
                .clk(clk), .resetn(resetn),
                .s_valid(gen_valid || in_inj), .s_ready(slice_ready),
                .s_data(in_inj ? {1'b1, {64{1'b1}}, inj_data[512 * TI +: 512]} :
                                 {gen_eof, {64{1'b1}}, lane_pattern(k[7:0], gen_sent[k])}),
                .m_valid(mid_valid), .m_ready(mid_ready), .m_data(mid_data)
            );
            axis_skid #(.DATA_BITS(512 + 64 + 1), .SLICES(4)) inst_out (
                .clk(clk), .resetn(resetn),
                .s_valid(mid_valid), .s_ready(mid_ready), .s_data(mid_data),
                .m_valid(m_tvalid[k]),
                .m_ready(m_tready[k]),
                .m_data({m_tlast[k], m_tkeep[64 * k +: 64], m_tdata[512 * k +: 512]})
            );

            // ------------------------------------------------ checker
            // stage 0: NSU outputs straight into registers
            reg         l_valid;
            reg         l_last;
            reg [63:0]  l_keep;
            reg [511:0] l_data;
            always @(posedge clk) begin
                l_valid <= s_tvalid[k];
                l_last <= s_tlast[k];
                l_keep <= s_tkeep[64 * k +: 64];
                l_data <= s_tdata[512 * k +: 512];
            end
            if (IS_TAP) begin : g_tap
                assign tap_valid[TI] = l_valid;
                assign tap_last[TI] = l_last;
                assign tap_keep[64 * TI +: 64] = l_keep;
                assign tap_data[512 * TI +: 512] = l_data;
            end

            // stage 1: form check of each word and sequence check of word 0
            reg         c_valid, c_last, c_seq_bad;
            reg [7:0]   c_form_bad;
            reg [31:0]  c_got, c_exp;
            always @(posedge clk) begin
                c_valid <= resetn && !clear_mask[k] && l_valid;
                c_last <= l_last;
                c_got <= l_data[31:0];
                c_exp <= chk_next[k][31:0];
                c_seq_bad <= (l_data[63:0] != chk_next[k]);
                c_form_bad[0] <= (l_keep != {64{1'b1}});
                for (int j = 1; j < 8; j++)
                    c_form_bad[j] <= (l_data[64 * j +: 64] != {k[7:0], j[7:0], l_data[47:0]});
                if (!resetn || clear_mask[k])
                    chk_next[k] <= 0;
                else if (l_valid)
                    chk_next[k] <= l_data[63:0] + 1;
            end

            // stage 2: counters
            wire mismatch = c_seq_bad || (c_form_bad != 0);
            always @(posedge clk) begin
                if (!resetn || clear_mask[k]) begin
                    chk_beats[k] <= 0;
                    chk_frames[k] <= 0;
                    chk_first[k] <= 0;
                    chk_last[k] <= 0;
                    chk_started[k] <= 1'b0;
                    chk_errors[k] <= 0;
                    chk_err_beat[k] <= 0;
                    chk_err_got[k] <= 0;
                    chk_err_exp[k] <= 0;
                end else if (c_valid) begin
                    chk_beats[k] <= chk_beats[k] + 1;
                    chk_frames[k] <= chk_frames[k] + c_last;
                    chk_started[k] <= 1'b1;
                    if (!chk_started[k])
                        chk_first[k] <= cycles;
                    chk_last[k] <= cycles;
                    if (mismatch) begin
                        chk_errors[k] <= chk_errors[k] + 1;
                        if (chk_errors[k] == 0) begin
                            chk_err_beat[k] <= chk_beats[k][31:0];
                            chk_err_got[k] <= c_got;
                            chk_err_exp[k] <= c_exp;
                        end
                    end
                end
            end
        end
    endgenerate

    // ---------------------------------------------------------------- AXI4-Lite
    assign s_axil_bresp = 2'b00;
    assign s_axil_rresp = 2'b00;

    wire [15:0] w_addr = s_axil_awaddr;
    wire        w_lane_reg = (w_addr >= 16'h100) && (w_addr < 16'h100 + LANES * 16'h80);
    wire [7:0]  w_lane = (w_addr - 16'h100) >> 7;
    wire [6:0]  w_off = w_addr[6:0];

    always @(posedge clk) begin
        start_mask <= 0;
        stop_mask <= 0;
        clear_mask <= 0;
        if (!resetn) begin
            s_axil_awready <= 1'b0;
            s_axil_wready <= 1'b0;
            s_axil_bvalid <= 1'b0;
            for (int i = 0; i < LANES; i++) begin
                gen_count[i] <= 0;
                gen_len[i] <= 16'd64;
                gen_gap[i] <= 0;
            end
        end else begin
            s_axil_awready <= 1'b0;
            s_axil_wready <= 1'b0;
            if (s_axil_bvalid && s_axil_bready)
                s_axil_bvalid <= 1'b0;
            if (s_axil_awvalid && s_axil_wvalid && !s_axil_awready && !s_axil_bvalid) begin
                s_axil_awready <= 1'b1;
                s_axil_wready <= 1'b1;
                s_axil_bvalid <= 1'b1;
                if (s_axil_wstrb == 4'hF) begin
                    if (!w_lane_reg) begin
                        case (w_addr)
                            16'h010: start_mask <= s_axil_wdata[LANES-1:0];
                            16'h014: stop_mask <= s_axil_wdata[LANES-1:0];
                            16'h018: clear_mask <= s_axil_wdata[LANES-1:0];
                            default: ;
                        endcase
                    end else begin
                        case (w_off)
                            7'h08: gen_count[w_lane][31:0] <= s_axil_wdata;
                            7'h0C: gen_count[w_lane][63:32] <= s_axil_wdata;
                            7'h10: gen_len[w_lane] <= (s_axil_wdata[15:0] == 0) ? 16'd1 : s_axil_wdata[15:0];
                            7'h14: gen_gap[w_lane] <= s_axil_wdata;
                            default: ;
                        endcase
                    end
                end
            end
        end
    end

    // reads: address registered, data one cycle later
    reg        r_pend;
    reg [15:0] r_addr;
    wire       r_lane_reg = (r_addr >= 16'h100) && (r_addr < 16'h100 + LANES * 16'h80);
    wire [7:0] r_lane = (r_addr - 16'h100) >> 7;
    wire [6:0] r_off = r_addr[6:0];

    function automatic [31:0] half(input [63:0] v, input hi);
        half = hi ? v[63:32] : v[31:0];
    endfunction

    always @(posedge clk) begin
        if (!resetn) begin
            s_axil_arready <= 1'b0;
            s_axil_rvalid <= 1'b0;
            r_pend <= 1'b0;
        end else begin
            s_axil_arready <= 1'b0;
            if (s_axil_rvalid && s_axil_rready)
                s_axil_rvalid <= 1'b0;
            if (s_axil_arvalid && !s_axil_arready && !r_pend && !s_axil_rvalid) begin
                s_axil_arready <= 1'b1;
                r_addr <= s_axil_araddr;
                r_pend <= 1'b1;
            end
            if (r_pend) begin
                r_pend <= 1'b0;
                s_axil_rvalid <= 1'b1;
                s_axil_rdata <= 32'd0;
                if (!r_lane_reg) begin
                    case (r_addr)
                        16'h000: s_axil_rdata <= ID;
                        16'h004: s_axil_rdata <= {VERSION, 8'd0, 8'(LANES)};
                        16'h008: s_axil_rdata <= cycles[31:0];
                        16'h00C: s_axil_rdata <= cycles[63:32];
                        default: ;
                    endcase
                end else begin
                    case (r_off)
                        7'h00: s_axil_rdata <= {31'd0, gen_run[r_lane]};
                        7'h08, 7'h0C: s_axil_rdata <= half(gen_count[r_lane], r_off[2]);
                        7'h10: s_axil_rdata <= gen_len[r_lane];
                        7'h14: s_axil_rdata <= gen_gap[r_lane];
                        7'h18, 7'h1C: s_axil_rdata <= half(gen_sent[r_lane], r_off[2]);
                        7'h20, 7'h24: s_axil_rdata <= half(gen_stall[r_lane], r_off[2]);
                        7'h28, 7'h2C: s_axil_rdata <= half(gen_first[r_lane], r_off[2]);
                        7'h30, 7'h34: s_axil_rdata <= half(gen_last[r_lane], r_off[2]);
                        7'h40, 7'h44: s_axil_rdata <= half(chk_beats[r_lane], r_off[2]);
                        7'h48, 7'h4C: s_axil_rdata <= half(chk_frames[r_lane], r_off[2]);
                        7'h50: s_axil_rdata <= chk_errors[r_lane];
                        7'h54: s_axil_rdata <= chk_err_beat[r_lane];
                        7'h58: s_axil_rdata <= chk_err_got[r_lane];
                        7'h5C: s_axil_rdata <= chk_err_exp[r_lane];
                        7'h60, 7'h64: s_axil_rdata <= half(chk_first[r_lane], r_off[2]);
                        7'h68, 7'h6C: s_axil_rdata <= half(chk_last[r_lane], r_off[2]);
                        default: ;
                    endcase
                end
            end
        end
    end
endmodule
