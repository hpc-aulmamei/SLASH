# ##################################################################################################
#  The MIT License (MIT)
#  Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy of this software
#  and associated documentation files (the "Software"), to deal in the Software without restriction,
#  including without limitation the rights to use, copy, modify, merge, publish, distribute,
#  sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in all copies or
#  substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
# NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
# ##################################################################################################

# Overrides of the Versal-DCMAC datapath and clock hierarchies for the port testers.
# Source after slash_setup_dcmac (which sources Versal-DCMAC tcl/dcmac.tcl) and
# before bd_dcmac_qsfp.
#
# Versal-DCMAC converts the 1024-bit RX stream of seg_to_unseg to 512 bits and
# drops tuser (bad FCS), and takes 512 bits in the user clock on TX. Here:
#   datapath_rx: dcmac_rx_path  1024-bit + tuser at dclk -> FCS filter -> 1024-bit at aclk
#   datapath_tx: dcmac_tx_path  1024-bit at aclk -> padding, packet FIFO -> 512-bit at dclk
# Both keep the pin names bd_dcmac connects (aclk, dclk, dresetn, aresetn on
# TX), and add aresetn on RX and rx_drop_count, which qsfp_connect_dcmac_extra
# (qsfp_dcmac.tcl) wires after bd_dcmac_qsfp.
#
#   bd_clock_reset_ctrl: as in Versal-DCMAC, plus a fourth MMCM output for the
#   testers (user_clk, VCO / 17 = 230 MHz with the 3910 MHz VCO the wizard
#   picks for 782 / 390.625 / 100 MHz either way, so the DCMAC clocks are the
#   Versal-DCMAC ones) and the MMCM lock. Both MMCMs of the service-layer
#   pblock are the DCMAC clock wizards, and a DPLL there can reach neither
#   service_clk nor a BUFG on dedicated routing. The reset sequencer takes its
#   reset from fsm_resetn (dcmac_phy_ctl) instead of aresetn; its ACTIVE_PORTS
#   and GT_WAIT_CYCLES are set by qsfp_config_reset_ctrl.

proc create_hier_cell_datapath_rx { parentCell nameHier {dcmac_index 0} } {
    set oldCurInst [current_bd_instance .]
    current_bd_instance [get_bd_cells $parentCell]
    set hier [create_bd_cell -type hier $nameHier]
    current_bd_instance $hier

    create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:axis_rtl:1.0 m_axis_rx
    create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:axis_rtl:1.0 s_axis_rx
    create_bd_pin -dir I -type clk aclk
    create_bd_pin -dir I -type rst aresetn
    create_bd_pin -dir I -type clk dclk
    create_bd_pin -dir I -type rst dresetn
    create_bd_pin -dir O -from 15 -to 0 rx_drop_count

    set rx [create_bd_cell -type module -reference dcmac_rx_path rx_path]

    connect_bd_intf_net [get_bd_intf_pins s_axis_rx] [get_bd_intf_pins rx_path/s_axis]
    connect_bd_intf_net [get_bd_intf_pins rx_path/m_axis] [get_bd_intf_pins m_axis_rx]
    connect_bd_net [get_bd_pins dclk] [get_bd_pins rx_path/dclk]
    connect_bd_net [get_bd_pins dresetn] [get_bd_pins rx_path/dresetn]
    connect_bd_net [get_bd_pins aclk] [get_bd_pins rx_path/aclk]
    connect_bd_net [get_bd_pins aresetn] [get_bd_pins rx_path/aresetn]
    connect_bd_net [get_bd_pins rx_path/rx_drop_count] [get_bd_pins rx_drop_count]

    current_bd_instance $oldCurInst
}

proc create_hier_cell_datapath_tx { parentCell nameHier {dcmac_index 0} } {
    set oldCurInst [current_bd_instance .]
    current_bd_instance [get_bd_cells $parentCell]
    set hier [create_bd_cell -type hier $nameHier]
    current_bd_instance $hier

    create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:axis_rtl:1.0 s_axis_tx
    create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:axis_rtl:1.0 m_axis_tx
    create_bd_pin -dir I -type clk aclk
    create_bd_pin -dir I -type rst aresetn
    create_bd_pin -dir I -type clk dclk
    create_bd_pin -dir I -type rst dresetn

    set tx [create_bd_cell -type module -reference dcmac_tx_path tx_path]

    connect_bd_intf_net [get_bd_intf_pins s_axis_tx] [get_bd_intf_pins tx_path/s_axis]
    connect_bd_intf_net [get_bd_intf_pins tx_path/m_axis] [get_bd_intf_pins m_axis_tx]
    connect_bd_net [get_bd_pins aclk] [get_bd_pins tx_path/aclk]
    connect_bd_net [get_bd_pins aresetn] [get_bd_pins tx_path/aresetn]
    connect_bd_net [get_bd_pins dclk] [get_bd_pins tx_path/dclk]
    connect_bd_net [get_bd_pins dresetn] [get_bd_pins tx_path/dresetn]

    current_bd_instance $oldCurInst
}

# dual_dcmac: as Versal-DCMAC's signature; both ports are configured by
# qsfp_config_reset_ctrl after bd_dcmac_qsfp.
proc create_hier_cell_bd_clock_reset_ctrl { parentCell nameHier {dual_dcmac 0} } {
  set oldCurInst [current_bd_instance .]
  current_bd_instance [get_bd_cells $parentCell]
  set hier_obj [create_bd_cell -type hier $nameHier]
  current_bd_instance $hier_obj

  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:diff_clock_rtl:1.0 qsfp_gt_clk

  create_bd_pin -dir O -from 0 -to 0 -type clk gt_ref_clk
  create_bd_pin -dir O -type clk dcmac_core_clk
  create_bd_pin -dir O -type clk dcmac_axis_clk
  create_bd_pin -dir O -type rst gt_reset
  create_bd_pin -dir I gt_reset_done_rx
  create_bd_pin -dir I gt_reset_done_tx
  create_bd_pin -dir O -type rst tx_core_reset
  create_bd_pin -dir O -from 5 -to 0 tx_chan_flush
  create_bd_pin -dir O -from 5 -to 0 -type rst tx_serdes_reset
  create_bd_pin -dir O -from 5 -to 0 rx_chan_flush
  create_bd_pin -dir O -type rst rx_core_reset
  create_bd_pin -dir O -from 5 -to 0 -type rst rx_serdes_reset
  create_bd_pin -dir I -type rst aresetn
  create_bd_pin -dir I -type rst fsm_resetn
  create_bd_pin -dir O -type rst axis_resetn
  create_bd_pin -dir O -type clk sys_clk
  create_bd_pin -dir O -type clk user_clk
  create_bd_pin -dir O user_locked

  set diff_buff [ create_bd_cell -type ip -vlnv xilinx.com:ip:util_ds_buf:2.2 diff_buff ]
  set_property CONFIG.C_BUF_TYPE {IBUFDS_GTME5} $diff_buff

  set bufgt_dcmac [ create_bd_cell -type ip -vlnv xilinx.com:ip:bufg_gt:1.0 bufgt_dcmac ]

  set const_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 const_1 ]
  set_property CONFIG.CONST_VAL {1} $const_1

  set clk_wizard_dcmac [ create_bd_cell -type ip -vlnv xilinx.com:ip:clk_wizard:1.0 clk_wizard_dcmac ]
  set_property -dict [list \
    CONFIG.CLKOUT_DRIVES {BUFG,BUFG,BUFG,BUFG,BUFG,BUFG,BUFG} \
    CONFIG.CLKOUT_DYN_PS {None,None,None,None,None,None,None} \
    CONFIG.CLKOUT_GROUPING {Auto,Auto,Auto,Auto,Auto,Auto,Auto} \
    CONFIG.CLKOUT_MATCHED_ROUTING {false,false,false,false,false,false,false} \
    CONFIG.CLKOUT_PORT {clk_out1,clk_out2,clk_out3,clk_out4,clk_out5,clk_out6,clk_out7} \
    CONFIG.CLKOUT_REQUESTED_DUTY_CYCLE {50.000,50.000,50.000,50.000,50.000,50.000,50.000} \
    CONFIG.CLKOUT_REQUESTED_OUT_FREQUENCY {782,390.625,100,230,100.000,100.000,100.000} \
    CONFIG.CLKOUT_REQUESTED_PHASE {0.000,0.000,0.000,0.000,0.000,0.000,0.000} \
    CONFIG.CLKOUT_USED {true,true,true,true,false,false,false} \
    CONFIG.OVERRIDE_PRIMITIVE {false} \
    CONFIG.PRIM_IN_FREQ $::dcmac_gt_refclk_mhz \
    CONFIG.PRIM_SOURCE {Global_buffer} \
    CONFIG.USE_LOCKED {true} \
  ] $clk_wizard_dcmac

  set block_name dcmac_reset_ctrl_wrapper
  set block_cell_name dcmac_reset_ctrl
  if { [catch {set dcmac_reset_ctrl [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
  }

  connect_bd_intf_net -intf_net Conn1 [get_bd_intf_pins diff_buff/CLK_IN_D1] [get_bd_intf_pins qsfp_gt_clk]

  connect_bd_net -net fsm_resetn_1  [get_bd_pins fsm_resetn] [get_bd_pins dcmac_reset_ctrl/async_resetn]
  connect_bd_net -net bufgt_dcmac_usrclk  [get_bd_pins bufgt_dcmac/usrclk] [get_bd_pins clk_wizard_dcmac/clk_in1]
  connect_bd_net [get_bd_pins clk_wizard_dcmac/clk_out1] [get_bd_pins dcmac_core_clk] [get_bd_pins dcmac_reset_ctrl/dcmac_core_clk]
  connect_bd_net [get_bd_pins clk_wizard_dcmac/clk_out2] [get_bd_pins dcmac_axis_clk] [get_bd_pins dcmac_reset_ctrl/dcmac_axis_clk]
  connect_bd_net -net clk_wizard_dcmac_clk_out3  [get_bd_pins clk_wizard_dcmac/clk_out3] [get_bd_pins sys_clk] [get_bd_pins dcmac_reset_ctrl/sys_clk]
  connect_bd_net [get_bd_pins clk_wizard_dcmac/clk_out4] [get_bd_pins user_clk]
  connect_bd_net [get_bd_pins clk_wizard_dcmac/locked] [get_bd_pins user_locked]
  connect_bd_net -net const_1_dout  [get_bd_pins const_1/dout] [get_bd_pins bufgt_dcmac/gt_bufgtce]
  connect_bd_net -net dcmac_reset_ctrl_axis_resetn  [get_bd_pins dcmac_reset_ctrl/axis_resetn] [get_bd_pins axis_resetn]
  connect_bd_net -net dcmac_reset_ctrl_gt_reset  [get_bd_pins dcmac_reset_ctrl/gt_reset] [get_bd_pins gt_reset]
  connect_bd_net -net dcmac_reset_ctrl_rx_chan_flush  [get_bd_pins dcmac_reset_ctrl/rx_chan_flush] [get_bd_pins rx_chan_flush]
  connect_bd_net -net dcmac_reset_ctrl_rx_core_reset  [get_bd_pins dcmac_reset_ctrl/rx_core_reset] [get_bd_pins rx_core_reset]
  connect_bd_net -net dcmac_reset_ctrl_rx_serdes_reset  [get_bd_pins dcmac_reset_ctrl/rx_serdes_reset] [get_bd_pins rx_serdes_reset]
  connect_bd_net -net dcmac_reset_ctrl_tx_chan_flush  [get_bd_pins dcmac_reset_ctrl/tx_chan_flush] [get_bd_pins tx_chan_flush]
  connect_bd_net -net dcmac_reset_ctrl_tx_core_reset  [get_bd_pins dcmac_reset_ctrl/tx_core_reset] [get_bd_pins tx_core_reset]
  connect_bd_net -net dcmac_reset_ctrl_tx_serdes_reset  [get_bd_pins dcmac_reset_ctrl/tx_serdes_reset] [get_bd_pins tx_serdes_reset]
  connect_bd_net -net diff_buff_IBUFDS_GTME5_O  [get_bd_pins diff_buff/IBUFDS_GTME5_O] [get_bd_pins gt_ref_clk]
  connect_bd_net -net diff_buff_IBUFDS_GTME5_ODIV2  [get_bd_pins diff_buff/IBUFDS_GTME5_ODIV2] [get_bd_pins bufgt_dcmac/outclk]
  connect_bd_net -net gt_reset_done_rx_1  [get_bd_pins gt_reset_done_rx] [get_bd_pins dcmac_reset_ctrl/gt_reset_done_rx]
  connect_bd_net -net gt_reset_done_tx_1  [get_bd_pins gt_reset_done_tx] [get_bd_pins dcmac_reset_ctrl/gt_reset_done_tx]

  current_bd_instance $oldCurInst
}
