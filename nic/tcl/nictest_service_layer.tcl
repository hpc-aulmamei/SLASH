# Service-layer contents of the nictest RM (NIC static), sourced inside the
# generated service_layer block design script (gen_service_layer.py) after the
# NoC boundary cells exist and before the final address assignment.
# gen_service_layer.py adds the QDMA stream NoC endpoints after this script and
# connects them to qnoc_lb.
#
#   service_clk (250 MHz)  AXI-Lite from S_AXILITE_INI (PF2 BAR2), host_probe,
#                          qnoc_lb (qdma_noc_rm + loopback) and its NoC endpoints.
#                          At 390 MHz (dma_clk) qnoc_lb missed timing by up to
#                          1.6 ns; 250 MHz x 64 B is 128 Gb/s per direction.
#
# BAR2 map (PF2, offsets from 0x0203_0000_0000):
#   0x0700_0000  host_probe (64 KiB): card -> host memory through the CPM5
#                slave bridge (sw/host_probe.py)
#   0x0600_0000  qnoc_lb (64 KiB): QDMA streaming sink / loopback / generator
#                (rtl/qnoc_lb.sv, sw/qnoc_lb.py)
#   0x0000_0000  rm_mmio (64 KiB), smart static only (nic(smart) 1): 32-bit
#                accesses into the QDMA queue space 0x7_1000_0000 (doorbells,
#                CSRs) through M_QDMA_SLV_BRIDGE, and registers the R5 reaches
#                at 0x9C00_0000 (rtl/rm_mmio.sv, sw/rm_mmio.py)
#
# No DCMAC: the template's DCMAC INIS lanes, SL2NOC and the virtual NoC stay as
# the template leaves them.

proc nictest_service_layer {} {
    global nic

    set smart [expr {[info exists nic(smart)] && $nic(smart)}]
    set rtl {host_probe.sv host_probe_bd.v qnoc_lb.sv qnoc_lb_bd.v rst_pipe_bd.v}
    if {$smart} { lappend rtl rm_mmio.sv rm_mmio_bd.v }
    foreach f $rtl {
        lappend files [file join $nic(root) rtl $f]
    }
    add_files -fileset sources_1 -norecurse $files
    foreach f [get_files -of_objects [get_filesets sources_1] *.sv] {
        set_property file_type SystemVerilog $f
    }
    update_compile_order -fileset sources_1

    # The template's register slices and passthroughs take aresetn straight
    # from the BUFG_FABRIC reset (ilreduced_logic_0/Res). On the BDF static that
    # net crossed SLRs and missed by up to 124 ps, so they get a local
    # pipeline instead (rtl/rst_pipe_bd.v).
    set rnet [get_bd_nets -of_objects [get_bd_pins ilreduced_logic_0/Res]]
    set moved {}
    foreach p [get_bd_pins -of_objects $rnet] {
        set c [get_property NAME [get_bd_cells -of_objects $p]]
        if {[get_property NAME $p] eq "aresetn" &&
            ([string match axi_register_slice_* $c] || [string match axi4_full_passthrough_* $c])} {
            lappend moved $p
        }
    }
    if {[llength $moved] == 0} { error "nictest: no register slice or passthrough on [get_property NAME $rnet]" }
    puts "nictest: [llength $moved] aresetn pins moved to sl_rst_pipe"
    disconnect_bd_net $rnet $moved
    create_bd_cell -type module -reference rst_pipe_bd sl_rst_pipe
    connect_bd_net [get_bd_ports service_clk] [get_bd_pins sl_rst_pipe/clk]
    connect_bd_net [get_bd_pins ilreduced_logic_0/Res] [get_bd_pins sl_rst_pipe/rstn_in]
    connect_bd_net [get_bd_pins sl_rst_pipe/rstn_out] {*}$moved

    set xbar [create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 sl_xbar]
    set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI [expr {$smart ? 3 : 2}] CONFIG.NUM_CLKS {1}] $xbar
    connect_bd_net [get_bd_ports service_clk] [get_bd_pins sl_xbar/aclk]
    connect_bd_net [get_bd_pins ilreduced_logic_0/Res] [get_bd_pins sl_xbar/aresetn]
    connect_bd_intf_net [get_bd_intf_pins axi_noc_0/M00_AXI] [get_bd_intf_pins sl_xbar/S00_AXI]

    source [file join $nic(root) tcl host_probe.tcl]
    host_probe_add sl_xbar/M00_AXI 101

    # qnoc_lb: its AXIS ports carry the stub's names, gen_service_layer.py wires
    # them to the NoC endpoints and connects service_clk and the reset
    create_bd_cell -type module -reference qnoc_lb_bd qnoc_lb
    connect_bd_intf_net [get_bd_intf_pins sl_xbar/M01_AXI] [get_bd_intf_pins qnoc_lb/s_axil]
    assign_bd_address -offset 0x020306000000 -range 0x00010000 \
        -target_address_space [get_bd_addr_spaces S_AXILITE_INI] \
        [get_bd_addr_segs qnoc_lb/s_axil/*] -force

    if {$smart} {
        # rm_mmio: registers at offset 0x0 (the R5's window starts there), its
        # 32-bit master a third input of host_xbar (host_probe.tcl) in front of
        # noc_virt_4; the master sees only the queue space.
        create_bd_cell -type module -reference rm_mmio_bd rm_mmio
        set_property CONFIG.BUILD 1 [get_bd_cells rm_mmio]
        connect_bd_net [get_bd_ports service_clk] [get_bd_pins rm_mmio/clk]
        connect_bd_net [get_bd_pins ilreduced_logic_0/Res] [get_bd_pins rm_mmio/resetn]
        connect_bd_intf_net [get_bd_intf_pins sl_xbar/M02_AXI] [get_bd_intf_pins rm_mmio/s_axil]
        set_property CONFIG.NUM_SI {3} [get_bd_cells host_xbar]
        connect_bd_intf_net [get_bd_intf_pins rm_mmio/m_axi] [get_bd_intf_pins host_xbar/S02_AXI]
        # With two apertures M_QDMA_SLV_BRIDGE has two segments: Reg (0xE000_0000
        # 256M) and Reg_1 (0x7_1000_0000 16M, the queue space). Only rm_mmio gets
        # Reg_1: host_probe (512-bit bursts) and the user region's passthrough
        # are excluded from it before the template's final assign_bd_address.
        set qspc [get_bd_addr_segs M_QDMA_SLV_BRIDGE/Reg_1]
        if {[llength $qspc] != 1} { error "nictest: M_QDMA_SLV_BRIDGE/Reg_1 (queue space) not found" }
        assign_bd_address -offset 0x000710000000 -range 0x01000000 \
            -target_address_space [get_bd_addr_spaces rm_mmio/m_axi] $qspc -force
        foreach sp {host_probe/m_axi axi4_full_passthrough_4/m_axi} {
            exclude_bd_addr_seg -offset 0x000710000000 -range 0x01000000 \
                -target_address_space [get_bd_addr_spaces $sp] $qspc
        }
        assign_bd_address -offset 0x020300000000 -range 0x00010000 \
            -target_address_space [get_bd_addr_spaces S_AXILITE_INI] \
            [get_bd_addr_segs rm_mmio/s_axil/*] -force
        puts "nictest: rm_mmio added (smart static)"
    }
}

nictest_service_layer
