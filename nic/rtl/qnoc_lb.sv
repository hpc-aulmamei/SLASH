// qnoc_lb: QDMA streaming test logic behind qdma_noc_rm (nictest RM).
//
// One clock (the RM's service_clk, 250 MHz, in the nictest RM). Modes (CTRL[1:0]):
//   0 SINK      H2C packets are consumed and counted, C2H idle
//   1 LOOPBACK  every H2C packet returns as one C2H packet of the same length
//               (store and forward: C2H needs the length on its first beat,
//               H2C gives it on its last) with a completion entry
//   2 GEN       C2H packets of GEN_LEN bytes on GEN_QID, GEN_COUNT of them
//               (0: until CTRL.mode changes), H2C as in SINK
// In every mode H2C data can be checked against the pattern (CTRL.check):
// the 32-bit word at byte offset o of a packet is o ^ SEED. The generator
// sends the same pattern.
//
// C2H packets set has_cmpt; each gets a completion (CMPT_SIZE, default 1 =
// 16 B, the size the SLASH driver configures) of type HAS_PLD with
// wait_pld_pkt_id from one counter (PKT_ID, the id of the next packet; the
// CPM counts from 1 after its reset, so set it to match after reloading the
// RM). Entry layout as libqdma parses it: [2:0] 0 (format, color and error:
// the CPM inserts color/error, col_idx = err_idx = 0), [3] desc_used 1,
// [19:4] length, [20] eot 1. C2H packets must fit one C2H buffer (4096 B in
// the SLASH driver); longer H2C packets are still looped back and counted in
// LONG_PKTS.
//
// Registers (AXI4-Lite, 64 KiB, 32-bit; 64-bit counters low word first,
// cleared by CTRL.clear):
//   0x000 ID 0x514E4C42 ("QNLB")   0x004 VERSION   0x008 CAPS: [15:0] buffer beats
//   0x010 CTRL    [1:0] mode [4] check [5] cmpt_en (1) [8] clear (self-clearing)
//                 [12] qid_map (loopback: C2H qid = QID_MAP instead of the H2C qid)
//   0x014 STATUS  [0] framing error (qdma_noc_rm, sticky) [1] event overflow (sticky)
//                 [2] generator busy [3] loopback buffer empty
//   0x018 SEED    0x01C QID_MAP [11:0]
//   0x020 GEN_QID [11:0]   0x024 GEN_LEN bytes (1..65535)   0x028 GEN_COUNT
//   0x02C CMPT_SIZE [1:0]  0x030 PKT_ID [15:0] (next wait_pld_pkt_id; write to set)
//   0x040 CYCLES          0x048 H2C_PKTS        0x050 H2C_BYTES
//   0x058 H2C_ERR_PKTS    0x060 H2C_BAD_BEATS   0x068 H2C_FIRST (cycle)  0x070 H2C_LAST
//   0x078 C2H_PKTS        0x080 C2H_BYTES       0x088 C2H_FIRST          0x090 C2H_LAST
//   0x098 CMPT_SENT       0x0A0 STS_OK          0x0A8 STS_DROP           0x0B0 STS_ERR
//   0x0B8 LONG_PKTS       0x0C0 ZERO_PKTS (H2C zero_byte, not looped back)
//   0x0C8 DSC_STS         0x0D0 QSTS
//   0x0E0 LAST_H2C        [11:0] qid [31:16] length   0x0E4 LAST_C2H_STS [11:0] qid [16] drop [17] error
`timescale 1ns / 1ps

module qnoc_lb #(
    parameter integer BUF_BEATS = 512        // loopback buffer, 64 B beats (power of two)
) (
    input  logic         clk,
    input  logic         rstn,

    // qdma_noc_rm user side
    input  logic [511:0] h2c_tdata,
    input  logic         h2c_tvalid,
    output logic         h2c_tready,
    input  logic         h2c_tlast,
    input  logic [11:0]  h2c_qid,
    input  logic         h2c_err,
    input  logic [5:0]   h2c_mty,
    input  logic         h2c_zero_byte,

    output logic [511:0] c2h_tdata,
    output logic         c2h_tvalid,
    input  logic         c2h_tready,
    output logic         c2h_tlast,
    output logic [15:0]  c2h_ctrl_len,
    output logic [11:0]  c2h_ctrl_qid,
    output logic         c2h_ctrl_has_cmpt,

    output logic [511:0] cmpt_data,
    output logic         cmpt_tvalid,
    input  logic         cmpt_tready,
    output logic [1:0]   cmpt_size,
    output logic [12:0]  cmpt_qid,
    output logic [15:0]  cmpt_wait_pld_pkt_id,

    input  logic         c2h_sts_valid,
    input  logic         c2h_sts_drop,
    input  logic         c2h_sts_error,
    input  logic [11:0]  c2h_sts_qid,
    input  logic         dsc_sts_valid,
    input  logic         qsts_vld,
    input  logic         evt_overflow,
    input  logic         noc_status,

    // AXI4-Lite
    input  logic [15:0]  s_axil_awaddr,
    input  logic         s_axil_awvalid,
    output logic         s_axil_awready,
    input  logic [31:0]  s_axil_wdata,
    input  logic         s_axil_wvalid,
    output logic         s_axil_wready,
    output logic [1:0]   s_axil_bresp,
    output logic         s_axil_bvalid,
    input  logic         s_axil_bready,
    input  logic [15:0]  s_axil_araddr,
    input  logic         s_axil_arvalid,
    output logic         s_axil_arready,
    output logic [31:0]  s_axil_rdata,
    output logic [1:0]   s_axil_rresp,
    output logic         s_axil_rvalid,
    input  logic         s_axil_rready
);
    localparam logic [31:0] ID = 32'h514E_4C42, VERSION = 32'h0001_0000;
    localparam integer BA = $clog2(BUF_BEATS);

    // ------------------------------------------------------------ registers
    logic [1:0]  mode;
    logic        check_en, cmpt_en, qid_map_en, clear;
    logic [31:0] seed;
    logic [11:0] qid_map, gen_qid;
    logic [15:0] gen_len;
    logic [31:0] gen_count;
    logic [1:0]  cmpt_size_r;
    logic [15:0] pkt_id;           // next wait_pld_pkt_id
    logic        pkt_id_wr;
    logic [15:0] pkt_id_wdata;

    logic [63:0] cycles, h2c_pkts, h2c_bytes, h2c_err_pkts, h2c_bad_beats, h2c_first, h2c_last;
    logic [63:0] c2h_pkts, c2h_bytes, c2h_first, c2h_last, cmpt_sent;
    logic [63:0] sts_ok, sts_drop, sts_err, long_pkts, zero_pkts, dsc_cnt, qsts_cnt;
    logic        h2c_seen, c2h_seen, framing_err, ovf_err;
    logic [31:0] last_h2c, last_sts;
    logic        gen_busy, lb_empty, gen_start;

    // ------------------------------------------------------------ H2C: count, check
    logic        h2c_fire, h2c_sop;
    logic [15:0] h2c_beat;          // beat index in packet
    logic [15:0] h2c_len_now;       // bytes of this packet up to and including this beat
    assign h2c_fire = h2c_tvalid && h2c_tready;
    assign h2c_len_now = {h2c_beat[9:0], 6'd0} + 16'd64 - (h2c_tlast ? {10'd0, h2c_mty} : 16'd0);

    function automatic logic [511:0] pat(input logic [15:0] beat, input logic [31:0] s);
        for (int i = 0; i < 16; i++)
            pat[32*i +: 32] = ({16'd0, beat[9:0], 6'd0} + 32'(4 * i)) ^ s;
    endfunction

    // bytes of the last beat that are valid: mask the check
    logic [511:0] h2c_mask;
    always_comb begin
        for (int b = 0; b < 64; b++)
            h2c_mask[8*b +: 8] = (!h2c_tlast || b < 64 - h2c_mty) ? 8'hFF : 8'h00;
    end
    logic h2c_bad;
    assign h2c_bad = check_en && !h2c_zero_byte && (((h2c_tdata ^ pat(h2c_beat, seed)) & h2c_mask) != '0);

    // ------------------------------------------------------------ loopback buffer
    // data FIFO (BUF_BEATS x 513, block RAM, read latency 2) + packet FIFO (len, qid)
    logic         lb_in_ok;               // room in both FIFOs
    logic         lb_wr;
    logic [BA:0]  d_wr, d_rd;             // write / read pointers of the data RAM
    logic [512:0] dmem [0:BUF_BEATS-1];
    logic [BA:0]  d_used;                 // written, not yet issued for read
    assign d_used = d_wr - d_rd;

    logic         p_in_valid, p_in_ready, p_out_valid, p_out_ready;
    logic [27:0]  p_in, p_out;            // {qid, len}
    qnoc_fifo #(.W(28), .DEPTH(16)) u_pkt (
        .clk(clk), .rstn(rstn), .s_valid(p_in_valid), .s_ready(p_in_ready), .s_data(p_in),
        .m_valid(p_out_valid), .m_ready(p_out_ready), .m_data(p_out));

    // H2C ready: SINK/GEN always, LOOPBACK when the buffer can take the beat and,
    // on tlast, the packet FIFO the packet
    logic lb_mode;
    assign lb_mode    = (mode == 2'd1);
    assign lb_in_ok   = (d_used < BUF_BEATS - 4) && p_in_ready;
    assign h2c_tready = lb_mode ? lb_in_ok : 1'b1;
    assign lb_wr      = lb_mode && h2c_fire && !h2c_zero_byte;
    assign p_in_valid = lb_wr && h2c_tlast;
    assign p_in       = {qid_map_en ? qid_map : h2c_qid, h2c_len_now};

    always_ff @(posedge clk) begin
        if (lb_wr)
            dmem[d_wr[BA-1:0]] <= {h2c_tlast, h2c_tdata};
    end

    // read side: issue reads while the output FIFO (4) can hold them
    logic         rd_issue, rd_v1, rd_v2;
    logic [512:0] rd_q1, rd_q2;
    logic [2:0]   o_cnt;                  // entries in the output FIFO + reads in flight
    logic         o_valid, o_ready;
    logic [512:0] o_data;
    logic         o_in_ready;
    assign rd_issue = (d_wr != d_rd) && (o_cnt < 3'd4);
    always_ff @(posedge clk) begin
        rd_q1 <= dmem[d_rd[BA-1:0]];
        rd_q2 <= rd_q1;
    end
    always_ff @(posedge clk) begin
        if (!rstn) begin
            d_wr <= '0; d_rd <= '0; rd_v1 <= 1'b0; rd_v2 <= 1'b0; o_cnt <= '0;
        end else begin
            if (lb_wr) d_wr <= d_wr + 1'b1;
            if (rd_issue) d_rd <= d_rd + 1'b1;
            rd_v1 <= rd_issue;
            rd_v2 <= rd_v1;
            o_cnt <= o_cnt + rd_issue - (o_valid && o_ready);
        end
    end
    qnoc_fifo #(.W(513), .DEPTH(4)) u_out (
        .clk(clk), .rstn(rstn), .s_valid(rd_v2), .s_ready(o_in_ready), .s_data(rd_q2),
        .m_valid(o_valid), .m_ready(o_ready), .m_data(o_data));
    assign lb_empty = (d_wr == d_rd) && !o_valid && !p_out_valid;

    // ------------------------------------------------------------ generator
    logic [31:0] gen_left;               // packets left (GEN_COUNT 0: endless)
    logic        gen_endless;
    logic [15:0] g_beat;
    logic [15:0] g_beats;                 // beats of a packet
    assign g_beats = (gen_len + 16'd63) >> 6;

    // ------------------------------------------------------------ C2H out + completion
    logic        c_sop;                   // next C2H beat is the first of a packet
    logic        c_src_valid, c_src_last;
    logic [511:0] c_src_data;
    logic [15:0] c_len;
    logic [11:0] c_qid;
    logic        k_in_valid, k_in_ready;
    logic [44:0] k_in, k_out;             // {qid 13, pkt id 16, len 16}
    logic        k_out_valid;
    // the first beat also needs room for the completion
    logic        c_go;
    assign c_go = !c_sop || !cmpt_en || k_in_ready;

    always_comb begin
        c_src_valid = 1'b0; c_src_last = 1'b0; c_src_data = '0; c_len = '0; c_qid = '0;
        if (lb_mode) begin
            c_src_valid = o_valid && p_out_valid;
            c_src_last  = o_data[512];
            c_src_data  = o_data[511:0];
            c_len       = p_out[15:0];
            c_qid       = p_out[27:16];
        end else if (mode == 2'd2) begin
            c_src_valid = gen_busy;
            c_src_last  = (g_beat == g_beats - 1'b1);
            c_src_data  = pat(g_beat, seed);
            c_len       = gen_len;
            c_qid       = gen_qid;
        end
    end
    assign c2h_tvalid        = c_src_valid && c_go;
    assign c2h_tdata         = c_src_data;
    assign c2h_tlast         = c_src_last;
    assign c2h_ctrl_len      = c_len;
    assign c2h_ctrl_qid      = c_qid;
    assign c2h_ctrl_has_cmpt = cmpt_en;
    logic c2h_fire;
    assign c2h_fire    = c2h_tvalid && c2h_tready;
    assign o_ready     = lb_mode && c2h_fire;
    assign p_out_ready = lb_mode && c2h_fire && c_src_last;

    assign k_in_valid = c2h_fire && c_sop && cmpt_en;
    assign k_in       = {1'b0, c_qid, pkt_id, c_len};
    qnoc_fifo #(.W(45), .DEPTH(16)) u_cmpt (
        .clk(clk), .rstn(rstn), .s_valid(k_in_valid), .s_ready(k_in_ready), .s_data(k_in),
        .m_valid(k_out_valid), .m_ready(cmpt_tready), .m_data(k_out));
    assign cmpt_tvalid          = k_out_valid;
    assign cmpt_size            = cmpt_size_r;
    assign cmpt_qid             = k_out[44:32];
    assign cmpt_wait_pld_pkt_id = k_out[31:16];
    always_comb begin
        cmpt_data        = '0;
        cmpt_data[3]     = 1'b1;              // desc_used
        cmpt_data[19:4]  = k_out[15:0];       // length
        cmpt_data[20]    = 1'b1;              // eot
    end

    // ------------------------------------------------------------ state and counters
    always_ff @(posedge clk) begin
        if (!rstn) begin
            h2c_beat <= '0; c_sop <= 1'b1; g_beat <= '0; gen_busy <= 1'b0; gen_left <= '0;
            gen_endless <= 1'b0; pkt_id <= 16'd1;
            framing_err <= 1'b0; ovf_err <= 1'b0;
        end else begin
            if (h2c_fire) h2c_beat <= h2c_tlast ? '0 : h2c_beat + 1'b1;
            if (c2h_fire) c_sop <= c_src_last;
            if (pkt_id_wr) pkt_id <= pkt_id_wdata;
            else if (k_in_valid && k_in_ready) pkt_id <= pkt_id + 1'b1;
            // generator: started by writing GEN_COUNT (see register writes)
            if (mode != 2'd2) begin
                gen_busy <= 1'b0; g_beat <= '0;
            end else if (gen_busy && c2h_fire) begin
                if (c_src_last) begin
                    g_beat <= '0;
                    if (!gen_endless) begin
                        gen_left <= gen_left - 1'b1;
                        if (gen_left == 32'd1) gen_busy <= 1'b0;
                    end
                end else begin
                    g_beat <= g_beat + 1'b1;
                end
            end
            if (gen_start) begin
                gen_busy <= (gen_len != 0); gen_left <= gen_count; gen_endless <= (gen_count == 0); g_beat <= '0;
            end
            if (noc_status) framing_err <= 1'b1;
            if (evt_overflow) ovf_err <= 1'b1;
        end
    end

    always_ff @(posedge clk) begin
        if (!rstn || clear) begin
            cycles <= '0; h2c_pkts <= '0; h2c_bytes <= '0; h2c_err_pkts <= '0; h2c_bad_beats <= '0;
            h2c_first <= '0; h2c_last <= '0; c2h_pkts <= '0; c2h_bytes <= '0; c2h_first <= '0;
            c2h_last <= '0; cmpt_sent <= '0; sts_ok <= '0; sts_drop <= '0; sts_err <= '0;
            long_pkts <= '0; zero_pkts <= '0; dsc_cnt <= '0; qsts_cnt <= '0;
            h2c_seen <= 1'b0; c2h_seen <= 1'b0; last_h2c <= '0; last_sts <= '0;
        end else begin
            cycles <= cycles + 1'b1;
            if (h2c_fire) begin
                if (!h2c_seen) h2c_first <= cycles;
                h2c_seen <= 1'b1;
                if (h2c_bad) h2c_bad_beats <= h2c_bad_beats + 1'b1;
                if (h2c_tlast) begin
                    h2c_last <= cycles;
                    h2c_pkts <= h2c_pkts + 1'b1;
                    if (h2c_zero_byte) zero_pkts <= zero_pkts + 1'b1;
                    else h2c_bytes <= h2c_bytes + h2c_len_now;
                    if (h2c_err) h2c_err_pkts <= h2c_err_pkts + 1'b1;
                    if (!h2c_zero_byte && h2c_len_now > 16'd4096) long_pkts <= long_pkts + 1'b1;
                    last_h2c <= {h2c_zero_byte ? 16'd0 : h2c_len_now, 4'd0, h2c_qid};
                end
            end
            if (c2h_fire) begin
                if (!c2h_seen) c2h_first <= cycles;
                c2h_seen <= 1'b1;
                if (c_src_last) begin
                    c2h_last <= cycles;
                    c2h_pkts <= c2h_pkts + 1'b1;
                    c2h_bytes <= c2h_bytes + c_len;
                end
            end
            if (cmpt_tvalid && cmpt_tready) cmpt_sent <= cmpt_sent + 1'b1;
            if (c2h_sts_valid) begin
                if (c2h_sts_drop) sts_drop <= sts_drop + 1'b1;
                else if (c2h_sts_error) sts_err <= sts_err + 1'b1;
                else sts_ok <= sts_ok + 1'b1;
                last_sts <= {14'd0, c2h_sts_error, c2h_sts_drop, 4'd0, c2h_sts_qid};
            end
            if (dsc_sts_valid) dsc_cnt <= dsc_cnt + 1'b1;
            if (qsts_vld) qsts_cnt <= qsts_cnt + 1'b1;
        end
    end

    // ------------------------------------------------------------ AXI4-Lite
    logic        aw_ok, w_ok;
    logic [15:0] aw_addr;
    logic [31:0] w_data;
    assign s_axil_awready = !aw_ok && !s_axil_bvalid;
    assign s_axil_wready  = !w_ok && !s_axil_bvalid;
    assign s_axil_bresp   = 2'b00;
    assign s_axil_rresp   = 2'b00;
    wire do_wr = aw_ok && w_ok && !s_axil_bvalid;

    always_ff @(posedge clk) begin
        if (!rstn) begin
            aw_ok <= 1'b0; w_ok <= 1'b0; s_axil_bvalid <= 1'b0;
            mode <= 2'd0; check_en <= 1'b0; cmpt_en <= 1'b1; qid_map_en <= 1'b0; clear <= 1'b0;
            seed <= '0; qid_map <= '0; gen_qid <= '0; gen_len <= 16'd64; gen_count <= '0;
            cmpt_size_r <= 2'd1; pkt_id_wr <= 1'b0; pkt_id_wdata <= '0; gen_start <= 1'b0;
        end else begin
            clear <= 1'b0; pkt_id_wr <= 1'b0; gen_start <= 1'b0;
            if (s_axil_awvalid && s_axil_awready) begin aw_ok <= 1'b1; aw_addr <= s_axil_awaddr; end
            if (s_axil_wvalid && s_axil_wready) begin w_ok <= 1'b1; w_data <= s_axil_wdata; end
            if (s_axil_bvalid && s_axil_bready) s_axil_bvalid <= 1'b0;
            if (do_wr) begin
                aw_ok <= 1'b0; w_ok <= 1'b0; s_axil_bvalid <= 1'b1;
                case ({aw_addr[15:2], 2'b00})
                    16'h010: begin
                        mode <= w_data[1:0]; check_en <= w_data[4]; cmpt_en <= w_data[5];
                        clear <= w_data[8]; qid_map_en <= w_data[12];
                    end
                    16'h018: seed <= w_data;
                    16'h01C: qid_map <= w_data[11:0];
                    16'h020: gen_qid <= w_data[11:0];
                    16'h024: gen_len <= w_data[15:0];
                    16'h028: begin gen_count <= w_data; gen_start <= 1'b1; end
                    16'h02C: cmpt_size_r <= w_data[1:0];
                    16'h030: begin pkt_id_wr <= 1'b1; pkt_id_wdata <= w_data[15:0]; end
                    default: ;
                endcase
            end
        end
    end

    logic [63:0] rd64;
    always_comb begin
        case ({s_axil_araddr[15:3], 3'b000})
            16'h040: rd64 = cycles;        16'h048: rd64 = h2c_pkts;      16'h050: rd64 = h2c_bytes;
            16'h058: rd64 = h2c_err_pkts;  16'h060: rd64 = h2c_bad_beats; 16'h068: rd64 = h2c_first;
            16'h070: rd64 = h2c_last;      16'h078: rd64 = c2h_pkts;      16'h080: rd64 = c2h_bytes;
            16'h088: rd64 = c2h_first;     16'h090: rd64 = c2h_last;      16'h098: rd64 = cmpt_sent;
            16'h0A0: rd64 = sts_ok;        16'h0A8: rd64 = sts_drop;      16'h0B0: rd64 = sts_err;
            16'h0B8: rd64 = long_pkts;     16'h0C0: rd64 = zero_pkts;     16'h0C8: rd64 = dsc_cnt;
            16'h0D0: rd64 = qsts_cnt;
            default: rd64 = '0;
        endcase
    end

    always_ff @(posedge clk) begin
        if (!rstn) begin
            s_axil_arready <= 1'b0; s_axil_rvalid <= 1'b0; s_axil_rdata <= '0;
        end else begin
            s_axil_arready <= 1'b0;
            if (s_axil_rvalid && s_axil_rready) s_axil_rvalid <= 1'b0;
            if (s_axil_arvalid && !s_axil_arready && !s_axil_rvalid) begin
                s_axil_arready <= 1'b1;
                s_axil_rvalid  <= 1'b1;
                if (s_axil_araddr >= 16'h040 && s_axil_araddr < 16'h0D8)
                    s_axil_rdata <= s_axil_araddr[2] ? rd64[63:32] : rd64[31:0];
                else case ({s_axil_araddr[15:2], 2'b00})
                    16'h000: s_axil_rdata <= ID;
                    16'h004: s_axil_rdata <= VERSION;
                    16'h008: s_axil_rdata <= BUF_BEATS;
                    16'h010: s_axil_rdata <= {19'd0, qid_map_en, 6'd0, cmpt_en, check_en, 2'd0, mode};
                    16'h014: s_axil_rdata <= {28'd0, lb_empty, gen_busy, ovf_err, framing_err};
                    16'h018: s_axil_rdata <= seed;
                    16'h01C: s_axil_rdata <= {20'd0, qid_map};
                    16'h020: s_axil_rdata <= {20'd0, gen_qid};
                    16'h024: s_axil_rdata <= {16'd0, gen_len};
                    16'h028: s_axil_rdata <= gen_count;
                    16'h02C: s_axil_rdata <= {30'd0, cmpt_size_r};
                    16'h030: s_axil_rdata <= {16'd0, pkt_id};
                    16'h0E0: s_axil_rdata <= last_h2c;
                    16'h0E4: s_axil_rdata <= last_sts;
                    default: s_axil_rdata <= 32'd0;
                endcase
            end
        end
    end
endmodule
