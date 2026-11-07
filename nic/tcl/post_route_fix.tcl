# Post-route phys_opt on a routed RM build that missed timing by a little (the
# 391 MHz DCMAC paths are tight: build 14 routed at -0.040 ns and closed at
# +0.001 ns with one AggressiveExplore pass). Writes the RM cell checkpoint
# only when timing is met; exits 2 otherwise.
#   vivado -mode batch -source post_route_fix.tcl -tclargs <routed full dcp> <out rm cell dcp> <report prefix>
lassign $argv routed out rpt
set_param general.maxThreads 8
proc wns {} { get_property SLACK [get_timing_paths -max_paths 1 -nworst 1 -setup] }
proc whs {} { get_property SLACK [get_timing_paths -max_paths 1 -nworst 1 -hold] }
proc met {} { expr {[wns] >= 0 && [whs] >= 0} }

open_checkpoint $routed
puts "post_route_fix: start WNS [wns] WHS [whs]"
foreach d {AggressiveExplore AggressiveFanoutOpt AlternateReplication} {
    if {[met]} break
    phys_opt_design -directive $d
    puts "post_route_fix: phys_opt $d WNS [wns] WHS [whs]"
}
report_timing_summary -max_paths 20 -file ${rpt}_timing_summary.rpt
report_route_status -file ${rpt}_route_status.rpt
if {![met]} {
    puts "post_route_fix: TIMING NOT MET WNS [wns] WHS [whs]"
    exit 2
}
write_checkpoint -force -cell top_i/service_layer $out
puts "post_route_fix: MET WNS [wns] WHS [whs], wrote $out"
