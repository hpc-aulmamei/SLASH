// Registered copy of the DCMAC stream reset for one port's RX converter, which
// has 11,000 synchronous reset loads. With one register per port the placer
// can put each reset next to its own loads.
module dcmac_reset_pipe (
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_RESET in_resetn:out_resetn" *)
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk CLK" *)
    input  wire clk,
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 in_resetn RST" *)
    input  wire in_resetn,
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 out_resetn RST" *)
    output wire out_resetn
);
    (* shreg_extract = "no" *) reg [1:0] r = 2'b00;
    always @(posedge clk) r <= {r[0], in_resetn};
    assign out_resetn = r[1];
endmodule
