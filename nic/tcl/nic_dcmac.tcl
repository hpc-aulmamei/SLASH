# DCMAC hierarchies of the NIC RM: QSFP0 on DCMAC0 port 0, QSFP2 on DCMAC1
# port 0, one 200GAUI-4 / 100CAUI-4 port per DCMAC, as the 200G RoCE design
# placed them on this boundary (qsfp-loop roce_service_layer.tcl). QSFP1 and
# QSFP3 are not used.
#
# Source after slash_setup_dcmac (Versal-DCMAC tcl/dcmac.tcl), before
# bd_dcmac_qsfp. From the hardware-tested four-port loopback (qsfp-loop
# build qsfp7, SLASH 1.0.0 m05 static):
#   datapath_rx: dcmac_rx_path  1024-bit + tuser at dclk -> FCS filter -> 1024-bit at aclk
#   datapath_tx: dcmac_tx_path  1024-bit at aclk -> padding, packet FIFO -> 512-bit at dclk
#   bd_clock_reset_ctrl: as in Versal-DCMAC, plus the MMCM lock out and the
#   reset sequencer on fsm_resetn (dcmac_phy_ctl) instead of aresetn.
# aclk here is service_clk (250 MHz): nic_core and the QDMA streams run on
# it, so the frame streams cross clocks only inside dcmac_rx_path /
# dcmac_tx_path. 1024 bits x 250 MHz is 256 Gb/s per direction.
#
# nic_connect_dcmac_extra (after bd_dcmac_qsfp) puts the GT loopback, RX CDR
# hold and GT datapath resets under host control (dcmac_phy_ctl, the port's
# PHY_CTRL / PHY_STATUS in nic_core), and with nic(dynamic) the rate:
# PHY_CTRL[8] selects 100G for that port's DCMAC. With one port per DCMAC
# QSFP0 and QSFP2 switch independently.

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

# dual_dcmac: as Versal-DCMAC's signature (always 0 here).
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
    CONFIG.CLKOUT_REQUESTED_OUT_FREQUENCY {782,390.625,100,100.000,100.000,100.000,100.000} \
    CONFIG.CLKOUT_REQUESTED_PHASE {0.000,0.000,0.000,0.000,0.000,0.000,0.000} \
    CONFIG.CLKOUT_USED {true,true,true,false,false,false,false} \
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

# After bd_dcmac_qsfp: RX reset and drop counter, MMCM lock, GT controls and
# PHY control (one port: gtwiz_versal_0, datapath_rx_<dcmac_index>).
#   qsfp: the hierarchy; dcmac_index: 0 or 1; uclk: the clock of phy_ctrl /
#   phy_status (nic_core's, service_clk)
proc nic_connect_dcmac_extra {qsfp dcmac_index uclk} {
    # no cable or no module must not hold the sequencer forever (0.5 s of sys_clk)
    set_property CONFIG.GT_WAIT_CYCLES {50000000} [get_bd_cells $qsfp/bd_clock_reset_ctrl/dcmac_reset_ctrl]

    set rx $qsfp/datapath_rx_${dcmac_index}
    # 512 beats of 128 bytes (the default): 64 KiB, sixteen 4 KiB frames
    connect_bd_net [get_bd_pins $qsfp/aresetn] [get_bd_pins $rx/aresetn]
    create_bd_pin -dir O -from 15 -to 0 $qsfp/rx_drop_count
    connect_bd_net [get_bd_pins $rx/rx_drop_count] [get_bd_pins $qsfp/rx_drop_count]

    # One reset register for the RX converter (as qsfp-loop, per converter)
    set dw $qsfp/dcmac_wrapper
    set seg_rst [get_bd_pins $dw/seg_to_axis_0/aresetn_axis_seg_in]
    disconnect_bd_net [get_bd_nets -of_objects $seg_rst] $seg_rst
    set pipe [create_bd_cell -type module -reference dcmac_reset_pipe $dw/rx_reset_pipe_0]
    connect_bd_net [get_bd_pins $dw/axis_clk] [get_bd_pins $pipe/clk]
    connect_bd_net [get_bd_pins $dw/axis_rx_resetn] [get_bd_pins $pipe/in_resetn]
    connect_bd_net [get_bd_pins $pipe/out_resetn] $seg_rst

    set crc $qsfp/bd_clock_reset_ctrl
    create_bd_pin -dir O $qsfp/user_locked
    connect_bd_net [get_bd_pins $crc/user_locked] [get_bd_pins $qsfp/user_locked]

    # GT controls: off the Versal-DCMAC constants onto pins of gt_wrapper
    set gw $qsfp/gt_wrapper
    set gtw $gw/gtwiz_versal_0
    create_bd_pin -dir I -from 2 -to 0 $gw/gt_loopback
    create_bd_pin -dir I $gw/gt_rxcdrhold
    create_bd_pin -dir I $gw/gt_rst_rx_datapath
    create_bd_pin -dir I $gw/gt_rst_rx_pll_datapath
    create_bd_pin -dir I $gw/gt_rst_tx_datapath
    create_bd_pin -dir O $gw/gtpowergood
    set lb_pins [get_bd_pins [list $gtw/QUAD0_ch0_loopback $gtw/QUAD0_ch1_loopback \
        $gtw/QUAD0_ch2_loopback $gtw/QUAD0_ch3_loopback]]
    disconnect_bd_net [get_bd_nets $gw/const_loopback_dout] $lb_pins
    connect_bd_net [get_bd_pins $gw/gt_loopback] $lb_pins
    set cdr_pins [get_bd_pins [list $gtw/INTF0_RX0_ch_rxcdrhold $gtw/INTF0_RX1_ch_rxcdrhold \
        $gtw/INTF0_RX2_ch_rxcdrhold $gtw/INTF0_RX3_ch_rxcdrhold]]
    disconnect_bd_net [get_bd_nets $gw/const_0_dout] $cdr_pins
    connect_bd_net [get_bd_pins $gw/gt_rxcdrhold] $cdr_pins
    foreach {pin gtpin} {
        gt_rst_rx_datapath     INTF0_rst_rx_datapath_in
        gt_rst_rx_pll_datapath INTF0_rst_rx_pll_and_datapath_in
        gt_rst_tx_datapath     INTF0_rst_tx_datapath_in
    } {
        disconnect_bd_net [get_bd_nets $gw/const_0_dout] [get_bd_pins $gtw/$gtpin]
        connect_bd_net [get_bd_pins $gw/$pin] [get_bd_pins $gtw/$gtpin]
    }
    connect_bd_net [get_bd_pins $gtw/gtpowergood] [get_bd_pins $gw/gtpowergood]

    # host control and status (the port's PHY_CTRL / PHY_STATUS)
    set phy [create_bd_cell -type module -reference dcmac_phy_ctl $qsfp/phy_ctl]
    set_property CONFIG.PORT 0 $phy
    create_bd_pin -dir I -from 15 -to 0 $qsfp/phy_ctrl
    create_bd_pin -dir O -from 15 -to 0 $qsfp/phy_status
    create_bd_pin -dir I -type clk $qsfp/phy_clk
    connect_bd_net $uclk [get_bd_pins $qsfp/phy_clk]
    connect_bd_net [get_bd_pins $qsfp/phy_ctrl] [get_bd_pins $phy/ctrl]
    connect_bd_net [get_bd_pins $phy/status] [get_bd_pins $qsfp/phy_status]
    connect_bd_net [get_bd_pins $crc/sys_clk] [get_bd_pins $phy/sys_clk]
    connect_bd_net [get_bd_pins $qsfp/phy_clk] [get_bd_pins $phy/user_clk]
    connect_bd_net [get_bd_pins $qsfp/aresetn] [get_bd_pins $phy/aresetn]
    connect_bd_net [get_bd_pins $phy/fsm_resetn] [get_bd_pins $crc/fsm_resetn]
    foreach {src dst} {
        gt_loopback gt_loopback  gt_rxcdrhold gt_rxcdrhold  gt_rst_rx_datapath gt_rst_rx_datapath
        gt_rst_rx_pll_datapath gt_rst_rx_pll_datapath  gt_rst_tx_datapath gt_rst_tx_datapath
    } {
        connect_bd_net [get_bd_pins $phy/$src] [get_bd_pins $gw/$dst]
    }
    connect_bd_net [get_bd_pins $gw/gt_rst_tx_done] [get_bd_pins $phy/gt_rst_tx_done]
    connect_bd_net [get_bd_pins $gw/gt_rst_rx_done] [get_bd_pins $phy/gt_rst_rx_done]
    connect_bd_net [get_bd_pins $gw/gtpowergood] [get_bd_pins $phy/gtpowergood]
    foreach pin {user_locked tx_core_reset rx_core_reset rx_serdes_reset axis_resetn} {
        connect_bd_net [get_bd_pins $crc/$pin] [get_bd_pins $phy/$pin]
    }

    global nic
    if {[info exists nic(dynamic)] && $nic(dynamic)} {
        nic_dynamic_rate $qsfp $dcmac_index
    }
}

# 200G / 100G at runtime on the one port of this DCMAC: Dynamic Configuration
# (configuration 0: 200GAUI-4 on port 0, as the static IP; configurations
# 1..4: 100CAUI-4 on port 0, never 2..4), GT line rate 1 (25.78125G NRZ), the
# TX converter at two segments a cycle, and RX segments 2-3 masked at 100G
# (rtl/dcmac/dcmac_rx_seg_mask.v: the DCMAC leaves their ena/sop/eop at the
# last 200G values). All from PHY_CTRL[8] through phy_ctl. As qsfp-loop's
# qsfp_dynamic_rate (build qsfp7, hardware-tested at 100G and against a
# ConnectX-5), for port 0 only. Every other port is Disabled in every
# configuration: their default (100GAUI-2, PAM4) would make configuration 1 a
# MIX one, for which the IP generates no GT lane mux.
proc nic_dynamic_rate {qsfp dcmac_index} {
    set dw $qsfp/dcmac_wrapper
    set gw $qsfp/gt_wrapper
    set dc [get_bd_cells $dw/dcmac_$dcmac_index]

    set keep {}
    foreach p [list_property $dc CONFIG.*] {
        if {[regexp {^CONFIG\.(MAC_PORT[0-5]_|GT_REF_CLK|DCMAC_LOCATION|IS_GT_WIZ|USE_AXIS)} $p]
                && ![string match *.VALUE_SRC $p]} {
            dict set keep $p [get_property $p $dc]
        }
    }
    set_property CONFIG.DCMAC_CONFIGURATION_TYPE {Dynamic Configuration} $dc
    foreach {p v} $keep {
        if {[get_property $p $dc] ne $v} { catch {set_property $p $v $dc} }
    }
    foreach n {1 2 3 4} {
        set_property CONFIG.OTH_PCS_PORT0_CONFIG${n}_C0 {100CAUI-4} $dc
        foreach p {1 2 3 4 5} {
            catch {set_property CONFIG.OTH_PCS_PORT${p}_CONFIG${n}_C0 Disabled $dc}
        }
    }
    foreach {p v} [list CONFIG.DCMAC_CONFIGURATION_TYPE {Dynamic Configuration} \
            CONFIG.MAC_PORT0_CONFIG_C0 200GAUI-4 CONFIG.OTH_PCS_PORT0_CONFIG1_C0 100CAUI-4 \
            CONFIG.GT_MODE_0 PAM4 CONFIG.GT_MODE_1 NRZ CONFIG.GT_MODE_2 NRZ CONFIG.GT_MODE_3 NRZ \
            CONFIG.GT_MODE_4 NRZ] {
        if {[get_property $p $dc] ne $v} {
            error "nic_dynamic_rate: $dc $p is [get_property $p $dc], not $v"
        }
    }
    foreach p {1 2 3 4 5} {
        set v [get_property -quiet CONFIG.MAC_PORT${p}_CONFIG_C0 $dc]
        # N/A: a port the 200GAUI-4 port 0 takes the lanes of
        if {$v ni {"" Disabled N/A}} {
            error "nic_dynamic_rate: $dc MAC_PORT${p}_CONFIG_C0 is $v, not Disabled or N/A"
        }
    }
    create_bd_pin -dir I -from 2 -to 0 $dw/config_switch
    connect_bd_net [get_bd_pins $dw/config_switch] [get_bd_pins $dc/config_switch]
    create_bd_pin -dir I $dw/tx_half
    connect_bd_net [get_bd_pins $dw/tx_half] [get_bd_pins $dw/axis_to_seg_0/half]

    set m [create_bd_cell -type module -reference dcmac_rx_seg_mask $dw/rx_seg_mask_0]
    connect_bd_net [get_bd_pins $dw/tx_half] [get_bd_pins $m/half]
    connect_bd_net [get_bd_pins $dw/axis_clk] [get_bd_pins $m/clk]
    foreach lane {2 3} {
        foreach {dfield cfield mfield} {tuser_ena Ena ena tuser_sop Sop sop tuser_eop Eop eop} {
            set src [get_bd_pins $dc/rx_axis_${dfield}${lane}]
            set dst [get_bd_pins $dw/seg_to_axis_0/Seg2UnSeg${cfield}${lane}_in]
            disconnect_bd_net [get_bd_nets -of_objects $dst] $dst
            connect_bd_net $src [get_bd_pins $m/${mfield}${lane}_in]
            connect_bd_net [get_bd_pins $m/${mfield}${lane}] $dst
        }
    }
    puts "nic_dynamic_rate: RX segments 2-3 masked at 100G on $dw"

    set r $::dcmac_gt_refclk_mhz
    set gtw [get_bd_cells $gw/gtwiz_versal_0]
    set_property CONFIG.INTF0_GT_SETTINGS(LR1_SETTINGS) \
        "PRESET GTM-NRZ_Ethernet_25G RXPROGDIV_FREQ_VAL 644.531 RX_REFCLK_FREQUENCY $r TXPROGDIV_FREQ_VAL 644.531 TX_REFCLK_FREQUENCY $r" $gtw
    set lr1 [get_property CONFIG.INTF0_LR1_SETTINGS $gtw]
    if {![regexp {TX_LINE_RATE 25\.78125\M} $lr1] || ![regexp {TX_PAM_SEL NRZ\M} $lr1]} {
        error "nic_dynamic_rate: $gtw LR1 is not 25.78125G NRZ"
    }
    set pins [get_bd_pins -of $gtw -filter {NAME =~ INTF0_*_ch_txrate || NAME =~ INTF0_*_ch_rxrate}]
    disconnect_bd_net [get_bd_nets $gw/const_gt_rate_dout] $pins
    create_bd_pin -dir I -from 7 -to 0 $gw/gt_rate
    connect_bd_net [get_bd_pins $gw/gt_rate] $pins

    set phy $qsfp/phy_ctl
    connect_bd_net [get_bd_pins $phy/gt_rate] [get_bd_pins $gw/gt_rate]
    connect_bd_net [get_bd_pins $phy/config_switch] [get_bd_pins $dw/config_switch]
    connect_bd_net [get_bd_pins $phy/tx_half] [get_bd_pins $dw/tx_half]
}
