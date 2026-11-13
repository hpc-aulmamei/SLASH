# (c) Copyright 2024, Advanced Micro Devices, Inc.
# 
# Permission is hereby granted, free of charge, to any person obtaining a 
# copy of this software and associated documentation files (the "Software"), 
# to deal in the Software without restriction, including without limitation 
# the rights to use, copy, modify, merge, publish, distribute, sublicense, 
# and/or sell copies of the Software, and to permit persons to whom the 
# Software is furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in 
# all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL 
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
# DEALINGS IN THE SOFTWARE.
############################################################

# Set pdi properties to accelerate device download
set_property bitstream.general.npiDmaMode Yes [current_design]
set_property bitstream.general.compress true [current_design]
# set_param place.runBufgInsertion false
# set_param place.runBufgInsertionVersal false

set_property -dict { PACKAGE_PIN AR51 } [get_ports "qsfp0_322mhz_clk_p"]
set_property -dict { PACKAGE_PIN AL17 } [get_ports "qsfp2_322mhz_clk_p"]

# ---------------------------------------------------------------------------------------------------
# Four-port shell: both DCMACs in dual 200GAUI-4 mode (scripts/service_layer.tcl), all four QSFP
# cages on the boundary.
#
#   DCMAC0 (DCMAC_X1Y1) port 0  GTM_QUAD_X1Y7   QSFP0      port 2  GTM_QUAD_X1Y8   QSFP1
#   DCMAC1 (DCMAC_X0Y2) port 0  GTM_QUAD_X0Y9   QSFP3      port 2  GTM_QUAD_X0Y10  QSFP2
#
# The quads are placed explicitly: left to the placer, a dual DCMAC1 once had QSFP2's quad on
# QSFP3's site.
foreach {q g loc} {
    qsfp_0_n_1 0 GTM_QUAD_X1Y7
    qsfp_0_n_1 1 GTM_QUAD_X1Y8
    qsfp_2_n_3 0 GTM_QUAD_X0Y9
    qsfp_2_n_3 1 GTM_QUAD_X0Y10
} {
    set c [get_cells -quiet top_i/service_layer/$q/gt_wrapper/gtwiz_versal_$g/inst/intf_quad_map_inst/quad_top_inst/gt_quad_base_0_inst/inst/quad_inst]
    if {[llength $c] != 1} {
        error "opt.post: GT quad $g of $q not found (four-port shell)"
    }
    set_property LOC $loc $c
    puts "opt.post: LOC $loc on $c"
}

# Two ports per DCMAC: eight clocks to a half column, as route_design asked for when both ports of
# DCMAC1 were placed (Route 35-4647), and each DCMAC's fabric kept in SLR1 next to the hard block.
# The pblocks are hard: create_pblock is soft in this flow, and the placer drops a soft pblock that
# overflows without a message.
set_param place.adv.maxNumClocksInHalfColumn 8
set_param place.adv.enforceMaxNumClocksInHalfColumn 1

proc qsfp4_sites_in_pblock {crs parent} {
    set ranges {}
    foreach r [get_property GRID_RANGES $parent] {
        if {[regexp {^(\w+)_X(\d+)Y(\d+):\w+_X(\d+)Y(\d+)$} $r -> t x0 y0 x1 y1]} {
            lappend ranges [list $t $x0 $y0 $x1 $y1]
        }
    }
    set out {}
    foreach s [get_sites -of_objects [get_clock_regions -quiet $crs] \
            -filter {SITE_TYPE =~ SLICE* || SITE_TYPE =~ RAMB* || SITE_TYPE =~ URAM*}] {
        if {![regexp {^(\w+)_X(\d+)Y(\d+)$} $s -> t x y]} continue
        foreach r $ranges {
            lassign $r rt x0 y0 x1 y1
            if {$rt eq $t && $x >= $x0 && $x <= $x1 && $y >= $y0 && $y <= $y1} {
                lappend out $s
                break
            }
        }
    }
    return $out
}

foreach {q dc pb crs} {
    qsfp_0_n_1 0 pblock_qsfp4_dcmac0
    {X6Y5 X6Y6 X7Y5 X7Y6 X8Y5 X8Y6 X9Y5 X9Y6 X10Y5 X10Y6 X11Y5 X11Y6}
    qsfp_2_n_3 1 pblock_qsfp4_dcmac1
    {X0Y6 X0Y7 X1Y5 X1Y6 X2Y5 X2Y6 X3Y5 X3Y6 X4Y5 X5Y5}
} {
    set h top_i/service_layer/$q
    set names {}
    foreach i {0 1} {
        lappend names $h/dcmac_wrapper/seg_to_axis_$i $h/dcmac_wrapper/axis_to_seg_$i
    }
    lappend names $h/datapath_rx_$dc $h/datapath_tx_$dc $h/datapath_rx_${dc}_1 $h/datapath_tx_${dc}_1
    set cells [get_cells -quiet $names]
    if {[llength $cells] != 8} {
        error "opt.post: $q: expected 8 DCMAC fabric cells, found: $cells"
    }
    set parent [get_pblocks pblock_service_layer]
    set p [create_pblock $pb]
    set_property PARENT $parent $p
    set_property IS_SOFT FALSE $p
    resize_pblock $p -add [qsfp4_sites_in_pblock $crs $parent]
    add_cells_to_pblock $p $cells
    puts "opt.post: $pb [get_property GRID_RANGES $p]"
}
