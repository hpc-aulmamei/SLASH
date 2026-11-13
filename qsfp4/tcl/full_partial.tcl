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

# Write the partial of the four-port RM from the full design: the routed shell
# with its service layer replaced by the routed RM. A partial written from the
# abstract-shell run of service_layer_build.tcl hung the PLM on load (SLASH
# 1.0.0 shell); this one loads. Also checks where every GT quad, refclk buffer
# and DCMAC landed.
#   vivado -mode batch -source full_partial.tcl -tclargs <full routed dcp> <RM routed cell dcp> <out pdi> [assembled dcp]
lassign $argv full rm out asm
set_param general.maxThreads 8
open_checkpoint $full
update_design -cell top_i/service_layer -black_box
# the four-port shell has all 64 QSFP GT pins on its boundary
set n [llength [get_ports -quiet {qsfp?_4x_g*}]]
if {$n != 64} {
    error "qsfp_full_partial: the shell has $n QSFP GT ports, not 64: not the four-port shell"
}
read_checkpoint -cell top_i/service_layer $rm
report_route_status -file [file rootname $out]_route_status.rpt

proc qsfp_site {cell want} {
    if {[llength $cell] != 1} {
        error "qsfp_full_partial: expected one cell on $want, got: $cell"
    }
    if {[get_property LOC $cell] ne $want} {
        error "qsfp_full_partial: $cell on [get_property LOC $cell], not $want"
    }
    puts "qsfp_full_partial: $want ok"
}
set sl top_i/service_layer
foreach {q g loc} {
    qsfp_0_n_1 0 GTM_QUAD_X1Y7
    qsfp_0_n_1 1 GTM_QUAD_X1Y8
    qsfp_2_n_3 0 GTM_QUAD_X0Y9
    qsfp_2_n_3 1 GTM_QUAD_X0Y10
} {
    qsfp_site [get_cells -hier -filter "REF_NAME == GTME5_QUAD && NAME =~ $sl/$q/gt_wrapper/gtwiz_versal_$g/*"] $loc
}
foreach {q loc} {qsfp_0_n_1 GTM_REFCLK_X1Y14 qsfp_2_n_3 GTM_REFCLK_X0Y18} {
    qsfp_site [get_cells -hier -filter "REF_NAME == IBUFDS_GTME5 && NAME =~ $sl/$q/*"] $loc
}
foreach {q loc} {qsfp_0_n_1 DCMAC_X1Y1 qsfp_2_n_3 DCMAC_X0Y2} {
    qsfp_site [get_cells -hier -filter "REF_NAME == DCMAC && NAME =~ $sl/$q/*"] $loc
}
if {$asm ne ""} {
    write_checkpoint -force $asm
}
write_device_image -cell top_i/service_layer -force $out
puts "qsfp_full_partial: wrote $out"
