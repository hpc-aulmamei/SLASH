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

# Overrides of the Versal-DCMAC hierarchies for dual-port operation, and the
# host control of both GT quads. Source after slash_setup_dcmac and
# dcmac_datapath.tcl (which gives datapath_rx / datapath_tx and the clock
# hierarchy with the 230 MHz user clock), before bd_dcmac_qsfp.
#
# One DCMAC, two 200GAUI-4 ports:
#   port 0: S_AXIS_0 / M_AXIS_0, qsfp_gt0, gtwiz_versal_0
#   port 2: S_AXIS_1 / M_AXIS_1, qsfp_gt1, gtwiz_versal_1
# One clock wizard and one reset sequencer per DCMAC. Each quad has its own
# loopback, CDR hold and datapath resets (two dcmac_phy_ctl).

# Reset sequencer: both ports released, and it does not wait forever for a quad
# with no signal (0.5 s of the 100 MHz sys_clk): one empty cage must not hold
# the other port of the DCMAC down.
proc qsfp_config_reset_ctrl {qsfp} {
    set_property -dict [list CONFIG.ACTIVE_PORTS {15} CONFIG.GT_WAIT_CYCLES {50000000}] \
        [get_bd_cells $qsfp/bd_clock_reset_ctrl/dcmac_reset_ctrl]
}

# After bd_dcmac_qsfp: RX resets, drop counters, user clock, GT controls.
proc qsfp_connect_dcmac_extra {qsfp dcmac_index} {
    qsfp_config_reset_ctrl $qsfp

    create_bd_pin -dir O -type clk $qsfp/user_clk
    create_bd_pin -dir O $qsfp/user_locked
    connect_bd_net [get_bd_pins $qsfp/bd_clock_reset_ctrl/user_clk] [get_bd_pins $qsfp/user_clk]
    connect_bd_net [get_bd_pins $qsfp/bd_clock_reset_ctrl/user_locked] [get_bd_pins $qsfp/user_locked]

    # One reset register per RX converter, 11,000 loads each: from one register
    # both converters together were a 22,000-load net (-2.2 ns in build qsfp2).
    set dw $qsfp/dcmac_wrapper
    foreach i {0 1} {
        set seg_rst [get_bd_pins $dw/seg_to_axis_$i/aresetn_axis_seg_in]
        disconnect_bd_net [get_bd_nets -of_objects $seg_rst] $seg_rst
        set pipe [create_bd_cell -type module -reference dcmac_reset_pipe $dw/rx_reset_pipe_$i]
        connect_bd_net [get_bd_pins $dw/axis_clk] [get_bd_pins $pipe/clk]
        connect_bd_net [get_bd_pins $dw/axis_rx_resetn] [get_bd_pins $pipe/in_resetn]
        connect_bd_net [get_bd_pins $pipe/out_resetn] $seg_rst
    }

    set gw $qsfp/gt_wrapper
    set crc $qsfp/bd_clock_reset_ctrl
    set rst_and [create_bd_cell -type ip -vlnv xilinx.com:ip:util_vector_logic:2.0 $qsfp/fsm_resetn_and]
    set_property -dict [list CONFIG.C_OPERATION {and} CONFIG.C_SIZE {1}] $rst_and
    connect_bd_net [get_bd_pins $qsfp/fsm_resetn_and/Res] [get_bd_pins $crc/fsm_resetn]

    foreach q {0 1} dport {0 2} rx [list datapath_rx_${dcmac_index} datapath_rx_${dcmac_index}_1] {
        # 256 beats of 128 bytes still hold three 9 KB frames, and the block
        # RAM of both ports must fit around one DCMAC
        set_property CONFIG.FILTER_ADDR_BITS 8 [get_bd_cells $qsfp/$rx/rx_path]

        # RX reset in aclk, drop counter
        connect_bd_net [get_bd_pins $qsfp/aresetn] [get_bd_pins $qsfp/$rx/aresetn]
        create_bd_pin -dir O -from 15 -to 0 $qsfp/rx_drop_count_$q
        connect_bd_net [get_bd_pins $qsfp/$rx/rx_drop_count] [get_bd_pins $qsfp/rx_drop_count_$q]

        # GT controls of this quad, off the Versal-DCMAC constants
        set gtw $gw/gtwiz_versal_$q
        create_bd_pin -dir I -from 2 -to 0 $gw/gt_loopback_$q
        create_bd_pin -dir I $gw/gt_rxcdrhold_$q
        create_bd_pin -dir I $gw/gt_rst_rx_datapath_$q
        create_bd_pin -dir I $gw/gt_rst_rx_pll_datapath_$q
        create_bd_pin -dir I $gw/gt_rst_tx_datapath_$q
        create_bd_pin -dir O $gw/gtpowergood_$q
        create_bd_pin -dir O $gw/gt_rst_tx_done_$q
        create_bd_pin -dir O $gw/gt_rst_rx_done_$q

        set lb_pins [get_bd_pins [list $gtw/QUAD0_ch0_loopback $gtw/QUAD0_ch1_loopback \
            $gtw/QUAD0_ch2_loopback $gtw/QUAD0_ch3_loopback]]
        disconnect_bd_net [get_bd_nets $gw/const_loopback_dout] $lb_pins
        connect_bd_net [get_bd_pins $gw/gt_loopback_$q] $lb_pins
        set cdr_pins [get_bd_pins [list $gtw/INTF0_RX0_ch_rxcdrhold $gtw/INTF0_RX1_ch_rxcdrhold \
            $gtw/INTF0_RX2_ch_rxcdrhold $gtw/INTF0_RX3_ch_rxcdrhold]]
        disconnect_bd_net [get_bd_nets $gw/const_0_dout] $cdr_pins
        connect_bd_net [get_bd_pins $gw/gt_rxcdrhold_$q] $cdr_pins
        foreach {pin gtpin} {
            gt_rst_rx_datapath     INTF0_rst_rx_datapath_in
            gt_rst_rx_pll_datapath INTF0_rst_rx_pll_and_datapath_in
            gt_rst_tx_datapath     INTF0_rst_tx_datapath_in
        } {
            disconnect_bd_net [get_bd_nets $gw/const_0_dout] [get_bd_pins $gtw/$gtpin]
            connect_bd_net [get_bd_pins $gw/${pin}_$q] [get_bd_pins $gtw/$gtpin]
        }
        connect_bd_net [get_bd_pins $gtw/gtpowergood] [get_bd_pins $gw/gtpowergood_$q]
        connect_bd_net [get_bd_pins $gtw/INTF0_rst_tx_done_out] [get_bd_pins $gw/gt_rst_tx_done_$q]
        connect_bd_net [get_bd_pins $gtw/INTF0_rst_rx_done_out] [get_bd_pins $gw/gt_rst_rx_done_$q]

        # host control and status of this port (PHY_CTRL / PHY_STATUS of its eth_test)
        set phy [create_bd_cell -type module -reference dcmac_phy_ctl $qsfp/phy_ctl_$q]
        set_property CONFIG.PORT $dport $phy
        create_bd_pin -dir I -from 15 -to 0 $qsfp/phy_ctrl_$q
        create_bd_pin -dir O -from 15 -to 0 $qsfp/phy_status_$q
        connect_bd_net [get_bd_pins $qsfp/phy_ctrl_$q] [get_bd_pins $phy/ctrl]
        connect_bd_net [get_bd_pins $phy/status] [get_bd_pins $qsfp/phy_status_$q]
        connect_bd_net [get_bd_pins $crc/sys_clk] [get_bd_pins $phy/sys_clk]
        connect_bd_net [get_bd_pins $gw/gtwiz_freerun_clk] [get_bd_pins $phy/gt_clk]
        connect_bd_net [get_bd_pins $crc/user_clk] [get_bd_pins $phy/user_clk]
        connect_bd_net [get_bd_pins $qsfp/aresetn] [get_bd_pins $phy/aresetn]
        connect_bd_net [get_bd_pins $phy/fsm_resetn] \
            [get_bd_pins $qsfp/fsm_resetn_and/Op[expr {$q + 1}]]
        foreach {src dst} [list gt_loopback gt_loopback_$q gt_rxcdrhold gt_rxcdrhold_$q \
                gt_rst_rx_datapath gt_rst_rx_datapath_$q \
                gt_rst_rx_pll_datapath gt_rst_rx_pll_datapath_$q \
                gt_rst_tx_datapath gt_rst_tx_datapath_$q] {
            connect_bd_net [get_bd_pins $phy/$src] [get_bd_pins $gw/$dst]
        }
        connect_bd_net [get_bd_pins $gw/gt_rst_tx_done_$q] [get_bd_pins $phy/gt_rst_tx_done]
        connect_bd_net [get_bd_pins $gw/gt_rst_rx_done_$q] [get_bd_pins $phy/gt_rst_rx_done]
        connect_bd_net [get_bd_pins $gw/gtpowergood_$q] [get_bd_pins $phy/gtpowergood]
        foreach pin {user_locked tx_core_reset rx_core_reset rx_serdes_reset axis_resetn} {
            connect_bd_net [get_bd_pins $crc/$pin] [get_bd_pins $phy/$pin]
        }
    }

    # Runtime 200G / 100G is built in unless QSFP_DYNAMIC=0 (200G only)
    if {![info exists ::env(QSFP_DYNAMIC)] || $::env(QSFP_DYNAMIC)} {
        qsfp_dynamic_rate $qsfp $dcmac_index
    }
}

# 200G / 100G at runtime, one rate per DCMAC (both cages): the DCMAC in Dynamic
# Configuration (configuration 0: 200GAUI-4 on ports 0 and 2, as the static IP;
# configuration 1: 100CAUI-4 on both), a second GT line rate (LR1, 25.78125G
# NRZ) and the TX converters at two segments a cycle, all from PHY_CTRL[8] of
# DCMAC port 0's tester. The IP generates the GT lane mapping only for
# configurations that are all PAM4 or all NRZ, so the two ports of a DCMAC
# cannot run different rates. Configurations 2..4 repeat 1 (NRZ, never used).
proc qsfp_dynamic_rate {qsfp dcmac_index} {
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
        set_property -dict [list CONFIG.OTH_PCS_PORT0_CONFIG${n}_C0 {100CAUI-4} \
            CONFIG.OTH_PCS_PORT2_CONFIG${n}_C0 {100CAUI-4}] $dc
        # the other ports' defaults (100GAUI-2, PAM4) would make it a MIX configuration
        foreach p {1 3 4 5} {
            catch {set_property CONFIG.OTH_PCS_PORT${p}_CONFIG${n}_C0 Disabled $dc}
        }
    }
    foreach {p v} [list CONFIG.DCMAC_CONFIGURATION_TYPE {Dynamic Configuration} \
            CONFIG.MAC_PORT0_CONFIG_C0 200GAUI-4 CONFIG.MAC_PORT2_CONFIG_C0 200GAUI-4 \
            CONFIG.OTH_PCS_PORT0_CONFIG1_C0 100CAUI-4 CONFIG.OTH_PCS_PORT2_CONFIG1_C0 100CAUI-4 \
            CONFIG.GT_MODE_0 PAM4 CONFIG.GT_MODE_1 NRZ CONFIG.GT_MODE_2 NRZ CONFIG.GT_MODE_3 NRZ \
            CONFIG.GT_MODE_4 NRZ] {
        if {[get_property $p $dc] ne $v} {
            error "qsfp_dynamic_rate: $dc $p is [get_property $p $dc], not $v"
        }
    }
    create_bd_pin -dir I -from 2 -to 0 $dw/config_switch
    connect_bd_net [get_bd_pins $dw/config_switch] [get_bd_pins $dc/config_switch]
    create_bd_pin -dir I $dw/tx_half
    connect_bd_net [get_bd_pins $dw/tx_half] [get_bd_pins $dw/axis_to_seg_0/half] \
        [get_bd_pins $dw/axis_to_seg_1/half]

    # RX: at 100G the DCMAC leaves ena/sop/eop of segments 2-3 of each port
    # (4 segments to a converter) at their last 200G values; mask them (see
    # rtl/dcmac_rx_seg_mask.v). Converter 0 takes DCMAC segments 0-3, 1 takes 4-7.
    foreach i {0 1} {
        set m [create_bd_cell -type module -reference dcmac_rx_seg_mask $dw/rx_seg_mask_$i]
        connect_bd_net [get_bd_pins $dw/tx_half] [get_bd_pins $m/half]
        connect_bd_net [get_bd_pins $dw/axis_clk] [get_bd_pins $m/clk]
        foreach lane {2 3} {
            set dl [expr {$lane + 4 * $i}]
            foreach {dfield cfield mfield} {tuser_ena Ena ena tuser_sop Sop sop tuser_eop Eop eop} {
                set src [get_bd_pins $dc/rx_axis_${dfield}${dl}]
                set dst [get_bd_pins $dw/seg_to_axis_$i/Seg2UnSeg${cfield}${lane}_in]
                disconnect_bd_net [get_bd_nets -of_objects $dst] $dst
                connect_bd_net $src [get_bd_pins $m/${mfield}${lane}_in]
                connect_bd_net [get_bd_pins $m/${mfield}${lane}] $dst
            }
        }
    }
    puts "qsfp_dynamic_rate: RX segments 2-3 masked at 100G on $dw"

    set r $::dcmac_gt_refclk_mhz
    set rate_pins {}
    foreach q {0 1} {
        set gtw [get_bd_cells $gw/gtwiz_versal_$q]
        set_property CONFIG.INTF0_GT_SETTINGS(LR1_SETTINGS) \
            "PRESET GTM-NRZ_Ethernet_25G RXPROGDIV_FREQ_VAL 644.531 RX_REFCLK_FREQUENCY $r TXPROGDIV_FREQ_VAL 644.531 TX_REFCLK_FREQUENCY $r" $gtw
        set lr1 [get_property CONFIG.INTF0_LR1_SETTINGS $gtw]
        if {![regexp {TX_LINE_RATE 25\.78125\M} $lr1] || ![regexp {TX_PAM_SEL NRZ\M} $lr1]} {
            error "qsfp_dynamic_rate: $gtw LR1 is not 25.78125G NRZ"
        }
        set pins [get_bd_pins -of $gtw -filter {NAME =~ INTF0_*_ch_txrate || NAME =~ INTF0_*_ch_rxrate}]
        disconnect_bd_net [get_bd_nets $gw/const_gt_rate_dout] $pins
        lappend rate_pins {*}$pins
    }
    create_bd_pin -dir I -from 7 -to 0 $gw/gt_rate
    connect_bd_net [get_bd_pins $gw/gt_rate] [get_bd_pins $rate_pins]

    set phy $qsfp/phy_ctl_0
    connect_bd_net [get_bd_pins $phy/gt_rate] [get_bd_pins $gw/gt_rate]
    connect_bd_net [get_bd_pins $phy/config_switch] [get_bd_pins $dw/config_switch]
    connect_bd_net [get_bd_pins $phy/tx_half] [get_bd_pins $dw/tx_half]
}
