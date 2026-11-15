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

# Place and route the RM again from its opt_design checkpoint (which holds the
# opt.post pblocks) with other directives, for a build whose routed timing
# missed by more than post_route_fix can recover. The DCMAC1 RX FIFOs run at
# 391 MHz and sit up to three clock regions from DCMAC1; on the dev shell the
# run's Congestion_SSI_SpreadLogic_high placement routed at -0.121 ns, while
# set B closed at +0.001 ns, C at 0.000 and A at 0.000 after one phys_opt.
# Against the rebuilt shell (qsfp4-rm3) B, C and A missed by 0.09 .. 0.16 ns
# with the DCMAC1 RX converter drifting out of pblock_qsfp_dcmac1 (soft); set
# F (B placement, AggressiveExplore route, that pblock hard) closed at
# 0.000 / +0.002 ns.
# Writes the RM cell checkpoint only when timing is met; exits 2 otherwise.
#   vivado -mode batch -source replace_rm.tcl -tclargs <opt full dcp> <set A|B|C|F> <out rm cell dcp> <report prefix>
lassign $argv opt v out rpt
set_param general.maxThreads 8
proc wns {} { get_property SLACK [get_timing_paths -max_paths 1 -nworst 1 -setup] }
proc whs {} { get_property SLACK [get_timing_paths -max_paths 1 -nworst 1 -hold] }
proc met {} { expr {[wns] >= 0 && [whs] >= 0} }

open_checkpoint $opt
switch $v {
    A { place_design -directive ExtraTimingOpt
        phys_opt_design -directive AggressiveExplore
        route_design -directive AggressiveExplore }
    B { place_design -directive Explore -net_delay_weight high
        phys_opt_design -directive AggressiveExplore
        route_design -directive Explore }
    C { place_design -directive ExtraPostPlacementOpt
        phys_opt_design -directive AggressiveExplore
        phys_opt_design -directive AlternateReplication
        route_design -directive AggressiveExplore }
    F { set_property IS_SOFT FALSE [get_pblocks pblock_qsfp_dcmac1]
        place_design -directive Explore -net_delay_weight high
        phys_opt_design -directive AggressiveExplore
        route_design -directive AggressiveExplore }
    default { error "replace_rm: unknown set $v" }
}
puts "replace_rm: $v routed WNS [wns] WHS [whs]"
foreach d {AggressiveExplore AggressiveFanoutOpt AlternateReplication} {
    if {[met]} break
    phys_opt_design -directive $d
    puts "replace_rm: $v phys_opt $d WNS [wns] WHS [whs]"
}
report_timing_summary -max_paths 20 -file ${rpt}_timing_summary.rpt
report_route_status -file ${rpt}_route_status.rpt
if {![met]} {
    puts "replace_rm: $v TIMING NOT MET WNS [wns] WHS [whs]"
    exit 2
}
write_checkpoint -force -cell top_i/service_layer $out
puts "replace_rm: $v MET WNS [wns] WHS [whs], wrote $out"
