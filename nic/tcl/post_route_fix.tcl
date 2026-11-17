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
set physopt 0
foreach d {AggressiveExplore AggressiveFanoutOpt AlternateReplication} {
    if {[met]} break
    phys_opt_design -directive $d
    set physopt 1
    puts "post_route_fix: phys_opt $d WNS [wns] WHS [whs]"
}
# Still failing: rip up the data nets of the failing setup paths and route them
# again timing-driven, then phys_opt again (NIC RM, 250 MHz: -0.008 ns after the
# phys_opt passes, on the DCMAC0 RX CDC FIFO and the nic_core completion path).
for {set i 1} {$i <= 3 && ![met]} {incr i} {
    set paths [get_timing_paths -setup -max_paths 500 -nworst 1 -slack_lesser_than 0.020]
    set nets [lsort -unique [get_nets -quiet -of_objects $paths -filter {TYPE == SIGNAL}]]
    puts "post_route_fix: reroute $i: [llength $paths] paths, [llength $nets] nets"
    if {[llength $nets] == 0} break
    route_design -unroute -nets $nets
    route_design -delay -nets $nets
    route_design
    puts "post_route_fix: reroute $i WNS [wns] WHS [whs]"
    if {![met]} {
        phys_opt_design -directive AggressiveExplore
        set physopt 1
        puts "post_route_fix: reroute $i phys_opt WNS [wns] WHS [whs]"
    }
}
# phys_opt on a routed design can leave implicit hold-fix (IMUX) annotations that
# only route_design programs: write_device_image then fails DRC IMUX-1 (NIC RM
# build 4, after the reroute + phys_opt step). Finish with an incremental route.
if {$physopt} {
    route_design
    puts "post_route_fix: final route WNS [wns] WHS [whs]"
}
report_drc -checks {IMUX-1} -name drc_imux
set imux [llength [get_drc_violations -quiet -name drc_imux]]
report_timing_summary -max_paths 20 -file ${rpt}_timing_summary.rpt
report_route_status -file ${rpt}_route_status.rpt
if {![met]} {
    puts "post_route_fix: TIMING NOT MET WNS [wns] WHS [whs]"
    exit 2
}
if {$imux} {
    puts "post_route_fix: $imux IMUX-1 DRC violations left"
    exit 3
}
write_checkpoint -force -cell top_i/service_layer $out
puts "post_route_fix: MET WNS [wns] WHS [whs], wrote $out"
