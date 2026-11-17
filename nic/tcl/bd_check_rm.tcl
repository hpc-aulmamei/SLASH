# Generate and validate a service-layer RM block design only (no synthesis):
# the project setup of SLASH's service_layer_build.tcl up to sourcing the
# generated service_layer.tcl (whose end validates the block design).
#   vivado -mode batch -source bd_check_rm.tcl -tclargs <work> <ip repo> <static dcp> <base bd> <service_layer.tcl>
lassign $argv work ip_repo static_dcp base_bd gen_tcl
create_project bd_check $work -part xcv80-lsva4737-2MHP-e-S -force
set_property ip_repo_paths [list $ip_repo] [current_project]
update_ip_catalog
add_files $static_dcp
import_files $base_bd
set_property PR_FLOW 1 [current_project]
set_property DESIGN_MODE GateLvl [current_fileset]
set_property top top_wrapper [current_fileset]
set_property source_mgmt_mode All [current_project]
create_partition_def -name svc -module service_layer
create_reconfig_module -name svc_rm -partition_def [get_partition_defs svc] -define_from service_layer
set bd [file join $work bd_check.srcs sources_1 bd service_layer service_layer.bd]
open_bd_design $bd
foreach p [get_bd_intf_ports] { set_property HDL_ATTRIBUTE.LOCKED {TRUE} $p }
source $gen_tcl
puts "BD_CHECK: done"
