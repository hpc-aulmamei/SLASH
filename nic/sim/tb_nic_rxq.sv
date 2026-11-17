`timescale 1ns / 1ps

// tb_nic_rxq: nic_rxq alone under random frames (beats of 0..64 bytes, empty
// and partial beats anywhere, lengths around MAX_LEN and the 14 B minimum),
// random MAX_LEN at frame boundaries and random back-pressure. nic_rxq's own
// simulation check compares its commit logic with the plain formulas on every
// beat; this bench also checks that exactly the frames the plain formulas
// accept reach the output, with their length, padded length and data.
module tb_nic_rxq;
    logic clk = 0, rstn = 0;
    always #2 clk = ~clk;

    logic [15:0]  max_len;
    logic [511:0] s_tdata;
    logic [63:0]  s_tkeep;
    logic [6:0]   s_kb;
    logic         s_tlast, s_tvalid, s_tready;
    logic [511:0] m_tdata;
    logic         m_tlast, m_tvalid, m_tready;
    logic [15:0]  m_len, m_len_pad;
    logic         m_mcast, ev_host, ev_filtered, ev_toolong, ev_malformed;

    nic_rxq #(.BEATS(512)) dut (
        .clk(clk), .rstn(rstn), .rx_en(1'b1), .promisc(1'b1), .mac(48'h0), .max_len(max_len),
        .s_tdata(s_tdata), .s_tkeep(s_tkeep), .s_kb(s_kb), .s_tlast(s_tlast), .s_tvalid(s_tvalid), .s_tready(s_tready),
        .m_tdata(m_tdata), .m_tlast(m_tlast), .m_tvalid(m_tvalid), .m_tready(m_tready),
        .m_len(m_len), .m_len_pad(m_len_pad), .m_mcast(m_mcast),
        .ev_host(ev_host), .ev_filtered(ev_filtered), .ev_toolong(ev_toolong), .ev_malformed(ev_malformed));

    typedef struct { int len; int nb; logic [511:0] beats [$]; } exp_t;
    exp_t exp_q [$];
    int n_frames = 0, n_ok = 0, n_long = 0, n_bad = 0, n_out = 0, errors = 0;
    int c_host = 0, c_long = 0, c_bad = 0;

    always @(posedge clk) begin
        if (ev_host) c_host++;
        if (ev_toolong) c_long++;
        if (ev_malformed) c_bad++;
    end

    // output side: random ready, compare with the expected frames
    int ob = 0;
    always @(posedge clk) begin
        if (rstn && m_tvalid && m_tready) begin
            if (exp_q.size() == 0) begin
                $display("FAIL: unexpected output beat"); errors++;
            end else begin
                if (m_tdata !== exp_q[0].beats[ob]) begin
                    $display("FAIL: frame %0d beat %0d data", n_out, ob); errors++;
                end
                if (m_len !== 16'(exp_q[0].len) || m_len_pad !== 16'(64 * exp_q[0].nb)) begin
                    $display("FAIL: frame %0d len %0d/%0d pad %0d/%0d", n_out, m_len, exp_q[0].len,
                             m_len_pad, 64 * exp_q[0].nb); errors++;
                end
                if (m_tlast !== (ob == exp_q[0].nb - 1)) begin
                    $display("FAIL: frame %0d beat %0d tlast %b", n_out, ob, m_tlast); errors++;
                end
                if (m_tlast) begin ob = 0; void'(exp_q.pop_front()); n_out++; end
                else ob++;
            end
        end
    end
    always @(posedge clk) m_tready <= ($urandom % 4) != 0;

    function automatic int rand_kb(int mode);
        int r = $urandom % 100;
        if (mode == 0) return 64;                       // well formed body
        if (r < 70) return 64;
        if (r < 80) return 0;
        return 1 + $urandom % 63;
    endfunction

    task automatic send_beat(input logic [511:0] d, input int kb, input logic last);
        s_tdata <= d;
        s_tkeep <= (kb == 64) ? '1 : ((64'd1 << kb) - 64'd1);
        s_kb    <= 7'(kb);
        s_tlast <= last;
        s_tvalid <= 1'b1;
        @(posedge clk);
        while (!s_tready) @(posedge clk);
        s_tvalid <= 1'b0;
        if (($urandom % 3) == 0) repeat ($urandom % 3) @(posedge clk);
    endtask

    initial begin
        automatic int maxes [] = '{14, 15, 63, 64, 65, 127, 128, 129, 1500, 1514, 4096, 5000};
        s_tvalid = 0; s_tlast = 0; s_tkeep = 0; s_kb = 0; s_tdata = 0; max_len = 16'd4096;
        repeat (5) @(posedge clk);
        rstn <= 1;
        repeat (5) @(posedge clk);
        for (int f = 0; f < 6000; f++) begin
            automatic int nb, len, mode, kb;
            automatic int kbs [$];
            automatic logic [511:0] beats [$];
            automatic logic [511:0] d;
            automatic logic ok_formula;
            kbs.delete(); beats.delete();
            if ((f % 50) == 0) max_len <= 16'((($urandom % 2) == 0) ? maxes[$urandom % maxes.size()] : $urandom % 5200);
            mode = $urandom % 3;                        // 0 well formed, else random beats
            case ($urandom % 4)
                0: nb = 1;
                1: nb = 1 + $urandom % 4;
                2: nb = 20 + $urandom % 6;
                default: nb = 1 + $urandom % 82;
            endcase
            len = 0;
            for (int b = 0; b < nb; b++) begin
                kb = (b == nb - 1) ? (($urandom % 8 == 0) ? (mode ? $urandom % 65 : 1 + $urandom % 64) : 1 + $urandom % 64)
                                   : rand_kb(mode);
                for (int i = 0; i < 16; i++) d[32*i +: 32] = $urandom;
                for (int i = kb; i < 64; i++) d[8*i +: 8] = 8'h00;   // nic_core zeroes bytes outside tkeep
                kbs.push_back(kb); beats.push_back(d); len += kb;
            end
            @(posedge clk);
            ok_formula = !(len > max_len) && !(len < 14) && (nb == (len + 63) / 64);
            // a frame longer than max_len: too long as soon as the length passes it
            if (ok_formula) begin
                automatic exp_t e; e.len = len; e.nb = nb; e.beats = beats; exp_q.push_back(e); n_ok++;
            end else if (len > max_len) n_long++;
            else n_bad++;
            for (int b = 0; b < nb; b++) send_beat(beats[b], kbs[b], b == nb - 1);
            n_frames++;
        end
        repeat (5000) @(posedge clk);
        if (exp_q.size() != 0) begin $display("FAIL: %0d expected frames missing", exp_q.size()); errors++; end
        if (c_host != n_ok)  begin $display("FAIL: ev_host %0d, expected %0d", c_host, n_ok); errors++; end
        if (c_long != n_long) begin $display("FAIL: ev_toolong %0d, expected %0d", c_long, n_long); errors++; end
        if (c_bad != n_bad)  begin $display("FAIL: ev_malformed %0d, expected %0d", c_bad, n_bad); errors++; end
        $display("tb_nic_rxq: %0d frames: %0d to host (%0d out), %0d too long, %0d malformed",
                 n_frames, n_ok, n_out, n_long, n_bad);
        if (errors == 0) $display("PASS: tb_nic_rxq");
        else $display("FAIL: tb_nic_rxq, %0d errors", errors);
        $finish;
    end
endmodule
