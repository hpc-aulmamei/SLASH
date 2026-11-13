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

# Service-layer contents of the V80 four-port Ethernet test, sourced inside the
# generated service_layer block design script (see gen_service_layer.py) after
# the NoC boundary cells exist and before the final address assignment.
#
#   service_clk (250 MHz)   AXI-Lite from S_AXILITE_INI (PF2 BAR2), NoC lane test
#   user_clk (230 MHz)      one per DCMAC: both its eth_test blocks
#
#   DCMAC port  quad            cage   SLASH    eth_test
#   DCMAC0 p0   GTM_QUAD_X1Y7   1      QSFP0    eth_test_0
#   DCMAC0 p2   GTM_QUAD_X1Y8   2      QSFP1    eth_test_1
#   DCMAC1 p0   GTM_QUAD_X0Y9   4      QSFP3    eth_test_3
#   DCMAC1 p2   GTM_QUAD_X0Y10  3      QSFP2    eth_test_2
#
# DCMAC1 follows the mapping the DCMAC IP generates for DCMAC_X0Y2.
#
# BAR2 map (PF2, offsets from 0x0203_0000_0000):
#   0x0200_0000  DCMAC0 registers (256 KiB, as in SLASH)
#   0x0300_0000  DCMAC1 registers
#   0x0400_0000  eth_test_0 .. 0x0403_0000 eth_test_3 (64 KiB each)
#   0x0600_0000  lane_test (NoC stream lanes)

proc qsfp_service_layer {} {
    global qsfp4

    source [file join $qsfp4(root) tcl qsfp_ips.tcl]
    set files [qsfp_rtl_files $qsfp4(root)]
    add_files -fileset sources_1 -norecurse $files
    foreach f [get_files -of_objects [get_filesets sources_1] *.sv] {
        set_property file_type SystemVerilog $f
    }
    set_property file_type {Verilog Header} [get_files eth_pattern.vh]
    set_property include_dirs [list [file join $qsfp4(root) rtl]] [get_filesets sources_1]
    update_compile_order -fileset sources_1

    source $qsfp4(slash_wrapper)
    slash_setup_dcmac $qsfp4(versal_dcmac)
    source [file join $qsfp4(root) tcl dcmac_datapath.tcl]
    source [file join $qsfp4(root) tcl qsfp_dcmac.tcl]

    # The base BD's qsfp1_4x and qsfp3_4x were never connected, so they have no
    # width: left as they are, the RM's top has 1-bit ports and does not link.
    # Recreated, they take [3:0] from the GT interface they connect to.
    foreach p {qsfp1_4x qsfp3_4x} {
        delete_bd_objs [get_bd_intf_ports $p]
        create_bd_intf_port -mode Master -vlnv xilinx.com:interface:gt_rtl:1.0 $p
    }

    # dc idx hier: the DCMAC's hierarchy; gt0 gt1: its quads; t0 t1: their eth_test
    foreach {dc idx hier gt0 gt1 t0 t1} {
        0 0 qsfp_0_n_1 qsfp0_4x qsfp1_4x 0 1
        1 2 qsfp_2_n_3 qsfp3_4x qsfp2_4x 3 2
    } {
        # bd_dcmac_qsfp: parentCell, dcmac_index, dual_dcmac, dcmac_axil, board, nclk_f (user clock, MHz)
        bd_dcmac_qsfp "" $dc 1 1 v80 230
        qsfp_connect_dcmac_extra $hier $dc

        set clk [get_bd_intf_ports qsfp${idx}_322mhz]
        set_property -dict [list CONFIG.FREQ_HZ {322265625}] $clk
        connect_bd_intf_net $clk [get_bd_intf_pins $hier/qsfp_gt_clk]
        connect_bd_intf_net [get_bd_intf_ports $gt0] [get_bd_intf_pins $hier/qsfp_gt0]
        connect_bd_intf_net [get_bd_intf_ports $gt1] [get_bd_intf_pins $hier/qsfp_gt1]

        set rst [create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 rst_dcmac_$dc]
        connect_bd_net [get_bd_pins ilreduced_logic_0/Res] [get_bd_pins $rst/ext_reset_in]
        connect_bd_net [get_bd_pins $hier/user_clk] [get_bd_pins $rst/slowest_sync_clk] \
            [get_bd_pins $hier/aclk]
        connect_bd_net [get_bd_pins $hier/user_locked] [get_bd_pins $rst/dcm_locked]
        connect_bd_net [get_bd_pins $rst/peripheral_aresetn] [get_bd_pins $hier/aresetn]

        foreach q {0 1} t [list $t0 $t1] {
            set node eth_test_$t
            create_bd_cell -type module -reference eth_test $node
            set_property -dict [list CONFIG.PORT $t CONFIG.BUILD 1] [get_bd_cells $node]
            connect_bd_net [get_bd_pins $hier/user_clk] [get_bd_pins $node/clk]
            connect_bd_net [get_bd_pins $rst/peripheral_aresetn] [get_bd_pins $node/resetn]
            connect_bd_intf_net [get_bd_intf_pins $hier/M_AXIS_$q] [get_bd_intf_pins $node/s_axis]
            connect_bd_intf_net [get_bd_intf_pins $node/m_axis] [get_bd_intf_pins $hier/S_AXIS_$q]
            connect_bd_net [get_bd_pins $hier/rx_drop_count_$q] [get_bd_pins $node/rx_drop_count]
            connect_bd_net [get_bd_pins $node/phy_ctrl] [get_bd_pins $hier/phy_ctrl_$q]
            connect_bd_net [get_bd_pins $hier/phy_status_$q] [get_bd_pins $node/phy_status]
        }
    }

    # ---------------------------------------------------------------- control
    set xbar [create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 sl_xbar]
    set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI {7} CONFIG.NUM_CLKS {3}] $xbar
    connect_bd_net [get_bd_ports service_clk] [get_bd_pins sl_xbar/aclk]
    connect_bd_net [get_bd_pins qsfp_0_n_1/user_clk] [get_bd_pins sl_xbar/aclk1]
    connect_bd_net [get_bd_pins qsfp_2_n_3/user_clk] [get_bd_pins sl_xbar/aclk2]
    connect_bd_net [get_bd_pins ilreduced_logic_0/Res] [get_bd_pins sl_xbar/aresetn]
    connect_bd_intf_net [get_bd_intf_pins axi_noc_0/M00_AXI] [get_bd_intf_pins sl_xbar/S00_AXI]
    connect_bd_intf_net [get_bd_intf_pins sl_xbar/M00_AXI] [get_bd_intf_pins qsfp_0_n_1/s_axi]
    connect_bd_intf_net [get_bd_intf_pins sl_xbar/M01_AXI] [get_bd_intf_pins qsfp_2_n_3/s_axi]
    foreach t {0 1 2 3} {
        connect_bd_intf_net [get_bd_intf_pins sl_xbar/M0[expr {$t + 2}]_AXI] \
            [get_bd_intf_pins eth_test_$t/s_axil]
    }

    # ---------------------------------------------------------------- NoC lane test
    # The template leaves the lanes' boundary cells
    # unconnected, with some tready pins tied high.
    create_bd_cell -type module -reference lane_test_bd lane_test
    connect_bd_net [get_bd_ports service_clk] [get_bd_pins lane_test/clk]
    connect_bd_net [get_bd_pins ilreduced_logic_0/Res] [get_bd_pins lane_test/resetn]
    for {set k 0} {$k < 8} {incr k} {
        set rdy [get_bd_pins dummy_noc_$k/M00_AXIS_tready]
        set net [get_bd_nets -quiet -of_objects $rdy]
        if {$net ne ""} {
            disconnect_bd_net $net $rdy
        }
        connect_bd_intf_net [get_bd_intf_pins dummy_noc_$k/M00_AXIS] [get_bd_intf_pins lane_test/s0${k}_axis]
        connect_bd_intf_net [get_bd_intf_pins lane_test/m0${k}_axis] [get_bd_intf_pins dummy_noc_m_$k/S00_AXIS]
    }
    set tie [get_bd_nets -quiet xlconstant_0_dout]
    if {$tie ne "" && [llength [get_bd_pins -quiet -of_objects $tie]] <= 1} {
        delete_bd_objs $tie [get_bd_cells xlconstant_0]
    }
    connect_bd_intf_net [get_bd_intf_pins sl_xbar/M06_AXI] [get_bd_intf_pins lane_test/s_axil]

    set as [get_bd_addr_spaces S_AXILITE_INI]
    assign_bd_address -offset 0x020302000000 -range 0x00040000 -target_address_space $as \
        [get_bd_addr_segs qsfp_0_n_1/dcmac_wrapper/dcmac_0/s_axi/Reg] -force
    assign_bd_address -offset 0x020303000000 -range 0x00040000 -target_address_space $as \
        [get_bd_addr_segs qsfp_2_n_3/dcmac_wrapper/dcmac_1/s_axi/Reg] -force
    foreach t {0 1 2 3} {
        assign_bd_address -offset [format 0x0203040%X0000 $t] -range 0x00010000 \
            -target_address_space $as [get_bd_addr_segs eth_test_$t/s_axil/*] -force
    }
    assign_bd_address -offset 0x020306000000 -range 0x00010000 -target_address_space $as \
        [get_bd_addr_segs lane_test/s_axil/*] -force
}

qsfp_service_layer
