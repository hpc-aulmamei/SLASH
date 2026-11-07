# host_probe in a service-layer RM (build 14 on): an AXI4 master (512-bit,
# service_clk) towards the host through M_QDMA_SLV_BRIDGE -> static axi_noc_1
# -> CPM5 slave bridge. host_xbar merges it with the template's user-region
# path (S_QDMA_SLV_BRIDGE -> axi4_full_passthrough_4 -> axi_register_slice_9)
# in front of noc_virt_4, whose NMU then runs 512-bit. The static only routes
# 0xE000_0000/256M (APERTURES of M_QDMA_SLV_BRIDGE); of it the CPM5 decodes
# 0xE800_0000 + 128 MiB (region 0; on the m05 static only its first 4 KiB).
#
#   host_probe_add <control MI pin> <build>
#       the control MI is a SmartConnect master on service_clk; the probe's
#       registers are assigned at BAR2 0x0700_0000 (64 KiB)

proc host_probe_add {ctl_mi build} {
    create_bd_cell -type module -reference host_probe_bd host_probe
    set_property CONFIG.BUILD $build [get_bd_cells host_probe]
    connect_bd_net [get_bd_ports service_clk] [get_bd_pins host_probe/clk]
    connect_bd_net [get_bd_pins ilreduced_logic_0/Res] [get_bd_pins host_probe/resetn]
    connect_bd_intf_net [get_bd_intf_pins $ctl_mi] [get_bd_intf_pins host_probe/s_axil]

    set nmu_in [get_bd_intf_pins noc_virt_4/S00_AXI]
    set old [get_bd_intf_nets -of_objects $nmu_in]
    if {[llength $old] != 1} { error "host_probe_add: noc_virt_4/S00_AXI is not driven by one net" }
    set old_src [get_bd_intf_pins -of_objects $old -filter {MODE == Master}]
    delete_bd_objs $old
    set hx [create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 host_xbar]
    set_property -dict [list CONFIG.NUM_SI {2} CONFIG.NUM_MI {1} CONFIG.NUM_CLKS {1}] $hx
    connect_bd_net [get_bd_ports service_clk] [get_bd_pins host_xbar/aclk]
    connect_bd_net [get_bd_pins ilreduced_logic_0/Res] [get_bd_pins host_xbar/aresetn]
    connect_bd_intf_net $old_src [get_bd_intf_pins host_xbar/S00_AXI]
    connect_bd_intf_net [get_bd_intf_pins host_probe/m_axi] [get_bd_intf_pins host_xbar/S01_AXI]
    connect_bd_intf_net [get_bd_intf_pins host_xbar/M00_AXI] $nmu_in
    assign_bd_address -offset 0xE0000000 -range 0x10000000 \
        -target_address_space [get_bd_addr_spaces host_probe/m_axi] \
        [get_bd_addr_segs M_QDMA_SLV_BRIDGE/Reg] -force
    assign_bd_address -offset 0x020307000000 -range 0x00010000 \
        -target_address_space [get_bd_addr_spaces S_AXILITE_INI] \
        [get_bd_addr_segs host_probe/s_axil/*] -force
}
