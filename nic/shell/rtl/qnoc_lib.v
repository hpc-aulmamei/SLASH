// Building blocks of the QDMA-over-NoC adapters (qdma_noc_static, qdma_noc_rm).
`timescale 1ns / 1ps

// Two-entry skid buffer: every output, s_ready included, comes from a register.
module qnoc_skid #(
    parameter integer W = 8
) (
    input  wire         clk,
    input  wire         rstn,
    input  wire         s_valid,
    output wire         s_ready,
    input  wire [W-1:0] s_data,
    output reg          m_valid,
    input  wire         m_ready,
    output reg  [W-1:0] m_data
);
    reg         skid_valid;
    reg [W-1:0] skid_data;
    assign s_ready = ~skid_valid;
    always @(posedge clk) begin
        if (!rstn) begin
            m_valid    <= 1'b0;
            skid_valid <= 1'b0;
        end else if (m_ready || !m_valid) begin
            if (skid_valid) begin
                m_data     <= skid_data;
                m_valid    <= 1'b1;
                skid_valid <= 1'b0;
            end else begin
                m_data  <= s_data;
                m_valid <= s_valid;
            end
        end else if (s_valid && !skid_valid) begin
            skid_data  <= s_data;
            skid_valid <= 1'b1;
        end
    end
endmodule

// First-word-fall-through FIFO, DEPTH a power of two. s_ready = not full,
// m_valid = not empty, both from registers.
module qnoc_fifo #(
    parameter integer W     = 8,
    parameter integer DEPTH = 4
) (
    input  wire         clk,
    input  wire         rstn,
    input  wire         s_valid,
    output wire         s_ready,
    input  wire [W-1:0] s_data,
    output wire         m_valid,
    input  wire         m_ready,
    output wire [W-1:0] m_data
);
    localparam integer A = (DEPTH <= 2) ? 1 : $clog2(DEPTH);
    reg [W-1:0] mem [0:DEPTH-1];
    reg [A:0]   wr, rd;
    wire full  = (wr[A] != rd[A]) && (wr[A-1:0] == rd[A-1:0]);
    wire empty = (wr == rd);
    assign s_ready = ~full;
    assign m_valid = ~empty;
    assign m_data  = mem[rd[A-1:0]];
    always @(posedge clk) begin
        if (!rstn) begin
            wr <= 0;
            rd <= 0;
        end else begin
            if (s_valid && !full) begin
                mem[wr[A-1:0]] <= s_data;
                wr <= wr + 1'b1;
            end
            if (m_ready && !empty)
                rd <= rd + 1'b1;
        end
    end
endmodule
