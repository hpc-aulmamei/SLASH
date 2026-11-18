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

# Service-layer contents of the slvmem RM: the nictest RM (host_probe,
# qnoc_lb, sl_rst_pipe; nictest_service_layer.tcl) plus the user region ->
# host address translator. Sourced by the generated service_layer block design
# script (gen_service_layer.py ... slvmem) before the final address assignment.
#
# Host path (user region -> host memory):
#
#   S_QDMA_SLV_BRIDGE (0x208_0000_0000, 32G) -> axi_noc_5 -> axi_register_slice_8 -+
#   host_probe_vm/m_axi (test source, 512-bit) -------------------------------------+-> vm_xbar
#     -> sl_vmem (in place of axi4_full_passthrough_4; 0x208_... -> 0xE800_0000 + window)
#     -> axi_register_slice_9 -> host_xbar S00 (S01: host_probe, untranslated)
#     -> noc_virt_4 -> M_QDMA_SLV_BRIDGE (0xE000_0000, 256M) -> CPM5 slave bridge
#
# BAR2 map (PF2, offsets from 0x0203_0000_0000), in addition to nictest's
# host_probe (0x0700_0000) and qnoc_lb (0x0600_0000):
#   0x0400_0000  sl_vmem (256 KiB): registers, page table at +0x1_0000 (rtl/sl_vmem.sv)
#   0x0701_0000  host_probe_vm (64 KiB): host_probe whose requests go through sl_vmem
#                (its addresses are virtual: 0x208_0000_0000 + VA)

source [file join $nic(root) tcl nictest_service_layer.tcl]

proc slvmem_service_layer {} {
    global nic

    set files [list [file join $nic(root) rtl sl_vmem.sv] [file join $nic(root) rtl sl_vmem_bd.v]]
    add_files -fileset sources_1 -norecurse $files
    set_property file_type SystemVerilog [get_files -of_objects [get_filesets sources_1] *sl_vmem.sv]
    update_compile_order -fileset sources_1

    # The passthrough must sit between the two register slices, as in the template.
    set pt [get_bd_cells axi4_full_passthrough_4]
    if {[llength $pt] != 1} { error "slvmem: no axi4_full_passthrough_4" }
    set in_net  [get_bd_intf_nets -of_objects [get_bd_intf_pins axi4_full_passthrough_4/s_axi]]
    set out_net [get_bd_intf_nets -of_objects [get_bd_intf_pins axi4_full_passthrough_4/m_axi]]
    set in_src  [get_bd_intf_pins -of_objects $in_net -filter {MODE == Master}]
    set out_dst [get_bd_intf_pins -of_objects $out_net -filter {MODE == Slave}]
    if {[get_property PATH $in_src] ne "/axi_register_slice_8/M_AXI" ||
        [get_property PATH $out_dst] ne "/axi_register_slice_9/S_AXI"} {
        error "slvmem: axi4_full_passthrough_4 is not between axi_register_slice_8 and _9 ($in_src -> $out_dst)"
    }
    # nictest moved the passthrough's aresetn to sl_rst_pipe
    set rst_net [get_bd_nets -of_objects [get_bd_pins axi4_full_passthrough_4/aresetn]]
    set rst_src [get_bd_pins -of_objects $rst_net -filter {DIR == O}]
    if {[get_property PATH $rst_src] ne "/sl_rst_pipe/rstn_out"} {
        error "slvmem: axi4_full_passthrough_4/aresetn is driven by $rst_src, not sl_rst_pipe"
    }
    delete_bd_objs $in_net $out_net $pt
    puts "slvmem: axi4_full_passthrough_4 removed"

    create_bd_cell -type module -reference sl_vmem_bd sl_vmem
    set_property CONFIG.BUILD 1 [get_bd_cells sl_vmem]
    connect_bd_net [get_bd_ports service_clk] [get_bd_pins sl_vmem/aclk]
    connect_bd_net [get_bd_pins sl_rst_pipe/rstn_out] [get_bd_pins sl_vmem/aresetn]

    # test source: a second host_probe in front of the translator
    create_bd_cell -type module -reference host_probe_bd host_probe_vm
    set_property CONFIG.BUILD 102 [get_bd_cells host_probe_vm]
    connect_bd_net [get_bd_ports service_clk] [get_bd_pins host_probe_vm/clk]
    connect_bd_net [get_bd_pins ilreduced_logic_0/Res] [get_bd_pins host_probe_vm/resetn]

    # In nictest axi_noc_5's NSU and axi_register_slice_8 take 128 bits from
    # the passthrough's AXI_DATA_WIDTH; behind a SmartConnect the slice falls
    # back to 32. Keep the NSU at 128 (as in the tested nictest RM).
    set_property CONFIG.DATA_WIDTH {128} [get_bd_cells axi_register_slice_8]

    set vx [create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 vm_xbar]
    set_property -dict [list CONFIG.NUM_SI {2} CONFIG.NUM_MI {1} CONFIG.NUM_CLKS {1}] $vx
    connect_bd_net [get_bd_ports service_clk] [get_bd_pins vm_xbar/aclk]
    connect_bd_net [get_bd_pins sl_rst_pipe/rstn_out] [get_bd_pins vm_xbar/aresetn]
    connect_bd_intf_net [get_bd_intf_pins axi_register_slice_8/M_AXI] [get_bd_intf_pins vm_xbar/S00_AXI]
    connect_bd_intf_net [get_bd_intf_pins host_probe_vm/m_axi] [get_bd_intf_pins vm_xbar/S01_AXI]
    connect_bd_intf_net [get_bd_intf_pins vm_xbar/M00_AXI] [get_bd_intf_pins sl_vmem/s_axi]
    connect_bd_intf_net [get_bd_intf_pins sl_vmem/m_axi] [get_bd_intf_pins axi_register_slice_9/S_AXI]

    # control: sl_xbar M02 -> sl_vmem, M03 -> host_probe_vm
    set_property CONFIG.NUM_MI {4} [get_bd_cells sl_xbar]
    connect_bd_intf_net [get_bd_intf_pins sl_xbar/M02_AXI] [get_bd_intf_pins sl_vmem/s_axil]
    connect_bd_intf_net [get_bd_intf_pins sl_xbar/M03_AXI] [get_bd_intf_pins host_probe_vm/s_axil]

    assign_bd_address -offset 0x020800000000 -range 0x000800000000 \
        -target_address_space [get_bd_addr_spaces S_QDMA_SLV_BRIDGE] \
        [get_bd_addr_segs sl_vmem/s_axi/*] -force
    assign_bd_address -offset 0x020800000000 -range 0x000800000000 \
        -target_address_space [get_bd_addr_spaces host_probe_vm/m_axi] \
        [get_bd_addr_segs sl_vmem/s_axi/*] -force
    assign_bd_address -offset 0xE0000000 -range 0x10000000 \
        -target_address_space [get_bd_addr_spaces sl_vmem/m_axi] \
        [get_bd_addr_segs M_QDMA_SLV_BRIDGE/Reg] -force
    assign_bd_address -offset 0x020304000000 -range 0x00040000 \
        -target_address_space [get_bd_addr_spaces S_AXILITE_INI] \
        [get_bd_addr_segs sl_vmem/s_axil/*] -force
    assign_bd_address -offset 0x020307010000 -range 0x00010000 \
        -target_address_space [get_bd_addr_spaces S_AXILITE_INI] \
        [get_bd_addr_segs host_probe_vm/s_axil/*] -force
}

slvmem_service_layer
