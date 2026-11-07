// qdma_st_pipe: random valid/ready on h2c (CPM -> RM) and c2h (RM -> CPM),
// every beat delivered once and in order; c2h, descriptor and queue status
// delayed intact and never back-pressured (RM rdy held 0);
// user interrupts: one CPM request per RM request, valid drops after ack.
`timescale 1ns / 1ps
module tb_qdma_st_pipe;
    parameter integer STAGES = 2;
    parameter integer BEATS  = 20000;
    parameter integer IRQS   = 300;

    reg clk = 0, rstn = 0;
    always #1.154 clk = ~clk;

    // h2c
    reg  [511:0] cpm_h2c_tdata; reg cpm_h2c_tvalid = 0; wire cpm_h2c_tready;
    wire [511:0] rm_h2c_tdata;  wire rm_h2c_tvalid;     reg rm_h2c_tready = 0;
    wire [11:0] rm_h2c_qid; wire rm_h2c_tlast;
    // c2h
    reg  [511:0] rm_c2h_tdata;  reg rm_c2h_tvalid = 0;  wire rm_c2h_tready;
    wire [511:0] cpm_c2h_tdata; wire cpm_c2h_tvalid;    reg cpm_c2h_tready = 0;
    wire [15:0] cpm_c2h_ctrl_len;
    // c2h status
    reg cpm_sts_valid = 0; reg [11:0] cpm_sts_qid = 0;
    wire rm_sts_valid; wire [11:0] rm_sts_qid;
    // descriptor / queue status
    reg cpm_dsc_valid = 0; reg [11:0] cpm_dsc_qid = 0; wire cpm_dsc_rdy;
    wire rm_dsc_valid; wire [11:0] rm_dsc_qid;
    reg cpm_qs_vld = 0; reg [63:0] cpm_qs_data = 0; wire cpm_qs_rdy;
    wire rm_qs_vld; wire [63:0] rm_qs_data;
    // irq
    reg rm_irq_valid = 0; reg [10:0] rm_irq_vec = 0; wire rm_irq_ack, rm_irq_fail;
    wire cpm_irq_valid; wire [10:0] cpm_irq_vec; wire [12:0] cpm_irq_fnc;
    reg cpm_irq_ack = 0, cpm_irq_fail = 0;

    qdma_st_pipe #(.STAGES(STAGES)) dut (
        .clk(clk), .rstn(rstn),
        .cpm_h2c_tdata(cpm_h2c_tdata), .cpm_h2c_tcrc(32'h0), .cpm_h2c_tvalid(cpm_h2c_tvalid),
        .cpm_h2c_tlast(cpm_h2c_tdata[0]), .cpm_h2c_tready(cpm_h2c_tready), .cpm_h2c_qid(cpm_h2c_tdata[12:1]),
        .cpm_h2c_port_id(3'd0), .cpm_h2c_err(1'b0), .cpm_h2c_mdata(32'h0), .cpm_h2c_mty(6'd0),
        .cpm_h2c_zero_byte(1'b0),
        .rm_h2c_tdata(rm_h2c_tdata), .rm_h2c_tcrc(), .rm_h2c_tvalid(rm_h2c_tvalid), .rm_h2c_tlast(rm_h2c_tlast),
        .rm_h2c_tready(rm_h2c_tready), .rm_h2c_qid(rm_h2c_qid), .rm_h2c_port_id(), .rm_h2c_err(),
        .rm_h2c_mdata(), .rm_h2c_mty(), .rm_h2c_zero_byte(),

        .rm_c2h_tdata(rm_c2h_tdata), .rm_c2h_tcrc(32'h0), .rm_c2h_ctrl_marker(1'b0), .rm_c2h_ctrl_port_id(3'd0),
        .rm_c2h_ctrl_len(rm_c2h_tdata[15:0]), .rm_c2h_ctrl_qid(12'd0), .rm_c2h_ctrl_has_cmpt(1'b0),
        .rm_c2h_mty(6'd0), .rm_c2h_tvalid(rm_c2h_tvalid), .rm_c2h_tlast(1'b1), .rm_c2h_tready(rm_c2h_tready),
        .rm_c2h_ecc(7'd0),
        .cpm_c2h_tdata(cpm_c2h_tdata), .cpm_c2h_tcrc(), .cpm_c2h_ctrl_marker(), .cpm_c2h_ctrl_port_id(),
        .cpm_c2h_ctrl_len(cpm_c2h_ctrl_len), .cpm_c2h_ctrl_qid(), .cpm_c2h_ctrl_has_cmpt(), .cpm_c2h_mty(),
        .cpm_c2h_tvalid(cpm_c2h_tvalid), .cpm_c2h_tlast(), .cpm_c2h_tready(cpm_c2h_tready), .cpm_c2h_ecc(),

        .rm_cmpt_data(512'h0), .rm_cmpt_dpar(16'h0), .rm_cmpt_tvalid(1'b0), .rm_cmpt_tready(),
        .rm_cmpt_size(2'd0), .rm_cmpt_col_idx(3'd0), .rm_cmpt_err_idx(3'd0), .rm_cmpt_qid(13'd0),
        .rm_cmpt_cmpt_type(2'd0), .rm_cmpt_wait_pld_pkt_id(16'd0), .rm_cmpt_port_id(3'd0),
        .rm_cmpt_marker(1'b0), .rm_cmpt_user_trig(1'b0), .rm_cmpt_no_wrb_marker(1'b0),
        .cpm_cmpt_data(), .cpm_cmpt_dpar(), .cpm_cmpt_tvalid(), .cpm_cmpt_tready(1'b1), .cpm_cmpt_size(),
        .cpm_cmpt_col_idx(), .cpm_cmpt_err_idx(), .cpm_cmpt_qid(), .cpm_cmpt_cmpt_type(),
        .cpm_cmpt_wait_pld_pkt_id(), .cpm_cmpt_port_id(), .cpm_cmpt_marker(), .cpm_cmpt_user_trig(),
        .cpm_cmpt_no_wrb_marker(),

        .cpm_c2h_sts_valid(cpm_sts_valid), .cpm_c2h_sts_drop(1'b0), .cpm_c2h_sts_qid(cpm_sts_qid),
        .cpm_c2h_sts_last(1'b0), .cpm_c2h_sts_status_cmp(1'b0), .cpm_c2h_sts_error(1'b0),
        .rm_c2h_sts_valid(rm_sts_valid), .rm_c2h_sts_drop(), .rm_c2h_sts_qid(rm_sts_qid), .rm_c2h_sts_last(),
        .rm_c2h_sts_status_cmp(), .rm_c2h_sts_error(),

        .rm_irq_valid(rm_irq_valid), .rm_irq_vec(rm_irq_vec), .rm_irq_fnc(13'h5), .rm_irq_ack(rm_irq_ack),
        .rm_irq_fail(rm_irq_fail),
        .cpm_irq_valid(cpm_irq_valid), .cpm_irq_vec(cpm_irq_vec), .cpm_irq_fnc(cpm_irq_fnc),
        .cpm_irq_ack(cpm_irq_ack), .cpm_irq_fail(cpm_irq_fail),

        .cpm_dsc_sts_valid(cpm_dsc_valid), .cpm_dsc_sts_rdy(cpm_dsc_rdy), .cpm_dsc_sts_byp(1'b0), .cpm_dsc_sts_dir(1'b0),
        .cpm_dsc_sts_mm(1'b0), .cpm_dsc_sts_qid(cpm_dsc_qid), .cpm_dsc_sts_avl(16'd0), .cpm_dsc_sts_pidx(16'd0),
        .cpm_dsc_sts_port_id(3'd0), .cpm_dsc_sts_qinv(1'b0), .cpm_dsc_sts_irq_arm(1'b0),
        .cpm_dsc_sts_error(1'b0), .cpm_dsc_sts_qen(1'b0),
        .rm_dsc_sts_valid(rm_dsc_valid), .rm_dsc_sts_rdy(1'b0), .rm_dsc_sts_byp(), .rm_dsc_sts_dir(), .rm_dsc_sts_mm(),
        .rm_dsc_sts_qid(rm_dsc_qid), .rm_dsc_sts_avl(), .rm_dsc_sts_pidx(), .rm_dsc_sts_port_id(), .rm_dsc_sts_qinv(),
        .rm_dsc_sts_irq_arm(), .rm_dsc_sts_error(), .rm_dsc_sts_qen(),

        .cpm_qsts_vld(cpm_qs_vld), .cpm_qsts_rdy(cpm_qs_rdy), .cpm_qsts_op(8'd0), .cpm_qsts_data(cpm_qs_data),
        .cpm_qsts_port_id(3'd0), .cpm_qsts_qid(13'd0),
        .rm_qsts_vld(rm_qs_vld), .rm_qsts_rdy(1'b0), .rm_qsts_op(), .rm_qsts_data(rm_qs_data), .rm_qsts_port_id(), .rm_qsts_qid(),

        .rm_crdt_valid(1'b0), .rm_crdt_rdy(), .rm_crdt_dir(1'b0), .rm_crdt_crdt(16'd0), .rm_crdt_qid(12'd0),
        .rm_crdt_fence(1'b0),
        .cpm_crdt_valid(), .cpm_crdt_rdy(1'b1), .cpm_crdt_dir(), .cpm_crdt_crdt(), .cpm_crdt_qid(),
        .cpm_crdt_fence()
    );

    integer errors = 0;
    function [511:0] pat(input integer n);
        pat = {16{n[31:0] ^ 32'hA5A5_0000}};
    endfunction

    // ---- h2c ----
    integer h2c_sent = 0, h2c_rcvd = 0, h2c_stall = 0;
    always @(posedge clk) if (rstn) begin
        if (cpm_h2c_tvalid && cpm_h2c_tready) h2c_sent <= h2c_sent + 1;
        if (cpm_h2c_tvalid && !cpm_h2c_tready) h2c_stall <= h2c_stall + 1;
        if (rm_h2c_tvalid && rm_h2c_tready) begin
            if (rm_h2c_tdata !== pat(h2c_rcvd) || rm_h2c_qid !== pat(h2c_rcvd)[12:1] || rm_h2c_tlast !== pat(h2c_rcvd)[0]) begin
                errors = errors + 1;
                if (errors < 10) $display("ERROR h2c beat %0d: got %h", h2c_rcvd, rm_h2c_tdata[31:0]);
            end
            h2c_rcvd <= h2c_rcvd + 1;
        end
    end
    // CPM keeps valid/data until accepted (AXI-S rule); new beat at random
    always @(posedge clk) if (rstn) begin
        if (!cpm_h2c_tvalid || cpm_h2c_tready) begin
            if (h2c_sent + (cpm_h2c_tvalid && cpm_h2c_tready) < BEATS && ($random & 3) != 0) begin
                cpm_h2c_tvalid <= 1;
                cpm_h2c_tdata  <= pat(h2c_sent + (cpm_h2c_tvalid && cpm_h2c_tready));
            end else cpm_h2c_tvalid <= 0;
        end
        rm_h2c_tready <= ($random & 3) != 0;
    end

    // ---- c2h ----
    integer c2h_sent = 0, c2h_rcvd = 0;
    always @(posedge clk) if (rstn) begin
        if (rm_c2h_tvalid && rm_c2h_tready) c2h_sent <= c2h_sent + 1;
        if (cpm_c2h_tvalid && cpm_c2h_tready) begin
            if (cpm_c2h_tdata !== pat(c2h_rcvd + 7) || cpm_c2h_ctrl_len !== pat(c2h_rcvd + 7)[15:0]) begin
                errors = errors + 1;
                if (errors < 10) $display("ERROR c2h beat %0d", c2h_rcvd);
            end
            c2h_rcvd <= c2h_rcvd + 1;
        end
    end
    always @(posedge clk) if (rstn) begin
        if (!rm_c2h_tvalid || rm_c2h_tready) begin
            if (c2h_sent + (rm_c2h_tvalid && rm_c2h_tready) < BEATS && ($random & 1)) begin
                rm_c2h_tvalid <= 1;
                rm_c2h_tdata  <= pat(c2h_sent + (rm_c2h_tvalid && rm_c2h_tready) + 7);
            end else rm_c2h_tvalid <= 0;
        end
        cpm_c2h_tready <= ($random & 7) != 0;
    end

    // ---- c2h status: delay exactly STAGES, contents intact ----
    reg [STAGES*13-1:0] sts_exp = 0;
    always @(posedge clk) if (rstn) begin
        cpm_sts_valid <= $random;
        cpm_sts_qid   <= $random;
        sts_exp <= {sts_exp, cpm_sts_valid, cpm_sts_qid};
        if ({rm_sts_valid, rm_sts_qid} !== sts_exp[STAGES*13-1 -: 13] && (rm_sts_valid || sts_exp[STAGES*13-1])) begin
            errors = errors + 1;
            if (errors < 10) $display("ERROR c2h status mismatch");
        end
    end

    // ---- descriptor / queue status: rdy always 1, delay exactly STAGES ----
    reg [STAGES*13-1:0] dsc_exp = 0;
    reg [STAGES*65-1:0] qs_exp = 0;
    integer dsc_n = 0, qs_n = 0;
    always @(posedge clk) if (rstn) begin
        cpm_dsc_valid <= $random;
        cpm_dsc_qid   <= $random;
        cpm_qs_vld    <= $random;
        cpm_qs_data   <= {$random, $random};
        if (cpm_dsc_valid) dsc_n <= dsc_n + 1;
        if (cpm_qs_vld)    qs_n  <= qs_n + 1;
        dsc_exp <= {dsc_exp, cpm_dsc_valid, cpm_dsc_qid};
        qs_exp  <= {qs_exp, cpm_qs_vld, cpm_qs_data};
        if (!cpm_dsc_rdy || !cpm_qs_rdy) begin
            errors = errors + 1;
            if (errors < 10) $display("ERROR status rdy low towards the CPM");
        end
        if ({rm_dsc_valid, rm_dsc_qid} !== dsc_exp[STAGES*13-1 -: 13] && (rm_dsc_valid || dsc_exp[STAGES*13-1])) begin
            errors = errors + 1;
            if (errors < 10) $display("ERROR descriptor status mismatch");
        end
        if ({rm_qs_vld, rm_qs_data} !== qs_exp[STAGES*65-1 -: 65] && (rm_qs_vld || qs_exp[STAGES*65-1])) begin
            errors = errors + 1;
            if (errors < 10) $display("ERROR queue status mismatch");
        end
    end

    // ---- irq: CPM model acks/fails after a random delay ----
    integer cpm_reqs = 0, cpm_acks = 0, irq_bad = 0, cpm_wait = 0;
    reg     cpm_irq_valid_q = 0, cpm_handled_q = 0;
    always @(posedge clk) if (rstn) begin
        cpm_irq_ack  <= 0;
        cpm_irq_fail <= 0;
        cpm_irq_valid_q <= cpm_irq_valid;
        cpm_handled_q   <= cpm_irq_valid && (cpm_irq_ack || cpm_irq_fail);
        if (cpm_irq_valid && !cpm_irq_valid_q) begin
            cpm_reqs <= cpm_reqs + 1;
            if (cpm_irq_vec !== cpm_reqs[10:0] || cpm_irq_fnc !== 13'h5) begin
                irq_bad = irq_bad + 1;
                $display("ERROR irq %0d: vec %0d fnc %h", cpm_reqs, cpm_irq_vec, cpm_irq_fnc);
            end
            cpm_wait <= $random & 7;
        end
        // valid must drop the cycle after ack/fail
        if (cpm_handled_q && cpm_irq_valid) begin
            irq_bad = irq_bad + 1;
            $display("ERROR irq valid still high after ack");
        end
        if (cpm_irq_valid && !cpm_irq_ack && !cpm_irq_fail && !cpm_handled_q) begin
            if (cpm_wait == 0) begin
                if (($random & 7) == 0) cpm_irq_fail <= 1; else cpm_irq_ack <= 1;
                cpm_acks <= cpm_acks + 1;
            end else cpm_wait <= cpm_wait - 1;
        end
    end

    // ---- irq: RM model ----
    integer rm_done = 0, rm_fails = 0;
    initial begin
        wait (rstn);
        repeat (5) @(posedge clk);
        while (rm_done < IRQS) begin
            @(posedge clk);
            rm_irq_vec   <= rm_done[10:0];
            rm_irq_valid <= 1;
            @(posedge clk);
            while (!(rm_irq_ack || rm_irq_fail)) @(posedge clk);
            if (rm_irq_fail) rm_fails = rm_fails + 1;
            rm_irq_valid <= 0;
            rm_done = rm_done + 1;
            repeat ($random & 3) @(posedge clk);
        end
    end

    initial begin
        repeat (8) @(posedge clk);
        rstn <= 1;
        fork
            begin
                wait (h2c_rcvd == BEATS && c2h_rcvd == BEATS && rm_done == IRQS);
            end
            begin
                repeat (BEATS * 20) @(posedge clk);
                $display("ERROR timeout h2c %0d c2h %0d irq %0d", h2c_rcvd, c2h_rcvd, rm_done);
                errors = errors + 1;
            end
        join_any
        repeat (20) @(posedge clk);
        if (cpm_reqs != IRQS || cpm_acks != IRQS) begin
            $display("ERROR irq count: rm %0d cpm requests %0d cpm answers %0d", IRQS, cpm_reqs, cpm_acks);
            errors = errors + 1;
        end
        errors = errors + irq_bad;
        $display("STAGES=%0d h2c %0d beats (%0d cycles CPM stalled), c2h %0d beats, irq %0d (%0d fail), dsc_sts %0d, qsts %0d",
                 STAGES, h2c_rcvd, h2c_stall, c2h_rcvd, rm_done, rm_fails, dsc_n, qs_n);
        if (errors == 0) $display("TB_QDMA_ST_PIPE PASS");
        else             $display("TB_QDMA_ST_PIPE FAIL (%0d errors)", errors);
        $finish;
    end
endmodule
