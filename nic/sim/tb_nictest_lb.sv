// Test of the nictest RM's QDMA streaming loopback, end to end:
//   CPM model <-> qdma_noc_bridge (static, DMA clock 390 MHz)
//             <-> NoC model (noc_ch of shell/sim/tb_qdma_noc.sv, random stalls)
//             <-> qnoc_lb_bd (qdma_noc_rm + qnoc_lb, RM clock) <-> AXI4-Lite master.
// Phases: registers; LOOPBACK of random H2C packets (1..4096 B, zero-byte and
// error packets among them, pattern checked) with every C2H packet checked at
// the CPM (data, len, qid, mty, tcrc against a serial CRC, ecc against the CPM5
// example design's encoder) and every completion (size, HAS_PLD, qid,
// wait_pld_pkt_id sequence, entry layout, dpar); a corrupted H2C packet for the
// checker; GEN mode; counters read back over AXI4-Lite.
`timescale 1ns / 1ps

module tb_nictest_lb;
    localparam [31:0] POLY = 32'h04C11DB7;
    localparam integer N_LB = 400, N_GEN = 40, GEN_LEN = 1000;
    localparam [31:0] SEED = 32'hC0DE_5EED;
    localparam integer LB = 32'h0000;   // qnoc_lb register base in the AXI4-Lite space

    reg dma_clk = 0, rm_clk = 0, rstn = 0;
    always #1.282 dma_clk = ~dma_clk;   // 390 MHz
    initial #0.7 forever #2.0 rm_clk = ~rm_clk;     // the RM's service_clk, 250 MHz

    // ---------------------------------------------------------------- CPM side
    reg  [511:0] cpm_h2c_tdata;  reg [31:0] cpm_h2c_tcrc;  reg cpm_h2c_tvalid = 0, cpm_h2c_tlast;
    wire         cpm_h2c_tready; reg [11:0] cpm_h2c_qid;   reg [2:0] cpm_h2c_port_id = 0; reg cpm_h2c_err;
    reg  [31:0]  cpm_h2c_mdata = 0; reg [5:0] cpm_h2c_mty; reg cpm_h2c_zero_byte;
    wire [511:0] cpm_c2h_tdata;  wire [31:0] cpm_c2h_tcrc; wire cpm_c2h_ctrl_marker; wire [2:0] cpm_c2h_ctrl_port_id;
    wire [15:0]  cpm_c2h_ctrl_len; wire [11:0] cpm_c2h_ctrl_qid; wire cpm_c2h_ctrl_has_cmpt; wire [5:0] cpm_c2h_mty;
    wire         cpm_c2h_tvalid, cpm_c2h_tlast; reg cpm_c2h_tready = 0; wire [6:0] cpm_c2h_ecc;
    wire [511:0] cpm_cmpt_data;  wire [15:0] cpm_cmpt_dpar; wire cpm_cmpt_tvalid; reg cpm_cmpt_tready = 0;
    wire [1:0]   cpm_cmpt_size;  wire [2:0] cpm_cmpt_col_idx, cpm_cmpt_err_idx; wire [12:0] cpm_cmpt_qid;
    wire [1:0]   cpm_cmpt_cmpt_type; wire [15:0] cpm_cmpt_wait_pld_pkt_id; wire [2:0] cpm_cmpt_port_id;
    wire         cpm_cmpt_marker, cpm_cmpt_user_trig, cpm_cmpt_no_wrb_marker;
    reg          cpm_c2h_sts_valid = 0, cpm_c2h_sts_drop = 0, cpm_c2h_sts_last = 1, cpm_c2h_sts_status_cmp = 0,
                 cpm_c2h_sts_error = 0;
    reg  [11:0]  cpm_c2h_sts_qid = 0;
    wire         cpm_irq_valid; wire [10:0] cpm_irq_vec; wire [12:0] cpm_irq_fnc;
    wire         cpm_dsc_sts_rdy, cpm_qsts_rdy;
    wire         cpm_crdt_valid; wire cpm_crdt_dir; wire [15:0] cpm_crdt_crdt; wire [11:0] cpm_crdt_qid; wire cpm_crdt_fence;
    wire [3:0]   st_status;

    // NoC ports: static side (st_*) and RM side (rm_*)
    wire st_h2c0_v, st_h2c0_r, st_h2c0_l, st_h2c1_v, st_h2c1_r, st_h2c1_l, st_evt_v, st_evt_r, st_evt_l;
    wire [511:0] st_h2c0_d, st_h2c1_d; wire [255:0] st_evt_d;
    wire st_c2h0_v, st_c2h0_r, st_c2h0_l, st_c2h1_v, st_c2h1_r, st_c2h1_l, st_cmpt_v, st_cmpt_r, st_cmpt_l, st_cmd_v, st_cmd_r, st_cmd_l;
    wire [511:0] st_c2h0_d, st_c2h1_d, st_cmpt_d; wire [255:0] st_cmd_d;
    wire rm_h2c0_v, rm_h2c0_r, rm_h2c0_l, rm_h2c1_v, rm_h2c1_r, rm_h2c1_l, rm_evt_v, rm_evt_r, rm_evt_l;
    wire [511:0] rm_h2c0_d, rm_h2c1_d; wire [255:0] rm_evt_d;
    wire rm_c2h0_v, rm_c2h0_r, rm_c2h0_l, rm_c2h1_v, rm_c2h1_r, rm_c2h1_l, rm_cmpt_v, rm_cmpt_r, rm_cmpt_l, rm_cmd_v, rm_cmd_r, rm_cmd_l;
    wire [511:0] rm_c2h0_d, rm_c2h1_d, rm_cmpt_d; wire [255:0] rm_cmd_d;
    wire [63:0] k64 = {64{1'b1}}; wire [31:0] k32 = {32{1'b1}};

    qdma_noc_bridge #(.STAGES(2)) dut_st (
        .clk(dma_clk), .rstn(rstn),
        .cpm_h2c_tdata(cpm_h2c_tdata), .cpm_h2c_tcrc(cpm_h2c_tcrc), .cpm_h2c_tvalid(cpm_h2c_tvalid),
        .cpm_h2c_tlast(cpm_h2c_tlast), .cpm_h2c_tready(cpm_h2c_tready), .cpm_h2c_qid(cpm_h2c_qid),
        .cpm_h2c_port_id(cpm_h2c_port_id), .cpm_h2c_err(cpm_h2c_err), .cpm_h2c_mdata(cpm_h2c_mdata),
        .cpm_h2c_mty(cpm_h2c_mty), .cpm_h2c_zero_byte(cpm_h2c_zero_byte),
        .cpm_c2h_tdata(cpm_c2h_tdata), .cpm_c2h_tcrc(cpm_c2h_tcrc), .cpm_c2h_ctrl_marker(cpm_c2h_ctrl_marker),
        .cpm_c2h_ctrl_port_id(cpm_c2h_ctrl_port_id), .cpm_c2h_ctrl_len(cpm_c2h_ctrl_len),
        .cpm_c2h_ctrl_qid(cpm_c2h_ctrl_qid), .cpm_c2h_ctrl_has_cmpt(cpm_c2h_ctrl_has_cmpt),
        .cpm_c2h_mty(cpm_c2h_mty), .cpm_c2h_tvalid(cpm_c2h_tvalid), .cpm_c2h_tlast(cpm_c2h_tlast),
        .cpm_c2h_tready(cpm_c2h_tready), .cpm_c2h_ecc(cpm_c2h_ecc),
        .cpm_cmpt_data(cpm_cmpt_data), .cpm_cmpt_dpar(cpm_cmpt_dpar), .cpm_cmpt_tvalid(cpm_cmpt_tvalid),
        .cpm_cmpt_tready(cpm_cmpt_tready), .cpm_cmpt_size(cpm_cmpt_size), .cpm_cmpt_col_idx(cpm_cmpt_col_idx),
        .cpm_cmpt_err_idx(cpm_cmpt_err_idx), .cpm_cmpt_qid(cpm_cmpt_qid), .cpm_cmpt_cmpt_type(cpm_cmpt_cmpt_type),
        .cpm_cmpt_wait_pld_pkt_id(cpm_cmpt_wait_pld_pkt_id), .cpm_cmpt_port_id(cpm_cmpt_port_id),
        .cpm_cmpt_marker(cpm_cmpt_marker), .cpm_cmpt_user_trig(cpm_cmpt_user_trig),
        .cpm_cmpt_no_wrb_marker(cpm_cmpt_no_wrb_marker),
        .cpm_c2h_sts_valid(cpm_c2h_sts_valid), .cpm_c2h_sts_drop(cpm_c2h_sts_drop), .cpm_c2h_sts_qid(cpm_c2h_sts_qid),
        .cpm_c2h_sts_last(cpm_c2h_sts_last), .cpm_c2h_sts_status_cmp(cpm_c2h_sts_status_cmp),
        .cpm_c2h_sts_error(cpm_c2h_sts_error),
        .cpm_irq_valid(cpm_irq_valid), .cpm_irq_vec(cpm_irq_vec), .cpm_irq_fnc(cpm_irq_fnc),
        .cpm_irq_ack(1'b0), .cpm_irq_fail(1'b0),
        .cpm_dsc_sts_valid(1'b0), .cpm_dsc_sts_rdy(cpm_dsc_sts_rdy), .cpm_dsc_sts_byp(1'b0),
        .cpm_dsc_sts_dir(1'b0), .cpm_dsc_sts_mm(1'b0), .cpm_dsc_sts_qid(12'd0),
        .cpm_dsc_sts_avl(16'd0), .cpm_dsc_sts_pidx(16'd0), .cpm_dsc_sts_port_id(3'd0),
        .cpm_dsc_sts_qinv(1'b0), .cpm_dsc_sts_irq_arm(1'b0), .cpm_dsc_sts_error(1'b0), .cpm_dsc_sts_qen(1'b0),
        .cpm_qsts_vld(1'b0), .cpm_qsts_rdy(cpm_qsts_rdy), .cpm_qsts_op(8'd0),
        .cpm_qsts_data(64'd0), .cpm_qsts_port_id(3'd0), .cpm_qsts_qid(13'd0),
        .cpm_crdt_valid(cpm_crdt_valid), .cpm_crdt_rdy(1'b1), .cpm_crdt_dir(cpm_crdt_dir),
        .cpm_crdt_crdt(cpm_crdt_crdt), .cpm_crdt_qid(cpm_crdt_qid), .cpm_crdt_fence(cpm_crdt_fence),
        .m_h2c0_tvalid(st_h2c0_v), .m_h2c0_tready(st_h2c0_r), .m_h2c0_tdata(st_h2c0_d), .m_h2c0_tkeep(), .m_h2c0_tlast(st_h2c0_l),
        .m_h2c1_tvalid(st_h2c1_v), .m_h2c1_tready(st_h2c1_r), .m_h2c1_tdata(st_h2c1_d), .m_h2c1_tkeep(), .m_h2c1_tlast(st_h2c1_l),
        .m_evt_tvalid(st_evt_v), .m_evt_tready(st_evt_r), .m_evt_tdata(st_evt_d), .m_evt_tkeep(), .m_evt_tlast(st_evt_l),
        .s_c2h0_tvalid(st_c2h0_v), .s_c2h0_tready(st_c2h0_r), .s_c2h0_tdata(st_c2h0_d), .s_c2h0_tkeep(k64), .s_c2h0_tlast(st_c2h0_l),
        .s_c2h1_tvalid(st_c2h1_v), .s_c2h1_tready(st_c2h1_r), .s_c2h1_tdata(st_c2h1_d), .s_c2h1_tkeep(k64), .s_c2h1_tlast(st_c2h1_l),
        .s_cmpt_tvalid(st_cmpt_v), .s_cmpt_tready(st_cmpt_r), .s_cmpt_tdata(st_cmpt_d), .s_cmpt_tkeep(k64), .s_cmpt_tlast(st_cmpt_l),
        .s_cmd_tvalid(st_cmd_v), .s_cmd_tready(st_cmd_r), .s_cmd_tdata(st_cmd_d), .s_cmd_tkeep(k32), .s_cmd_tlast(st_cmd_l),
        .status(st_status));

    noc_ch #(512) n_h2c0 (dma_clk, rstn, st_h2c0_v, st_h2c0_r, st_h2c0_d, st_h2c0_l, rm_clk, rm_h2c0_v, rm_h2c0_r, rm_h2c0_d, rm_h2c0_l);
    noc_ch #(512) n_h2c1 (dma_clk, rstn, st_h2c1_v, st_h2c1_r, st_h2c1_d, st_h2c1_l, rm_clk, rm_h2c1_v, rm_h2c1_r, rm_h2c1_d, rm_h2c1_l);
    noc_ch #(256) n_evt  (dma_clk, rstn, st_evt_v,  st_evt_r,  st_evt_d,  st_evt_l,  rm_clk, rm_evt_v,  rm_evt_r,  rm_evt_d,  rm_evt_l);
    noc_ch #(512) n_c2h0 (rm_clk, rstn, rm_c2h0_v, rm_c2h0_r, rm_c2h0_d, rm_c2h0_l, dma_clk, st_c2h0_v, st_c2h0_r, st_c2h0_d, st_c2h0_l);
    noc_ch #(512) n_c2h1 (rm_clk, rstn, rm_c2h1_v, rm_c2h1_r, rm_c2h1_d, rm_c2h1_l, dma_clk, st_c2h1_v, st_c2h1_r, st_c2h1_d, st_c2h1_l);
    noc_ch #(512) n_cmpt (rm_clk, rstn, rm_cmpt_v, rm_cmpt_r, rm_cmpt_d, rm_cmpt_l, dma_clk, st_cmpt_v, st_cmpt_r, st_cmpt_d, st_cmpt_l);
    noc_ch #(256) n_cmd  (rm_clk, rstn, rm_cmd_v,  rm_cmd_r,  rm_cmd_d,  rm_cmd_l,  dma_clk, st_cmd_v,  st_cmd_r,  st_cmd_d,  st_cmd_l);
    initial begin
        #1;
        n_h2c0.p_stall = 15; n_h2c1.p_stall = 15; n_c2h0.p_stall = 15; n_c2h1.p_stall = 15; n_cmpt.p_stall = 15;
        n_h2c1.lat_ns = 140.0; n_c2h0.lat_ns = 150.0;
        n_evt.gap_ns = 2.0; n_cmd.gap_ns = 2.0;
    end

    // ---------------------------------------------------------------- RM: qnoc_lb_bd
    reg  [15:0] awaddr, araddr; reg awvalid = 0, wvalid = 0, bready = 0, arvalid = 0, rready = 0;
    reg  [31:0] wdata;
    wire awready, wready, bvalid, arready, rvalid; wire [1:0] bresp, rresp; wire [31:0] rdata;
    qnoc_lb_bd dut_rm (
        .clk(rm_clk), .rstn(rstn),
        .s_h2c0_tvalid(rm_h2c0_v), .s_h2c0_tready(rm_h2c0_r), .s_h2c0_tdata(rm_h2c0_d), .s_h2c0_tkeep(k64), .s_h2c0_tlast(rm_h2c0_l),
        .s_h2c1_tvalid(rm_h2c1_v), .s_h2c1_tready(rm_h2c1_r), .s_h2c1_tdata(rm_h2c1_d), .s_h2c1_tkeep(k64), .s_h2c1_tlast(rm_h2c1_l),
        .s_evt_tvalid(rm_evt_v), .s_evt_tready(rm_evt_r), .s_evt_tdata(rm_evt_d), .s_evt_tkeep(k32), .s_evt_tlast(rm_evt_l),
        .m_c2h0_tvalid(rm_c2h0_v), .m_c2h0_tready(rm_c2h0_r), .m_c2h0_tdata(rm_c2h0_d), .m_c2h0_tkeep(), .m_c2h0_tlast(rm_c2h0_l),
        .m_c2h1_tvalid(rm_c2h1_v), .m_c2h1_tready(rm_c2h1_r), .m_c2h1_tdata(rm_c2h1_d), .m_c2h1_tkeep(), .m_c2h1_tlast(rm_c2h1_l),
        .m_cmpt_tvalid(rm_cmpt_v), .m_cmpt_tready(rm_cmpt_r), .m_cmpt_tdata(rm_cmpt_d), .m_cmpt_tkeep(), .m_cmpt_tlast(rm_cmpt_l),
        .m_cmd_tvalid(rm_cmd_v), .m_cmd_tready(rm_cmd_r), .m_cmd_tdata(rm_cmd_d), .m_cmd_tkeep(), .m_cmd_tlast(rm_cmd_l),
        .s_axil_awaddr(awaddr), .s_axil_awvalid(awvalid), .s_axil_awready(awready), .s_axil_wdata(wdata),
        .s_axil_wstrb(4'hF), .s_axil_wvalid(wvalid), .s_axil_wready(wready), .s_axil_bresp(bresp),
        .s_axil_bvalid(bvalid), .s_axil_bready(bready), .s_axil_araddr(araddr), .s_axil_arvalid(arvalid),
        .s_axil_arready(arready), .s_axil_rdata(rdata), .s_axil_rresp(rresp), .s_axil_rvalid(rvalid),
        .s_axil_rready(rready));

    task automatic axil_wr(input [15:0] a, input [31:0] d);
        @(posedge rm_clk); awaddr <= a; awvalid <= 1; wdata <= d; wvalid <= 1;
        fork
            begin do @(posedge rm_clk); while (!awready); awvalid <= 0; end
            begin do @(posedge rm_clk); while (!wready); wvalid <= 0; end
        join
        bready <= 1; do @(posedge rm_clk); while (!bvalid); bready <= 0;
    endtask
    task automatic axil_rd(input [15:0] a, output [31:0] d);
        @(posedge rm_clk); araddr <= a; arvalid <= 1;
        do @(posedge rm_clk); while (!arready); arvalid <= 0;
        rready <= 1; while (!rvalid) @(posedge rm_clk); d = rdata; @(posedge rm_clk); rready <= 0;
    endtask
    task automatic axil_rd64(input [15:0] a, output [63:0] d);
        reg [31:0] lo, hi;
        axil_rd(a, lo); axil_rd(a + 4, hi); d = {hi, lo};
    endtask

    // ---------------------------------------------------------------- scoreboard
    integer errors = 0;
    task automatic fail(input string s);
        errors = errors + 1;
        if (errors <= 25) $display("[%0t] ERROR %s", $time, s);
    endtask
    function automatic [31:0] crc_beat(input [31:0] c, input [511:0] d);
        integer i; reg fb;
        for (i = 0; i < 512; i = i + 1) begin
            fb = c[31] ^ d[511 - i];
            c  = {c[30:0], 1'b0} ^ (fb ? POLY : 32'd0);
        end
        return c;
    endfunction
    function automatic [511:0] pat(input integer beat, input [31:0] s);
        for (int i = 0; i < 16; i++) pat[32*i +: 32] = (32'(beat * 64 + 4 * i)) ^ s;
    endfunction
    function automatic [511:0] keep_mask(input integer nbytes);   // nbytes valid of 64
        for (int b = 0; b < 64; b++) keep_mask[8*b +: 8] = (b < nbytes) ? 8'hFF : 8'h00;
    endfunction

    typedef struct { integer len; logic [11:0] qid; } pkt_t;
    pkt_t   exp_q[$];               // C2H packets expected at the CPM, in order
    integer n_c2h = 0, cnt_cmpt = 0, n_bad_sent = 0, n_zero = 0, n_err = 0, n_lb_pkts = 0, lb_bytes = 0;
    logic [15:0] exp_pkt_id = 16'd1;
    integer corrupt_idx = -1;      // C2H packet number of the corrupted H2C packet

    // ---- CPM H2C source ----
    task automatic send_h2c(input integer len, input [11:0] qid, input bit zb, input bit err, input bit corrupt);
        integer nb, k; reg [31:0] crc;
        nb = zb ? 1 : (len + 63) / 64;
        crc = 32'hFFFF_FFFF;
        for (k = 0; k < nb; k = k + 1) begin
            reg [511:0] d; integer valid;
            valid = zb ? 0 : ((k == nb - 1) ? len - 64 * k : 64);
            d = pat(k, SEED) & keep_mask(valid);
            if (corrupt && k == 0) d[7:0] = ~d[7:0];
            crc = crc_beat(crc, d);
            cpm_h2c_tdata <= d; cpm_h2c_tvalid <= 1; cpm_h2c_tlast <= (k == nb - 1);
            cpm_h2c_qid <= qid; cpm_h2c_err <= err; cpm_h2c_zero_byte <= zb;
            cpm_h2c_mty <= zb ? 6'd0 : 6'(64 - valid) & 6'h3F;
            cpm_h2c_tcrc <= crc;
            do @(posedge dma_clk); while (!cpm_h2c_tready);
            cpm_h2c_tvalid <= 0;
            if ($urandom % 100 < 20) repeat ($urandom % 3) @(posedge dma_clk);
        end
    endtask

    // golden ECC (CPM5 example design encoder)
    wire [6:0]  ecc_ref; wire [56:0] ecc_dout;
    qdma_ecc_enc #(.C_DATA_WIDTH(57), .C_CHK_BIT_WIDTH(7), .C_REG_INPUT(0), .C_REG_OUTPUT(0), .C_PIPELINE(0), .C_USE_CLKEN(0))
        u_ecc_ref (.ecc_clk(1'b0), .ecc_reset(1'b0), .ecc_enc_clk_en_in(1'b1),
                   .ecc_enc_data_in({24'd0, cpm_c2h_ctrl_has_cmpt, cpm_c2h_ctrl_marker, cpm_c2h_ctrl_port_id,
                                     cpm_c2h_ctrl_qid, cpm_c2h_ctrl_len}),
                   .ecc_enc_data_out(ecc_dout), .ecc_enc_chk_bits_out(ecc_ref));

    // ---- CPM C2H sink: checks against exp_q ----
    always @(posedge dma_clk) cpm_c2h_tready <= ($urandom % 100) >= 20;
    always @(posedge dma_clk) cpm_cmpt_tready <= ($urandom % 100) >= 25;
    integer c_beat = 0; pkt_t c_cur; reg [31:0] c_crc;
    always @(posedge dma_clk) begin
        cpm_c2h_sts_valid <= 1'b0;
        if (cpm_c2h_tvalid && cpm_c2h_tready) begin
            reg [511:0] d, exp_d; integer valid;
            if (c_beat == 0) begin
                if (exp_q.size() == 0) begin fail("unexpected C2H packet"); c_cur = '{0, 0}; end
                else c_cur = exp_q.pop_front();
                c_crc = 32'hFFFF_FFFF;
                if (cpm_c2h_ctrl_len != c_cur.len) fail($sformatf("C2H len %0d, expected %0d", cpm_c2h_ctrl_len, c_cur.len));
                if (cpm_c2h_ctrl_qid != c_cur.qid) fail($sformatf("C2H qid %0d, expected %0d", cpm_c2h_ctrl_qid, c_cur.qid));
                if (!cpm_c2h_ctrl_has_cmpt) fail("C2H has_cmpt 0");
                if (cpm_c2h_ecc !== ecc_ref) fail($sformatf("C2H ecc %h, expected %h", cpm_c2h_ecc, ecc_ref));
            end
            valid = (c_cur.len - 64 * c_beat >= 64) ? 64 : c_cur.len - 64 * c_beat;
            d = cpm_c2h_tdata & keep_mask(valid);
            exp_d = pat(c_beat, SEED) & keep_mask(valid);
            if (n_c2h == corrupt_idx && c_beat == 0) exp_d[7:0] = ~exp_d[7:0];
            if (d !== exp_d)
                fail($sformatf("C2H data, packet %0d beat %0d", n_c2h, c_beat));
            c_crc = crc_beat(c_crc, d);
            if (cpm_c2h_tlast != (64 * (c_beat + 1) >= c_cur.len))
                fail($sformatf("C2H tlast %0d at beat %0d of %0d B", cpm_c2h_tlast, c_beat, c_cur.len));
            if (cpm_c2h_tlast) begin
                if (cpm_c2h_mty != 6'(64 - valid)) fail($sformatf("C2H mty %0d, expected %0d", cpm_c2h_mty, 64 - valid));
                if (cpm_c2h_tcrc != c_crc) fail($sformatf("C2H tcrc %h, expected %h", cpm_c2h_tcrc, c_crc));
                n_c2h = n_c2h + 1;
                c_beat = 0;
                // the CPM's status for the packet: every 7th dropped
                cpm_c2h_sts_valid <= 1'b1; cpm_c2h_sts_qid <= c_cur.qid;
                cpm_c2h_sts_drop <= (n_c2h % 7 == 0);
            end else begin
                c_beat = c_beat + 1;
            end
        end
    end
    // ---- CPM CMPT sink: one entry per C2H packet, in order ----
    pkt_t cmpt_exp_q[$];
    always @(posedge dma_clk) begin
        if (cpm_cmpt_tvalid && cpm_cmpt_tready) begin
            pkt_t e; reg [15:0] par;
            if (cmpt_exp_q.size() == 0) begin fail("unexpected CMPT"); e = '{0, 0}; end
            else e = cmpt_exp_q.pop_front();
            if (cpm_cmpt_size != 2'd1) fail($sformatf("CMPT size %0d", cpm_cmpt_size));
            if (cpm_cmpt_cmpt_type != 2'd3) fail($sformatf("CMPT type %0d", cpm_cmpt_cmpt_type));
            if (cpm_cmpt_qid != {1'b0, e.qid}) fail($sformatf("CMPT qid %0d, expected %0d", cpm_cmpt_qid, e.qid));
            if (cpm_cmpt_wait_pld_pkt_id != exp_pkt_id)
                fail($sformatf("CMPT pkt id %0d, expected %0d", cpm_cmpt_wait_pld_pkt_id, exp_pkt_id));
            if (cpm_cmpt_data[2:0] != 3'd0 || cpm_cmpt_data[3] != 1'b1 || cpm_cmpt_data[19:4] != e.len[15:0]
                || cpm_cmpt_data[20] != 1'b1 || cpm_cmpt_data[127:21] != '0)
                fail($sformatf("CMPT entry %h, len %0d", cpm_cmpt_data[127:0], e.len));
            if (cpm_cmpt_col_idx != 0 || cpm_cmpt_err_idx != 0 || cpm_cmpt_user_trig || cpm_cmpt_marker)
                fail("CMPT control fields");
            for (int i = 0; i < 16; i++) par[i] = ~^cpm_cmpt_data[32*i +: 32];
            if (cpm_cmpt_dpar != par) fail($sformatf("CMPT dpar %h, expected %h", cpm_cmpt_dpar, par));
            exp_pkt_id = exp_pkt_id + 1;
            cnt_cmpt = cnt_cmpt + 1;
        end
    end

    // ---------------------------------------------------------------- sequence
    initial begin
        reg [31:0] r; reg [63:0] v; integer p, len, timeout, n_drop_exp;
        reg [11:0] qid; bit zb, er;
        repeat (10) @(posedge dma_clk);
        rstn = 1;
        repeat (30) @(posedge dma_clk);

        axil_rd(LB + 16'h000, r); if (r != 32'h514E_4C42) fail($sformatf("ID %h", r));
        axil_rd(LB + 16'h030, r); if (r != 1) fail($sformatf("PKT_ID after reset %0d", r));
        axil_rd(LB + 16'h02C, r); if (r != 1) fail($sformatf("CMPT_SIZE after reset %0d", r));
        axil_wr(LB + 16'h018, SEED);
        axil_wr(LB + 16'h010, 32'h0000_0131);       // clear, cmpt_en, check, LOOPBACK
        axil_wr(LB + 16'h010, 32'h0000_0031);

        // ---- LOOPBACK ----
        for (p = 0; p < N_LB; p = p + 1) begin
            zb  = ($urandom % 100) < 4;
            er  = !zb && ($urandom % 100) < 3;
            len = ($urandom % 4) == 0 ? 4096 : 1 + ($urandom % 4096);
            qid = $urandom % 8;
            if (zb) n_zero = n_zero + 1;
            else begin
                exp_q.push_back('{len, qid}); cmpt_exp_q.push_back('{len, qid});
                n_lb_pkts = n_lb_pkts + 1; lb_bytes = lb_bytes + len;
            end
            if (er) n_err = n_err + 1;
            send_h2c(zb ? 0 : len, qid, zb, er, 1'b0);
        end
        // one corrupted packet: looped back as received, counted by the checker
        corrupt_idx = n_lb_pkts;
        exp_q.push_back('{256, 12'd3}); cmpt_exp_q.push_back('{256, 12'd3});
        n_lb_pkts = n_lb_pkts + 1; lb_bytes = lb_bytes + 256;
        send_h2c(256, 12'd3, 1'b0, 1'b0, 1'b1);
        timeout = 0;
        while ((exp_q.size() || cmpt_exp_q.size()) && timeout < 200000) begin @(posedge dma_clk); timeout++; end
        if (exp_q.size() || cmpt_exp_q.size())
            fail($sformatf("loopback: %0d C2H and %0d CMPT still missing", exp_q.size(), cmpt_exp_q.size()));
        repeat (200) @(posedge dma_clk);
        axil_rd64(LB + 16'h048, v); if (v != N_LB + 1) fail($sformatf("H2C_PKTS %0d, expected %0d", v, N_LB + 1));
        axil_rd64(LB + 16'h050, v); if (v != lb_bytes) fail($sformatf("H2C_BYTES %0d, expected %0d", v, lb_bytes));
        axil_rd64(LB + 16'h058, v); if (v != n_err) fail($sformatf("H2C_ERR_PKTS %0d, expected %0d", v, n_err));
        axil_rd64(LB + 16'h060, v); if (v != 1) fail($sformatf("H2C_BAD_BEATS %0d, expected 1", v));
        axil_rd64(LB + 16'h0C0, v); if (v != n_zero) fail($sformatf("ZERO_PKTS %0d, expected %0d", v, n_zero));
        axil_rd64(LB + 16'h078, v); if (v != n_lb_pkts) fail($sformatf("C2H_PKTS %0d, expected %0d", v, n_lb_pkts));
        axil_rd64(LB + 16'h080, v); if (v != lb_bytes) fail($sformatf("C2H_BYTES %0d, expected %0d", v, lb_bytes));
        axil_rd64(LB + 16'h098, v); if (v != n_lb_pkts) fail($sformatf("CMPT_SENT %0d, expected %0d", v, n_lb_pkts));
        n_drop_exp = n_c2h / 7;
        axil_rd64(LB + 16'h0A8, v); if (v != n_drop_exp) fail($sformatf("STS_DROP %0d, expected %0d", v, n_drop_exp));
        axil_rd64(LB + 16'h0A0, v); if (v != n_c2h - n_drop_exp) fail($sformatf("STS_OK %0d, expected %0d", v, n_c2h - n_drop_exp));
        axil_rd(LB + 16'h030, r); if (r != exp_pkt_id) fail($sformatf("PKT_ID %0d, expected %0d", r, exp_pkt_id));
        axil_rd(LB + 16'h014, r); if (r[1:0] != 0 || !r[3]) fail($sformatf("STATUS %h", r));
        $display("LOOPBACK: %0d H2C packets (%0d zero-byte, %0d err), %0d C2H + CMPT checked, %0d bytes",
                 N_LB + 1, n_zero, n_err, cnt_cmpt, lb_bytes);

        // ---- GEN ----
        axil_wr(LB + 16'h010, 32'h0000_0122);       // clear, cmpt_en, GEN
        axil_wr(LB + 16'h020, 32'd9);
        axil_wr(LB + 16'h024, GEN_LEN);
        for (p = 0; p < N_GEN; p = p + 1) begin exp_q.push_back('{GEN_LEN, 12'd9}); cmpt_exp_q.push_back('{GEN_LEN, 12'd9}); end
        axil_wr(LB + 16'h028, N_GEN);               // start
        timeout = 0;
        while ((exp_q.size() || cmpt_exp_q.size()) && timeout < 200000) begin @(posedge dma_clk); timeout++; end
        if (exp_q.size() || cmpt_exp_q.size())
            fail($sformatf("GEN: %0d C2H and %0d CMPT still missing", exp_q.size(), cmpt_exp_q.size()));
        repeat (200) @(posedge dma_clk);
        axil_rd(LB + 16'h014, r); if (r[2]) fail("generator still busy");
        axil_rd64(LB + 16'h078, v); if (v != N_GEN) fail($sformatf("GEN C2H_PKTS %0d", v));
        $display("GEN: %0d packets of %0d B checked", N_GEN, GEN_LEN);

        // ---- PKT_ID write ----
        axil_wr(LB + 16'h030, 32'd500);
        exp_pkt_id = 16'd500;
        axil_wr(LB + 16'h024, 64);
        exp_q.push_back('{64, 12'd9}); cmpt_exp_q.push_back('{64, 12'd9});
        axil_wr(LB + 16'h028, 1);
        timeout = 0;
        while ((exp_q.size() || cmpt_exp_q.size()) && timeout < 20000) begin @(posedge dma_clk); timeout++; end
        if (exp_q.size() || cmpt_exp_q.size()) fail("PKT_ID packet missing");
        if (st_status != 0) fail($sformatf("static status %b", st_status));
        if (errors == 0) $display("PASS");
        else             $display("FAIL: %0d errors", errors);
        $finish;
    end
    initial begin #20ms; $display("FAIL: timeout"); $finish; end
endmodule
