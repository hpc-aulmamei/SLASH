# Write the service-layer partial PDI from the full design: the routed static
# shell (the full design the static image was written from) with the RM's
# routed cell in top_i/service_layer. On the patched SLASH 1.0.0 shell
# (shell-m05) partials written from the RM's abstract-shell run hang the PLM
# (the master stalls before its first SSIT sync); the same RM written this way
# loads.
#   vivado -mode batch -source full_partial.tcl -tclargs <full routed dcp> <RM routed cell dcp> <out pdi> [assembled dcp]
lassign $argv full rm out asm
set_param general.maxThreads 8
open_checkpoint $full
update_design -cell top_i/service_layer -black_box
read_checkpoint -cell top_i/service_layer $rm
report_route_status -file [file rootname $out]_route_status.rpt
# The GT quads must sit on the QSFP0 and QSFP2 cages' quads (SLASH's LOCs);
# an RM without DCMACs (nictest) has none.
foreach {q loc} {qsfp_0_n_1 GTM_QUAD_X1Y7 qsfp_2_n_3 GTM_QUAD_X0Y10} {
    set c [get_cells -quiet -hier -filter "REF_NAME == GTME5_QUAD && NAME =~ top_i/service_layer/$q/*"]
    if {[llength $c] == 0} { continue }
    if {[get_property LOC $c] ne $loc} {
        error "full_partial: $q GT quad on [get_property LOC $c], not $loc"
    }
}
if {$asm ne ""} {
    write_checkpoint -force $asm
}
write_device_image -cell top_i/service_layer -force $out
puts "full_partial: wrote $out"
