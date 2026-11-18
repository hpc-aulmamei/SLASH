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

// sl_vmem: address translator of the service layer's user region -> host
// path. It takes the place of axi4_full_passthrough_4:
//
//   S_QDMA_SLV_BRIDGE (0x208_0000_0000, 32 GiB) -> axi_register_slice_8
//     -> sl_vmem -> axi_register_slice_9 -> noc_virt_4 -> M_QDMA_SLV_BRIDGE
//     -> CPM5 slave bridge region 0 (0xE800_0000, 64 windows of 2 MiB)
//     -> BDF entry n -> host bus address
//
// Translation, per 2 MiB page:
//   va  = addr[34:0]  (the aperture is 32 GiB aligned; upper bits are ignored)
//   vpn = va[34:21]   (16384 entries)
//   out = 0xE800_0000 + win * 2 MiB + va[20:0]
// An AXI burst never crosses 4 KiB, so it never spans two pages: one lookup
// per AW/AR, no splitting. The output can only address region 0.
//
// A request is not forwarded (fault) when the translator is disabled, the
// entry is invalid, its window is >= WIN_LIMIT, or it lacks the permission.
// A faulting write has its W beats accepted and dropped and gets BRESP
// SLVERR; a faulting read gets ARLEN+1 beats of zeros with RRESP SLVERR. To
// keep AXI ordering per ID without per-ID state, a fault is answered only
// once every forwarded request of the same direction has completed, and no
// request behind it is forwarded until then. Faults are rare; this costs
// nothing on the forwarding path.
//
// PAUSE stops accepting AW/AR (in-flight requests complete). STATUS.drained
// is set once nothing is in flight: software then changes BDF entries and
// table entries, and clears PAUSE.
//
// After reset the table is cleared (16384 cycles, STATUS.init_busy); AW/AR
// and AXI-Lite wait until then. Reset state: disabled, all entries invalid.
//
// AXI-Lite registers (byte offsets, 32-bit):
//   0x000 ID          0x534C564D "SLVM"
//   0x004 VERSION     0x0001_0000
//   0x008 BUILD
//   0x00C CAPS        [15:0] table entries, [23:16] page shift, [31:24] windows
//   0x010 CTRL    RW  [0] enable, [1] pause
//   0x014 STATUS      [0] init_busy [1] drained [2] fault_valid [3] aw_busy
//                     [4] ar_busy [5] write fault in progress [6] read fault
//                     in progress [7] enabled [8] paused
//   0x018 WIN_LIMIT RW [6:0] windows >= this fault (reset 63: the R5 owns 63)
//   0x01C OUTSTANDING [15:0] forwarded writes without B, [31:16] forwarded
//                     reads without the last R
//   0x020 W_PENDING   forwarded write bursts whose W beats are not all through
//   0x024 CLEAR   W   [0] fault record and FAULT_COUNT, [1] XLAT_W/XLAT_R
//   0x028 FAULT_ADDR_LO  0x02C FAULT_ADDR_HI   address of the first fault
//                     (FAULT_ADDR and FAULT_INFO read 0 while no fault is recorded)
//   0x030 FAULT_INFO  [31] valid, [30] write, [26:24] cause, [23:16] AxLEN,
//                     [15:0] AxID. Cause: 1 invalid entry, 2 no read permission,
//                     3 no write permission, 4 window >= WIN_LIMIT, 5 disabled
//   0x034 FAULT_COUNT faults since the last clear (saturating)
//   0x038 XLAT_W      0x03C XLAT_R   forwarded bursts (wrapping)
//   0x040 WIN_BASE_LO 0x044 WIN_BASE_HI   0xE800_0000
//   0x048 VA_BASE_LO  0x04C VA_BASE_HI    0x208_0000_0000 (informational)
//   0x1_0000 + 4 * vpn  TABLE  RW [0] valid [1] read [2] write [13:8] window

// Page table: true dual port, port A for AXI-Lite and the reset sweep, port B
// for lookups. Both read with two cycles of latency (output register).
module sl_vmem_tbl #(
    parameter integer AW = 14,
    parameter integer DW = 9
) (
    input  wire          clk,
    input  wire          a_en,
    input  wire          a_we,
    input  wire [AW-1:0] a_addr,
    input  wire [DW-1:0] a_din,
    output reg  [DW-1:0] a_q,
    input  wire          b_en,
    input  wire          b_regce,
    input  wire [AW-1:0] b_addr,
    output reg  [DW-1:0] b_q
);
    (* ram_style = "block" *) reg [DW-1:0] mem [0:(1 << AW) - 1];
    reg [DW-1:0] a_q1, b_q1;

    always @(posedge clk) begin
        if (a_en) begin
            if (a_we) mem[a_addr] <= a_din;
            a_q1 <= mem[a_addr];
        end
        a_q <= a_q1;
    end

    always @(posedge clk) begin
        if (b_en) b_q1 <= mem[b_addr];
        if (b_regce) b_q <= b_q1;
    end
endmodule

// Lookup pipeline of one address channel: s1 (table read), s2 (table output
// register), s3 (decision, registered). One request per cycle.
module sl_vmem_xlat #(
    parameter integer PW    = 8,
    parameter bit     WRITE = 1'b0
) (
    input  wire          clk,
    input  wire          rst,
    input  wire          enable,
    input  wire [6:0]    win_limit,

    input  wire          s_valid,
    output wire          s_ready,
    input  wire [63:0]   s_addr,
    input  wire [7:0]    s_len,
    input  wire [PW-1:0] s_pl,

    output wire          t_en,
    output wire          t_regce,
    output wire [13:0]   t_addr,
    input  wire [8:0]    t_q,

    output wire          o_valid,
    input  wire          o_ready,
    output reg           o_first,      // o_* were loaded on the last edge
    output reg           o_fault,
    output reg  [2:0]    o_cause,
    output reg  [63:0]   o_taddr,
    output reg  [63:0]   o_addr,
    output reg  [7:0]    o_len,
    output reg  [PW-1:0] o_pl,
    output wire          busy
);
    localparam [2:0] C_OK = 3'd0, C_INVALID = 3'd1, C_NOREAD = 3'd2, C_NOWRITE = 3'd3,
                     C_WINLIM = 3'd4, C_DISABLED = 3'd5;
    localparam [4:0] REGION0_HI = 5'b11101;      // 0xE800_0000 >> 27

    reg          s1_v, s2_v, s3_v;
    reg [63:0]   s1_addr, s2_addr;
    reg [7:0]    s1_len, s2_len;
    reg [PW-1:0] s1_pl, s2_pl;

    wire fire3   = s3_v && o_ready;
    wire s3_free = !s3_v || fire3;
    wire mv23    = s2_v && s3_free;
    wire s2_free = !s2_v || mv23;
    wire mv12    = s1_v && s2_free;
    wire s1_free = !s1_v || mv12;
    wire in_fire = s_valid && s1_free;

    assign s_ready = s1_free;
    assign t_en    = in_fire;
    assign t_regce = mv12;
    assign t_addr  = s_addr[34:21];
    assign o_valid = s3_v;
    assign busy    = s1_v || s2_v || s3_v;

    wire       e_v   = t_q[0];
    wire       e_r   = t_q[1];
    wire       e_w   = t_q[2];
    wire [5:0] e_win = t_q[8:3];
    reg  [2:0] cause_d;
    always @* begin
        if (!enable)                           cause_d = C_DISABLED;
        else if (!e_v)                         cause_d = C_INVALID;
        else if ({1'b0, e_win} >= win_limit)   cause_d = C_WINLIM;
        else if (WRITE && !e_w)                cause_d = C_NOWRITE;
        else if (!WRITE && !e_r)               cause_d = C_NOREAD;
        else                                   cause_d = C_OK;
    end

    always @(posedge clk) begin
        if (rst) begin
            s1_v    <= 1'b0;
            s2_v    <= 1'b0;
            s3_v    <= 1'b0;
            o_first <= 1'b0;
        end else begin
            if (s1_free) s1_v <= s_valid;
            if (s2_free) s2_v <= s1_v;
            if (s3_free) s3_v <= s2_v;
            o_first <= mv23;
        end
        if (in_fire) begin
            s1_addr <= s_addr;
            s1_len  <= s_len;
            s1_pl   <= s_pl;
        end
        if (mv12) begin
            s2_addr <= s1_addr;
            s2_len  <= s1_len;
            s2_pl   <= s1_pl;
        end
        if (mv23) begin
            o_fault <= (cause_d != C_OK);
            o_cause <= cause_d;
            o_taddr <= {32'h0, REGION0_HI, e_win, s2_addr[20:0]};
            o_addr  <= s2_addr;
            o_len   <= s2_len;
            o_pl    <= s2_pl;
        end
    end
endmodule

module sl_vmem #(
    parameter integer ID_W     = 6,
    parameter integer DATA_W   = 128,
    parameter integer AWUSER_W = 1,
    parameter integer WUSER_W  = 1,
    parameter integer BUSER_W  = 1,
    parameter integer ARUSER_W = 1,
    parameter integer RUSER_W  = 1,
    parameter [63:0]  VA_BASE  = 64'h0000_0208_0000_0000,
    parameter [31:0]  BUILD    = 32'd1
) (
    input  wire                  aclk,
    input  wire                  aresetn,

    input  wire [17:0]           s_axil_awaddr,
    input  wire                  s_axil_awvalid,
    output wire                  s_axil_awready,
    input  wire [31:0]           s_axil_wdata,
    input  wire [3:0]            s_axil_wstrb,
    input  wire                  s_axil_wvalid,
    output wire                  s_axil_wready,
    output wire [1:0]            s_axil_bresp,
    output reg                   s_axil_bvalid,
    input  wire                  s_axil_bready,
    input  wire [17:0]           s_axil_araddr,
    input  wire                  s_axil_arvalid,
    output wire                  s_axil_arready,
    output reg  [31:0]           s_axil_rdata,
    output wire [1:0]            s_axil_rresp,
    output reg                   s_axil_rvalid,
    input  wire                  s_axil_rready,

    input  wire [ID_W-1:0]       s_axi_awid,
    input  wire [63:0]           s_axi_awaddr,
    input  wire [7:0]            s_axi_awlen,
    input  wire [2:0]            s_axi_awsize,
    input  wire [1:0]            s_axi_awburst,
    input  wire                  s_axi_awlock,
    input  wire [3:0]            s_axi_awcache,
    input  wire [2:0]            s_axi_awprot,
    input  wire [3:0]            s_axi_awqos,
    input  wire [3:0]            s_axi_awregion,
    input  wire [AWUSER_W-1:0]   s_axi_awuser,
    input  wire                  s_axi_awvalid,
    output wire                  s_axi_awready,
    input  wire [DATA_W-1:0]     s_axi_wdata,
    input  wire [DATA_W/8-1:0]   s_axi_wstrb,
    input  wire                  s_axi_wlast,
    input  wire [WUSER_W-1:0]    s_axi_wuser,
    input  wire                  s_axi_wvalid,
    output wire                  s_axi_wready,
    output wire [ID_W-1:0]       s_axi_bid,
    output wire [1:0]            s_axi_bresp,
    output wire [BUSER_W-1:0]    s_axi_buser,
    output wire                  s_axi_bvalid,
    input  wire                  s_axi_bready,
    input  wire [ID_W-1:0]       s_axi_arid,
    input  wire [63:0]           s_axi_araddr,
    input  wire [7:0]            s_axi_arlen,
    input  wire [2:0]            s_axi_arsize,
    input  wire [1:0]            s_axi_arburst,
    input  wire                  s_axi_arlock,
    input  wire [3:0]            s_axi_arcache,
    input  wire [2:0]            s_axi_arprot,
    input  wire [3:0]            s_axi_arqos,
    input  wire [3:0]            s_axi_arregion,
    input  wire [ARUSER_W-1:0]   s_axi_aruser,
    input  wire                  s_axi_arvalid,
    output wire                  s_axi_arready,
    output wire [ID_W-1:0]       s_axi_rid,
    output wire [DATA_W-1:0]     s_axi_rdata,
    output wire [1:0]            s_axi_rresp,
    output wire                  s_axi_rlast,
    output wire [RUSER_W-1:0]    s_axi_ruser,
    output wire                  s_axi_rvalid,
    input  wire                  s_axi_rready,

    output wire [ID_W-1:0]       m_axi_awid,
    output wire [63:0]           m_axi_awaddr,
    output wire [7:0]            m_axi_awlen,
    output wire [2:0]            m_axi_awsize,
    output wire [1:0]            m_axi_awburst,
    output wire                  m_axi_awlock,
    output wire [3:0]            m_axi_awcache,
    output wire [2:0]            m_axi_awprot,
    output wire [3:0]            m_axi_awqos,
    output wire [3:0]            m_axi_awregion,
    output wire [AWUSER_W-1:0]   m_axi_awuser,
    output wire                  m_axi_awvalid,
    input  wire                  m_axi_awready,
    output wire [DATA_W-1:0]     m_axi_wdata,
    output wire [DATA_W/8-1:0]   m_axi_wstrb,
    output wire                  m_axi_wlast,
    output wire [WUSER_W-1:0]    m_axi_wuser,
    output wire                  m_axi_wvalid,
    input  wire                  m_axi_wready,
    input  wire [ID_W-1:0]       m_axi_bid,
    input  wire [1:0]            m_axi_bresp,
    input  wire [BUSER_W-1:0]    m_axi_buser,
    input  wire                  m_axi_bvalid,
    output wire                  m_axi_bready,
    output wire [ID_W-1:0]       m_axi_arid,
    output wire [63:0]           m_axi_araddr,
    output wire [7:0]            m_axi_arlen,
    output wire [2:0]            m_axi_arsize,
    output wire [1:0]            m_axi_arburst,
    output wire                  m_axi_arlock,
    output wire [3:0]            m_axi_arcache,
    output wire [2:0]            m_axi_arprot,
    output wire [3:0]            m_axi_arqos,
    output wire [3:0]            m_axi_arregion,
    output wire [ARUSER_W-1:0]   m_axi_aruser,
    output wire                  m_axi_arvalid,
    input  wire                  m_axi_arready,
    input  wire [ID_W-1:0]       m_axi_rid,
    input  wire [DATA_W-1:0]     m_axi_rdata,
    input  wire [1:0]            m_axi_rresp,
    input  wire                  m_axi_rlast,
    input  wire [RUSER_W-1:0]    m_axi_ruser,
    input  wire                  m_axi_rvalid,
    output wire                  m_axi_rready
);
    localparam [31:0] ID      = 32'h534C564D;
    localparam [31:0] VERSION = 32'h0001_0000;
    localparam [31:0] CAPS    = {8'd64, 8'd21, 16'h4000};
    localparam [31:0] WIN_BASE = 32'hE800_0000;
    localparam [1:0]  SLVERR  = 2'b10;
    localparam integer CW     = 16;              // outstanding counter width

    wire clk = aclk;
    wire rst = !aresetn;

    // ------------------------------------------------------------ control
    reg        ctl_enable, ctl_pause;
    reg [6:0]  win_limit;
    reg        init_busy;
    reg [13:0] init_idx;

    always @(posedge clk) begin
        if (rst) begin
            init_busy <= 1'b1;
            init_idx  <= 14'd0;
        end else if (init_busy) begin
            init_idx <= init_idx + 14'd1;
            if (&init_idx) init_busy <= 1'b0;
        end
    end

    wire intake = !ctl_pause && !init_busy;

    // ------------------------------------------------------------ tables
    // two copies (one per address channel), written together through port A
    wire        la_en, la_we;
    wire [13:0] la_addr;
    wire [8:0]  la_din;
    wire        ta_en   = init_busy || la_en;
    wire        ta_we   = init_busy || la_we;
    wire [13:0] ta_addr = init_busy ? init_idx : la_addr;
    wire [8:0]  ta_din  = init_busy ? 9'd0 : la_din;
    wire [8:0]  ta_q;

    wire        aw_t_en, aw_t_regce, ar_t_en, ar_t_regce;
    wire [13:0] aw_t_addr, ar_t_addr;
    wire [8:0]  aw_t_q, ar_t_q;

    sl_vmem_tbl u_tbl_aw (
        .clk(clk), .a_en(ta_en), .a_we(ta_we), .a_addr(ta_addr), .a_din(ta_din), .a_q(ta_q),
        .b_en(aw_t_en), .b_regce(aw_t_regce), .b_addr(aw_t_addr), .b_q(aw_t_q));
    sl_vmem_tbl u_tbl_ar (
        .clk(clk), .a_en(ta_en), .a_we(ta_we), .a_addr(ta_addr), .a_din(ta_din), .a_q(),
        .b_en(ar_t_en), .b_regce(ar_t_regce), .b_addr(ar_t_addr), .b_q(ar_t_q));

    // ------------------------------------------------------------ write address
    localparam integer AWPL = ID_W + 3 + 2 + 1 + 4 + 3 + 4 + 4 + AWUSER_W;
    wire            aw_ready, aw_o_valid, aw_o_ready, aw_o_first, aw_o_fault, aw_busy;
    wire [2:0]      aw_o_cause;
    wire [63:0]     aw_o_taddr, aw_o_addr;
    wire [7:0]      aw_o_len;
    wire [AWPL-1:0] aw_o_pl;

    sl_vmem_xlat #(.PW(AWPL), .WRITE(1'b1)) u_aw (
        .clk(clk), .rst(rst), .enable(ctl_enable), .win_limit(win_limit),
        .s_valid(s_axi_awvalid && intake), .s_ready(aw_ready),
        .s_addr(s_axi_awaddr), .s_len(s_axi_awlen),
        .s_pl({s_axi_awid, s_axi_awsize, s_axi_awburst, s_axi_awlock, s_axi_awcache,
               s_axi_awprot, s_axi_awqos, s_axi_awregion, s_axi_awuser}),
        .t_en(aw_t_en), .t_regce(aw_t_regce), .t_addr(aw_t_addr), .t_q(aw_t_q),
        .o_valid(aw_o_valid), .o_ready(aw_o_ready), .o_first(aw_o_first),
        .o_fault(aw_o_fault), .o_cause(aw_o_cause), .o_taddr(aw_o_taddr),
        .o_addr(aw_o_addr), .o_len(aw_o_len), .o_pl(aw_o_pl), .busy(aw_busy));

    assign s_axi_awready = aw_ready && intake;
    wire [ID_W-1:0] aw_o_id = aw_o_pl[AWPL-1 -: ID_W];
    assign {m_axi_awid, m_axi_awsize, m_axi_awburst, m_axi_awlock, m_axi_awcache,
            m_axi_awprot, m_axi_awqos, m_axi_awregion, m_axi_awuser} = aw_o_pl;
    assign m_axi_awaddr  = aw_o_taddr;
    assign m_axi_awlen   = aw_o_len;
    assign m_axi_awvalid = aw_o_valid && !aw_o_fault;

    // ------------------------------------------------------------ write fault
    localparam [2:0] WF_IDLE = 3'd0, WF_WAITW = 3'd1, WF_SINK = 3'd2, WF_WAITB = 3'd3,
                     WF_RESP = 3'd4;
    reg  [2:0]    wf_st;
    reg  [CW-1:0] wr_out, w_fwd_cnt;
    wire          w_sink = (wf_st == WF_SINK);
    wire          bf     = (wf_st == WF_RESP);

    always @(posedge clk) begin
        if (rst) wf_st <= WF_IDLE;
        else case (wf_st)
            WF_IDLE:  if (aw_o_valid && aw_o_fault) wf_st <= WF_WAITW;
            WF_WAITW: if (w_fwd_cnt == 0) wf_st <= WF_SINK;
            WF_SINK:  if (s_axi_wvalid && s_axi_wlast) wf_st <= WF_WAITB;
            WF_WAITB: if (wr_out == 0) wf_st <= WF_RESP;
            WF_RESP:  if (s_axi_bready) wf_st <= WF_IDLE;
            default:  wf_st <= WF_IDLE;
        endcase
    end
    assign aw_o_ready = aw_o_fault ? (bf && s_axi_bready) : m_axi_awready;

    // W: forwarded in AW order; a burst's beats may pass once its AW is
    // decided (before the AW handshake downstream)
    wire w_fwd = (w_fwd_cnt != 0);
    assign m_axi_wdata   = s_axi_wdata;
    assign m_axi_wstrb   = s_axi_wstrb;
    assign m_axi_wlast   = s_axi_wlast;
    assign m_axi_wuser   = s_axi_wuser;
    assign m_axi_wvalid  = s_axi_wvalid && w_fwd;
    assign s_axi_wready  = w_sink || (w_fwd && m_axi_wready);

    assign s_axi_bvalid  = bf || m_axi_bvalid;
    assign s_axi_bid     = bf ? aw_o_id : m_axi_bid;
    assign s_axi_bresp   = bf ? SLVERR : m_axi_bresp;
    assign s_axi_buser   = bf ? {BUSER_W{1'b0}} : m_axi_buser;
    assign m_axi_bready  = !bf && s_axi_bready;

    wire aw_hs   = m_axi_awvalid && m_axi_awready;
    wire b_hs    = m_axi_bvalid && m_axi_bready;
    wire w_inc   = aw_o_first && !aw_o_fault;
    wire w_dec   = m_axi_wvalid && m_axi_wready && m_axi_wlast;

    always @(posedge clk) begin
        if (rst) begin
            wr_out    <= 0;
            w_fwd_cnt <= 0;
        end else begin
            wr_out    <= wr_out + aw_hs - b_hs;
            w_fwd_cnt <= w_fwd_cnt + w_inc - w_dec;
        end
    end

    // ------------------------------------------------------------ read address
    localparam integer ARPL = ID_W + 3 + 2 + 1 + 4 + 3 + 4 + 4 + ARUSER_W;
    wire            ar_ready, ar_o_valid, ar_o_ready, ar_o_first, ar_o_fault, ar_busy;
    wire [2:0]      ar_o_cause;
    wire [63:0]     ar_o_taddr, ar_o_addr;
    wire [7:0]      ar_o_len;
    wire [ARPL-1:0] ar_o_pl;

    sl_vmem_xlat #(.PW(ARPL), .WRITE(1'b0)) u_ar (
        .clk(clk), .rst(rst), .enable(ctl_enable), .win_limit(win_limit),
        .s_valid(s_axi_arvalid && intake), .s_ready(ar_ready),
        .s_addr(s_axi_araddr), .s_len(s_axi_arlen),
        .s_pl({s_axi_arid, s_axi_arsize, s_axi_arburst, s_axi_arlock, s_axi_arcache,
               s_axi_arprot, s_axi_arqos, s_axi_arregion, s_axi_aruser}),
        .t_en(ar_t_en), .t_regce(ar_t_regce), .t_addr(ar_t_addr), .t_q(ar_t_q),
        .o_valid(ar_o_valid), .o_ready(ar_o_ready), .o_first(ar_o_first),
        .o_fault(ar_o_fault), .o_cause(ar_o_cause), .o_taddr(ar_o_taddr),
        .o_addr(ar_o_addr), .o_len(ar_o_len), .o_pl(ar_o_pl), .busy(ar_busy));

    assign s_axi_arready = ar_ready && intake;
    wire [ID_W-1:0] ar_o_id = ar_o_pl[ARPL-1 -: ID_W];
    assign {m_axi_arid, m_axi_arsize, m_axi_arburst, m_axi_arlock, m_axi_arcache,
            m_axi_arprot, m_axi_arqos, m_axi_arregion, m_axi_aruser} = ar_o_pl;
    assign m_axi_araddr  = ar_o_taddr;
    assign m_axi_arlen   = ar_o_len;
    assign m_axi_arvalid = ar_o_valid && !ar_o_fault;

    // ------------------------------------------------------------ read fault
    localparam [1:0] RF_IDLE = 2'd0, RF_WAIT = 2'd1, RF_GEN = 2'd2;
    reg  [1:0]    rf_st;
    reg  [7:0]    rf_beat;
    reg  [CW-1:0] rd_out;
    wire          rg      = (rf_st == RF_GEN);
    wire          rg_last = (rf_beat == ar_o_len);

    always @(posedge clk) begin
        if (rst) begin
            rf_st   <= RF_IDLE;
            rf_beat <= 8'd0;
        end else case (rf_st)
            RF_IDLE: if (ar_o_valid && ar_o_fault) rf_st <= RF_WAIT;
            RF_WAIT: if (rd_out == 0) begin
                rf_st   <= RF_GEN;
                rf_beat <= 8'd0;
            end
            RF_GEN: if (s_axi_rready) begin
                rf_beat <= rf_beat + 8'd1;
                if (rg_last) rf_st <= RF_IDLE;
            end
            default: rf_st <= RF_IDLE;
        endcase
    end
    assign ar_o_ready = ar_o_fault ? (rg && s_axi_rready && rg_last) : m_axi_arready;

    assign s_axi_rvalid = rg || m_axi_rvalid;
    assign s_axi_rid    = rg ? ar_o_id : m_axi_rid;
    assign s_axi_rdata  = rg ? {DATA_W{1'b0}} : m_axi_rdata;
    assign s_axi_rresp  = rg ? SLVERR : m_axi_rresp;
    assign s_axi_rlast  = rg ? rg_last : m_axi_rlast;
    assign s_axi_ruser  = rg ? {RUSER_W{1'b0}} : m_axi_ruser;
    assign m_axi_rready = !rg && s_axi_rready;

    wire ar_hs  = m_axi_arvalid && m_axi_arready;
    wire rl_hs  = m_axi_rvalid && m_axi_rready && m_axi_rlast;
    always @(posedge clk) begin
        if (rst) rd_out <= 0;
        else     rd_out <= rd_out + ar_hs - rl_hs;
    end

    // ------------------------------------------------------------ fault record, counters
    reg         clr_fault, clr_stats;
    reg         f_valid, f_write;
    reg [2:0]   f_cause;
    reg [7:0]   f_len;
    reg [15:0]  f_id;
    reg [63:0]  f_addr;
    reg [31:0]  f_count, xlat_w, xlat_r;
    wire        wcap = aw_o_first && aw_o_fault;
    wire        rcap = ar_o_first && ar_o_fault;
    wire [31:0] f_base = clr_fault ? 32'd0 : f_count;
    wire [32:0] f_sum  = {1'b0, f_base} + wcap + rcap;

    always @(posedge clk) begin
        if (rst) begin
            f_valid <= 1'b0;
            f_count <= 32'd0;
            xlat_w  <= 32'd0;
            xlat_r  <= 32'd0;
        end else begin
            if (clr_fault) f_valid <= 1'b0;
            if ((wcap || rcap) && (!f_valid || clr_fault)) begin
                f_valid <= 1'b1;
                f_write <= wcap;
                f_cause <= wcap ? aw_o_cause : ar_o_cause;
                f_len   <= wcap ? aw_o_len : ar_o_len;
                f_id    <= 16'(wcap ? aw_o_id : ar_o_id);
                f_addr  <= wcap ? aw_o_addr : ar_o_addr;
            end
            if (clr_fault || wcap || rcap) f_count <= f_sum[32] ? 32'hFFFF_FFFF : f_sum[31:0];
            if (clr_stats) begin
                xlat_w <= 32'd0;
                xlat_r <= 32'd0;
            end else begin
                xlat_w <= xlat_w + aw_hs;
                xlat_r <= xlat_r + ar_hs;
            end
        end
    end

    wire drained = ctl_pause && !aw_busy && !ar_busy && wr_out == 0 && rd_out == 0 &&
                   w_fwd_cnt == 0;

    // ------------------------------------------------------------ AXI-Lite
    localparam [2:0] L_IDLE = 3'd0, L_WRESP = 3'd1, L_TRD1 = 3'd2, L_TRD2 = 3'd3, L_RDATA = 3'd4;
    reg  [2:0]  l_st;
    wire        l_wr  = (l_st == L_IDLE) && !init_busy && s_axil_awvalid && s_axil_wvalid;
    wire        l_rd  = (l_st == L_IDLE) && !init_busy && !l_wr && s_axil_arvalid;
    wire        l_wtbl = s_axil_awaddr[17:16] == 2'b01;
    wire        l_rtbl = s_axil_araddr[17:16] == 2'b01;

    assign s_axil_awready = l_wr;
    assign s_axil_wready  = l_wr;
    assign s_axil_arready = l_rd;
    assign s_axil_bresp   = 2'b00;
    assign s_axil_rresp   = 2'b00;

    assign la_en   = (l_wr && l_wtbl) || (l_rd && l_rtbl);
    assign la_we   = l_wr && l_wtbl;
    assign la_addr = l_wr ? s_axil_awaddr[15:2] : s_axil_araddr[15:2];
    assign la_din  = {s_axil_wdata[13:8], s_axil_wdata[2:0]};

    reg [31:0] reg_rd;
    always @* begin
        reg_rd = 32'd0;
        if (s_axil_araddr[17:8] == 10'd0) begin
            case (s_axil_araddr[7:2])
                6'h00: reg_rd = ID;
                6'h01: reg_rd = VERSION;
                6'h02: reg_rd = BUILD;
                6'h03: reg_rd = CAPS;
                6'h04: reg_rd = {30'd0, ctl_pause, ctl_enable};
                6'h05: reg_rd = {23'd0, ctl_pause, ctl_enable, rf_st != RF_IDLE,
                                 wf_st != WF_IDLE, ar_busy, aw_busy, f_valid, drained,
                                 init_busy};
                6'h06: reg_rd = {25'd0, win_limit};
                6'h07: reg_rd = {rd_out, wr_out};
                6'h08: reg_rd = 32'(w_fwd_cnt);
                6'h0A: reg_rd = f_valid ? f_addr[31:0] : 32'd0;
                6'h0B: reg_rd = f_valid ? f_addr[63:32] : 32'd0;
                6'h0C: reg_rd = f_valid ? {1'b1, f_write, 3'd0, f_cause, f_len, f_id} : 32'd0;
                6'h0D: reg_rd = f_count;
                6'h0E: reg_rd = xlat_w;
                6'h0F: reg_rd = xlat_r;
                6'h10: reg_rd = WIN_BASE;
                6'h11: reg_rd = 32'd0;
                6'h12: reg_rd = VA_BASE[31:0];
                6'h13: reg_rd = VA_BASE[63:32];
                default: reg_rd = 32'd0;
            endcase
        end
    end

    always @(posedge clk) begin
        clr_fault <= 1'b0;
        clr_stats <= 1'b0;
        if (rst) begin
            l_st          <= L_IDLE;
            s_axil_bvalid <= 1'b0;
            s_axil_rvalid <= 1'b0;
            ctl_enable    <= 1'b0;
            ctl_pause     <= 1'b0;
            win_limit     <= 7'd63;
        end else case (l_st)
            L_IDLE: begin
                if (l_wr) begin
                    if (s_axil_awaddr[17:8] == 10'd0) begin
                        case (s_axil_awaddr[7:2])
                            6'h04: begin
                                ctl_enable <= s_axil_wdata[0];
                                ctl_pause  <= s_axil_wdata[1];
                            end
                            6'h06: win_limit <= (s_axil_wdata[6:0] > 7'd64) ? 7'd64
                                                                             : s_axil_wdata[6:0];
                            6'h09: begin
                                clr_fault <= s_axil_wdata[0];
                                clr_stats <= s_axil_wdata[1];
                            end
                            default: ;
                        endcase
                    end
                    s_axil_bvalid <= 1'b1;
                    l_st          <= L_WRESP;
                end else if (l_rd) begin
                    if (l_rtbl) begin
                        l_st <= L_TRD1;
                    end else begin
                        s_axil_rdata  <= reg_rd;
                        s_axil_rvalid <= 1'b1;
                        l_st          <= L_RDATA;
                    end
                end
            end
            L_WRESP: if (s_axil_bready) begin
                s_axil_bvalid <= 1'b0;
                l_st          <= L_IDLE;
            end
            L_TRD1: l_st <= L_TRD2;
            L_TRD2: begin
                s_axil_rdata  <= {18'd0, ta_q[8:3], 5'd0, ta_q[2:0]};
                s_axil_rvalid <= 1'b1;
                l_st          <= L_RDATA;
            end
            L_RDATA: if (s_axil_rready) begin
                s_axil_rvalid <= 1'b0;
                l_st          <= L_IDLE;
            end
            default: l_st <= L_IDLE;
        endcase
    end

    wire _unused = &{1'b0, s_axil_wstrb};
endmodule
