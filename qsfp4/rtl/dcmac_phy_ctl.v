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

// Host control and status of one DCMAC's GT quad and reset sequencer
// (Versal-DCMAC dcmac_reset_ctrl), between the port tester registers (PHY_CTRL /
// PHY_STATUS of eth_test, user clock) and the DCMAC hierarchy (sys_clk, the GT free-running
// clock).
//
// ctrl (user clock, quasi-static):
//   [2:0] GT loopback, all four channels (0 normal, 1 near-end PCS,
//         2 near-end PMA, 4 far-end PMA, 6 far-end PCS)
//   [3]   RX CDR hold, all channels
//   [4]   GT RX datapath reset
//   [5]   GT RX PLL and datapath reset
//   [6]   GT TX datapath reset
//   [7]   hold the DCMAC/GT reset sequencer in reset; on release it reruns from
//         the GT reset (as after configuration)
//   [8]   100G: GT line rate 1 (25.78125G NRZ), DCMAC configuration 1
//         (100CAUI-4 on both ports), TX converters at two segments a cycle.
//         Used from the instance of DCMAC port 0 only (one rate per DCMAC); the
//         DCMAC port registers must follow (qsfp_loop.py speed).
// status (user clock):
//   [0] GT TX reset done   [1] GT RX reset done   [2] GT power good
//   [3] DCMAC clock wizard locked
//   [4] DCMAC TX core reset   [5] DCMAC RX core reset
//   [6] DCMAC RX serdes reset of PORT   [7] AXIS out of reset
//   [8] sequencer held (ctrl[7] as seen in sys_clk)
//   [9] 100G selected (ctrl[8] as seen in sys_clk)
module dcmac_phy_ctl #(
    parameter integer PORT = 0              // DCMAC port of status[6]
) (
    input  wire        sys_clk,
    input  wire        user_clk,
    input  wire        aresetn,             // hierarchy reset (asynchronous)

    input  wire [15:0] ctrl,                // user_clk
    output wire [15:0] status,              // user_clk

    // sys_clk
    output reg  [2:0]  gt_loopback,
    output reg         gt_rxcdrhold,
    output reg         gt_rst_rx_datapath,
    output reg         gt_rst_rx_pll_datapath,
    output reg         gt_rst_tx_datapath,
    output wire        fsm_resetn,          // to dcmac_reset_ctrl async_resetn
    output reg  [7:0]  gt_rate = 8'd0,           // gtwiz ch_txrate / ch_rxrate: line rate index
    output reg  [2:0]  config_switch = 3'd0,      // DCMAC dynamic configuration
    output reg         tx_half = 1'b0,           // axis_to_dcmac_seg: two segments a cycle

    input  wire        gt_rst_tx_done,
    input  wire        gt_rst_rx_done,
    input  wire        gtpowergood,
    input  wire        user_locked,
    input  wire        tx_core_reset,
    input  wire        rx_core_reset,
    input  wire [5:0]  rx_serdes_reset,
    input  wire        axis_resetn
);
    wire [8:0] ctrl_s;
    xpm_cdc_array_single #(
        .DEST_SYNC_FF(4), .INIT_SYNC_FF(0), .SIM_ASSERT_CHK(0), .SRC_INPUT_REG(1), .WIDTH(9)
    ) inst_ctrl_cdc (
        .src_clk(user_clk), .src_in(ctrl[8:0]), .dest_clk(sys_clk), .dest_out(ctrl_s)
    );

    reg hold = 1'b0;
    always @(posedge sys_clk) begin
        gt_loopback <= ctrl_s[2:0];
        gt_rxcdrhold <= ctrl_s[3];
        gt_rst_rx_datapath <= ctrl_s[4];
        gt_rst_rx_pll_datapath <= ctrl_s[5];
        gt_rst_tx_datapath <= ctrl_s[6];
        hold <= ctrl_s[7];
        gt_rate <= {7'd0, ctrl_s[8]};
        config_switch <= {2'd0, ctrl_s[8]};
        tx_half <= ctrl_s[8];
    end
    // dcmac_reset_ctrl synchronizes its reset to sys_clk itself
    assign fsm_resetn = aresetn & ~hold;

    wire [9:0] st_s;
    xpm_cdc_array_single #(
        .DEST_SYNC_FF(4), .INIT_SYNC_FF(0), .SIM_ASSERT_CHK(0), .SRC_INPUT_REG(0), .WIDTH(10)
    ) inst_status_cdc (
        .src_clk(sys_clk),
        .src_in({tx_half, hold, axis_resetn, rx_serdes_reset[PORT], rx_core_reset, tx_core_reset,
                 user_locked, gtpowergood, gt_rst_rx_done, gt_rst_tx_done}),
        .dest_clk(user_clk), .dest_out(st_s)
    );
    assign status = {6'd0, st_s};
endmodule
