# Service-layer contents of the nictest and nic RMs (NIC static), sourced
# inside the generated service_layer block design script (gen_service_layer.py)
# after the NoC boundary cells exist and before the final address assignment.
# gen_service_layer.py adds the QDMA stream NoC endpoints after this script and
# connects them to qnoc_lb (nictest) or nic (nic).
#
# nic(kind) nic: the NIC RM. nic_bd (qdma_noc_rm + rtl/nic_core.sv) in place of
# qnoc_lb, at BAR2 0x0400_0000, and the DCMAC hierarchies of QSFP0 (qsfp_0_n_1,
# DCMAC0) and QSFP2 (qsfp_2_n_3, DCMAC1), one port each (nic_dcmac.tcl), their
# registers at 0x0200_0000 and 0x0300_0000 as in SLASH. QSFP1 and QSFP3 are not
# used. nic(dynamic) 1: runtime 200G / 100G per port.
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
#   0x0400_0000  nic (64 KiB), nic kind only: nic_core registers (sw/nic_raw.py)
#   0x0200_0000  DCMAC0 (QSFP0) registers (256 KiB), nic kind only
#   0x0300_0000  DCMAC1 (QSFP2) registers (256 KiB), nic kind only
#
# The template's DCMAC INIS lanes, SL2NOC and the virtual NoC stay as the
# template leaves them.

# The nic kind's Ethernet side: both DCMAC hierarchies, one port each, on
# service_clk (aclk), wired to nic's frame streams; their registers on sl_xbar
# M<mi> and M<mi+1>.
#   nic port 0 = QSFP0 = DCMAC0 port 0 (GTM_QUAD_X1Y7, qsfp0_322mhz)
#   nic port 1 = QSFP2 = DCMAC1 port 0 (GTM_QUAD_X0Y10, qsfp2_322mhz)
proc nic_ports {mi} {
    global nic
    source $nic(slash_wrapper)
    slash_setup_dcmac $nic(versal_dcmac)
    source [file join $nic(root) tcl nic_dcmac.tcl]

    set sclk [get_bd_ports service_clk]
    set mhz [expr {round([get_property CONFIG.FREQ_HZ $sclk] / 1.0e6)}]
    foreach {p dc idx hier} {0 0 0 qsfp_0_n_1  1 1 2 qsfp_2_n_3} {
        # bd_dcmac_qsfp: parentCell, dcmac_index, dual_dcmac, dcmac_axil, board, nclk_f (aclk FREQ_HZ in
        # MHz, must match service_clk's for validation)
        bd_dcmac_qsfp "" $dc 0 1 v80 $mhz
        nic_connect_dcmac_extra $hier $dc $sclk

        set clk [get_bd_intf_ports qsfp${idx}_322mhz]
        set_property -dict [list CONFIG.FREQ_HZ {322265625}] $clk
        connect_bd_intf_net $clk [get_bd_intf_pins $hier/qsfp_gt_clk]
        connect_bd_intf_net [get_bd_intf_ports qsfp${idx}_4x] [get_bd_intf_pins $hier/qsfp_gt0]

        # aclk = service_clk; a local reset register per hierarchy
        set rst [create_bd_cell -type module -reference rst_pipe_bd rst_dcmac_$dc]
        connect_bd_net $sclk [get_bd_pins $rst/clk] [get_bd_pins $hier/aclk]
        connect_bd_net [get_bd_pins ilreduced_logic_0/Res] [get_bd_pins $rst/rstn_in]
        connect_bd_net [get_bd_pins $rst/rstn_out] [get_bd_pins $hier/aresetn]

        connect_bd_intf_net [get_bd_intf_pins $hier/M_AXIS_0] [get_bd_intf_pins nic/s_rx$p]
        connect_bd_intf_net [get_bd_intf_pins nic/m_tx$p] [get_bd_intf_pins $hier/S_AXIS_0]
        connect_bd_net [get_bd_pins $hier/rx_drop_count] [get_bd_pins nic/rx${p}_drop_count]
        connect_bd_net [get_bd_pins nic/phy${p}_ctrl] [get_bd_pins $hier/phy_ctrl]
        connect_bd_net [get_bd_pins $hier/phy_status] [get_bd_pins nic/phy${p}_status]

        connect_bd_intf_net [get_bd_intf_pins sl_xbar/M[format %02d [expr {$mi + $p}]]_AXI] \
            [get_bd_intf_pins $hier/s_axi]
    }
    set as [get_bd_addr_spaces S_AXILITE_INI]
    assign_bd_address -offset 0x020302000000 -range 0x00040000 -target_address_space $as \
        [get_bd_addr_segs qsfp_0_n_1/dcmac_wrapper/dcmac_0/s_axi/Reg] -force
    assign_bd_address -offset 0x020303000000 -range 0x00040000 -target_address_space $as \
        [get_bd_addr_segs qsfp_2_n_3/dcmac_wrapper/dcmac_1/s_axi/Reg] -force
    # mhz is the port's FREQ_HZ (300 in the base BD); the static's clock wizard runs it at 250 MHz
    puts "nic: QSFP0 (DCMAC0) and QSFP2 (DCMAC1) on service_clk (FREQ_HZ ${mhz} MHz), dynamic rate [expr {[info exists nic(dynamic)] && $nic(dynamic)}]"
}

proc nictest_service_layer {} {
    global nic

    set smart [expr {[info exists nic(smart)] && $nic(smart)}]
    set is_nic [expr {[info exists nic(kind)] && $nic(kind) eq "nic"}]
    set rtl {host_probe.sv host_probe_bd.v rst_pipe_bd.v}
    if {$is_nic} {
        lappend rtl nic_core.sv nic_bd.v
        foreach f {axis_skid.sv rx_fcs_filter.sv dcmac_rx_path.v dcmac_tx_path.v dcmac_tx_cdc.v
                   dcmac_phy_ctl.v dcmac_reset_pipe.v dcmac_rx_seg_mask.v} {
            lappend rtl [file join dcmac $f]
        }
    } else {
        lappend rtl qnoc_lb.sv qnoc_lb_bd.v
    }
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
    # M00 host_probe, M01 qnoc_lb or nic, M02 rm_mmio (smart), then the DCMACs (nic)
    set nmi [expr {2 + $smart}]
    set mi_dcmac $nmi
    if {$is_nic} { incr nmi 2 }
    set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI $nmi CONFIG.NUM_CLKS {1}] $xbar
    connect_bd_net [get_bd_ports service_clk] [get_bd_pins sl_xbar/aclk]
    connect_bd_net [get_bd_pins ilreduced_logic_0/Res] [get_bd_pins sl_xbar/aresetn]
    connect_bd_intf_net [get_bd_intf_pins axi_noc_0/M00_AXI] [get_bd_intf_pins sl_xbar/S00_AXI]

    source [file join $nic(root) tcl host_probe.tcl]
    host_probe_add sl_xbar/M00_AXI 101

    if {$is_nic} {
        # nic: its NoC stream ports carry the stub's names, gen_service_layer.py
        # wires them to the NoC endpoints and connects service_clk and the reset
        create_bd_cell -type module -reference nic_bd nic
        set_property CONFIG.BUILD 6 [get_bd_cells nic]
        connect_bd_intf_net [get_bd_intf_pins sl_xbar/M01_AXI] [get_bd_intf_pins nic/s_axil]
        assign_bd_address -offset 0x020304000000 -range 0x00010000 \
            -target_address_space [get_bd_addr_spaces S_AXILITE_INI] \
            [get_bd_addr_segs nic/s_axil/*] -force
        nic_ports $mi_dcmac
    } else {
        # qnoc_lb: its AXIS ports carry the stub's names, gen_service_layer.py wires
        # them to the NoC endpoints and connects service_clk and the reset
        create_bd_cell -type module -reference qnoc_lb_bd qnoc_lb
        connect_bd_intf_net [get_bd_intf_pins sl_xbar/M01_AXI] [get_bd_intf_pins qnoc_lb/s_axil]
        assign_bd_address -offset 0x020306000000 -range 0x00010000 \
            -target_address_space [get_bd_addr_spaces S_AXILITE_INI] \
            [get_bd_addr_segs qnoc_lb/s_axil/*] -force
    }

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
