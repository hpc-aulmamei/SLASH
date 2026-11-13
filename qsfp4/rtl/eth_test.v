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

// Ethernet test of one port: eth_gen, eth_chk and their registers, in the
// node clock of the port's DCMAC. Register map: qsfp4/README.md.
//
// Reads have no side effect (the host path turns every BAR2 read into two).
module eth_test #(
    parameter integer PORT = 0,             // SLASH port number, QSFPn
    parameter integer BUILD = 1
) (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axil:s_axis:m_axis, ASSOCIATED_RESET resetn" *)
    input  wire          clk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 resetn RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire          resetn,

    input  wire [15:0]   s_axil_awaddr,
    input  wire [2:0]    s_axil_awprot,
    input  wire          s_axil_awvalid,
    output wire          s_axil_awready,
    input  wire [31:0]   s_axil_wdata,
    input  wire [3:0]    s_axil_wstrb,
    input  wire          s_axil_wvalid,
    output wire          s_axil_wready,
    output wire [1:0]    s_axil_bresp,
    output reg           s_axil_bvalid,
    input  wire          s_axil_bready,
    input  wire [15:0]   s_axil_araddr,
    input  wire [2:0]    s_axil_arprot,
    input  wire          s_axil_arvalid,
    output wire          s_axil_arready,
    output reg  [31:0]   s_axil_rdata,
    output wire [1:0]    s_axil_rresp,
    output reg           s_axil_rvalid,
    input  wire          s_axil_rready,

    input  wire [1023:0] s_axis_tdata,
    input  wire [127:0]  s_axis_tkeep,
    input  wire          s_axis_tlast,
    input  wire          s_axis_tvalid,
    output wire          s_axis_tready,

    output wire [1023:0] m_axis_tdata,
    output wire [127:0]  m_axis_tkeep,
    output wire          m_axis_tlast,
    output wire          m_axis_tvalid,
    input  wire          m_axis_tready,

    input  wire [15:0]   rx_drop_count,
    output reg  [15:0]   phy_ctrl,
    input  wire [15:0]   phy_status
);
    localparam [31:0] ID = 32'h45544854;    // "ETHT"
    localparam [31:0] VERSION = 32'h00010000;
    localparam [7:0]  PORT_ID = PORT[7:0];

    reg [31:0] scratch;
    reg [63:0] cycles;
    reg [15:0] len_min, len_max, len_mask, gap, etype;
    reg [63:0] count, seed;
    reg [47:0] g_dst, g_src, c_dst, c_src;
    reg [7:0]  c_id;
    reg        g_start, g_stop, g_clear, c_clear;

    wire        running;
    wire [63:0] tx_frames, tx_bytes;
    wire [63:0] rx_frames, rx_bytes, rx_lost;
    wire [31:0] reorder, hdr_err, len_err, pay_err;
    wire [7:0]  last_id;
    wire [47:0] last_src;
    wire        first_valid;
    wire [2:0]  first_kind;
    wire [3:0]  first_lane;
    wire [15:0] first_beat;
    wire [63:0] first_seq, first_exp, first_got;

    eth_gen inst_gen (
        .clk(clk), .resetn(resetn),
        .start(g_start), .stop(g_stop), .clear(g_clear),
        .len_min(len_min), .len_max(len_max), .len_mask(len_mask), .gap(gap),
        .count(count), .dst(g_dst), .src(g_src), .etype(etype), .id(PORT_ID), .seed(seed),
        .running(running), .frames(tx_frames), .bytes(tx_bytes),
        .m_tdata(m_axis_tdata), .m_tkeep(m_axis_tkeep), .m_tlast(m_axis_tlast),
        .m_tvalid(m_axis_tvalid), .m_tready(m_axis_tready)
    );

    eth_chk inst_chk (
        .clk(clk), .resetn(resetn),
        .clear(c_clear),
        .dst(c_dst), .src(c_src), .etype(etype), .id(c_id), .seed(seed),
        .frames(rx_frames), .bytes(rx_bytes), .lost(rx_lost), .reorder(reorder),
        .hdr_err(hdr_err), .len_err(len_err), .pay_err(pay_err),
        .last_id(last_id), .last_src(last_src),
        .first_valid(first_valid), .first_kind(first_kind), .first_lane(first_lane),
        .first_beat(first_beat), .first_seq(first_seq), .first_exp(first_exp),
        .first_got(first_got),
        .s_tdata(s_axis_tdata), .s_tkeep(s_axis_tkeep), .s_tlast(s_axis_tlast),
        .s_tvalid(s_axis_tvalid), .s_tready(s_axis_tready)
    );

    assign s_axil_bresp = 2'b00;
    assign s_axil_rresp = 2'b00;

    // ---- writes: one at a time, whole words only ----
    wire do_wr = s_axil_awvalid && s_axil_wvalid && !s_axil_bvalid;
    assign s_axil_awready = do_wr;
    assign s_axil_wready = do_wr;
    wire [11:0] wa = {s_axil_awaddr[11:2], 2'b00};
    wire [31:0] wd = s_axil_wdata;

    always @(posedge clk) begin
        g_start <= 1'b0;
        g_stop <= 1'b0;
        g_clear <= 1'b0;
        c_clear <= 1'b0;
        cycles <= cycles + 64'd1;
        if (!resetn) begin
            s_axil_bvalid <= 1'b0;
            cycles <= 64'd0;
            scratch <= 32'd0;
            phy_ctrl <= 16'd0;
            len_min <= 16'd1500;
            len_max <= 16'd9000;
            len_mask <= 16'd0;
            gap <= 16'd0;
            etype <= 16'h88B5;
            count <= 64'd0;
            seed <= 64'h9E37_79B9_7F4A_7C15;
            g_dst <= 48'h02_00_00_00_00_00;
            g_src <= {40'h02_00_00_00_00, PORT_ID};
            c_dst <= {40'h02_00_00_00_00, PORT_ID};
            c_src <= 48'h02_00_00_00_00_00;
            c_id <= 8'd0;
        end else begin
            if (s_axil_bvalid && s_axil_bready)
                s_axil_bvalid <= 1'b0;
            if (do_wr) begin
                s_axil_bvalid <= 1'b1;
                if (&s_axil_wstrb) begin
                    case (wa)
                        12'h008: scratch <= wd;
                        12'h020: phy_ctrl <= wd[15:0];
                        12'h040: begin
                            g_start <= wd[0];
                            g_stop <= wd[1];
                            g_clear <= wd[2];
                        end
                        12'h044: len_min <= wd[15:0];
                        12'h048: len_max <= wd[15:0];
                        12'h04C: len_mask <= wd[15:0];
                        12'h050: gap <= wd[15:0];
                        12'h054: etype <= wd[15:0];
                        12'h058: count[31:0] <= wd;
                        12'h05C: count[63:32] <= wd;
                        12'h060: g_dst[31:0] <= wd;
                        12'h064: g_dst[47:32] <= wd[15:0];
                        12'h068: g_src[31:0] <= wd;
                        12'h06C: g_src[47:32] <= wd[15:0];
                        12'h070: seed[31:0] <= wd;
                        12'h074: seed[63:32] <= wd;
                        12'h100: c_clear <= wd[0];
                        12'h108: c_dst[31:0] <= wd;
                        12'h10C: c_dst[47:32] <= wd[15:0];
                        12'h110: c_src[31:0] <= wd;
                        12'h114: c_src[47:32] <= wd[15:0];
                        12'h118: c_id <= wd[7:0];
                        default: ;
                    endcase
                end
            end
        end
    end

    // ---- reads ----
    assign s_axil_arready = !s_axil_rvalid;
    reg [31:0] rd;
    always @* begin
        case ({s_axil_araddr[11:2], 2'b00})
            12'h000: rd = ID;
            12'h004: rd = VERSION;
            12'h008: rd = scratch;
            12'h00C: rd = BUILD;
            12'h010: rd = cycles[31:0];
            12'h014: rd = cycles[63:32];
            12'h018: rd = PORT;
            12'h020: rd = phy_ctrl;
            12'h024: rd = phy_status;
            12'h028: rd = rx_drop_count;
            12'h040: rd = running;
            12'h044: rd = len_min;
            12'h048: rd = len_max;
            12'h04C: rd = len_mask;
            12'h050: rd = gap;
            12'h054: rd = etype;
            12'h058: rd = count[31:0];
            12'h05C: rd = count[63:32];
            12'h060: rd = g_dst[31:0];
            12'h064: rd = g_dst[47:32];
            12'h068: rd = g_src[31:0];
            12'h06C: rd = g_src[47:32];
            12'h070: rd = seed[31:0];
            12'h074: rd = seed[63:32];
            12'h080: rd = tx_frames[31:0];
            12'h084: rd = tx_frames[63:32];
            12'h088: rd = tx_bytes[31:0];
            12'h08C: rd = tx_bytes[63:32];
            12'h108: rd = c_dst[31:0];
            12'h10C: rd = c_dst[47:32];
            12'h110: rd = c_src[31:0];
            12'h114: rd = c_src[47:32];
            12'h118: rd = c_id;
            12'h120: rd = rx_frames[31:0];
            12'h124: rd = rx_frames[63:32];
            12'h128: rd = rx_bytes[31:0];
            12'h12C: rd = rx_bytes[63:32];
            12'h130: rd = rx_lost[31:0];
            12'h134: rd = rx_lost[63:32];
            12'h138: rd = reorder;
            12'h13C: rd = hdr_err;
            12'h140: rd = len_err;
            12'h144: rd = pay_err;
            12'h148: rd = last_id;
            12'h14C: rd = last_src[31:0];
            12'h150: rd = last_src[47:32];
            12'h154: rd = {first_valid, 7'd0, first_lane, 1'b0, first_kind, first_beat};
            12'h158: rd = first_seq[31:0];
            12'h15C: rd = first_seq[63:32];
            12'h160: rd = first_exp[31:0];
            12'h164: rd = first_exp[63:32];
            12'h168: rd = first_got[31:0];
            12'h16C: rd = first_got[63:32];
            default: rd = 32'h0;
        endcase
    end

    always @(posedge clk) begin
        if (!resetn) begin
            s_axil_rvalid <= 1'b0;
        end else begin
            if (s_axil_rvalid && s_axil_rready)
                s_axil_rvalid <= 1'b0;
            if (s_axil_arvalid && s_axil_arready) begin
                s_axil_rvalid <= 1'b1;
                s_axil_rdata <= rd;
            end
        end
    end
endmodule
