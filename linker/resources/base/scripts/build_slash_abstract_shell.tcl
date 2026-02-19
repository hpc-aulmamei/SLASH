set src_dir [file dirname [file normalize [info script]]]
set cwd     [pwd]

if {[llength $argv] >= 1} {
  set project_name [lindex $argv 0]
} else {
  set project_name "example"
}

set base_dir         [file normalize [file join $src_dir ".."]]
# Build directory override:
# 1) Tcl variable `slash_hw_build_dir` (if pre-set by caller)
# 2) env `SLASH_HW_BUILD_DIR` (or `slash_hw_build_dir`)
# 3) default ../build
set default_build_dir [file normalize [file join $base_dir "build"]]
if {[info exists slash_hw_build_dir] && $slash_hw_build_dir ne ""} {
  set build_dir [file normalize $slash_hw_build_dir]
} elseif {[info exists ::env(SLASH_HW_BUILD_DIR)] && $::env(SLASH_HW_BUILD_DIR) ne ""} {
  set build_dir [file normalize $::env(SLASH_HW_BUILD_DIR)]
} elseif {[info exists ::env(slash_hw_build_dir)] && $::env(slash_hw_build_dir) ne ""} {
  set build_dir [file normalize $::env(slash_hw_build_dir)]
} else {
  set build_dir $default_build_dir
}
set constraints_dir  [file normalize [file join $base_dir "constraints"]]
set results_dir      [file normalize [file join $src_dir ".." ".." ".." "results"]]
set base_results_dir [file normalize [file join $results_dir "base"]]
set project_out_dir  [file normalize [file join $results_dir $project_name]]
set project_dcp_dir  [file normalize [file join $project_out_dir "dcp" "slash"]]
set project_img_dir  [file normalize [file join $project_out_dir "images"]]

set synth_dcp_path   [file normalize [file join $build_dir "slash.runs" "slash_${project_name}_inst_0_synth_1" "slash_${project_name}_inst_0.dcp"]]
set abs_shell_dcp    [file normalize [file join $base_results_dir "abs_shell_slash.dcp"]]
set synth_dcp_file   [file tail $synth_dcp_path]
set slash_ip_xci_glob [file normalize [file join $build_dir "slash.gen" "sources_1" "bd" "top" "bd" "slash_${project_name}_inst_0" "ip" "*" "*.xci"]]

file mkdir $project_dcp_dir
file mkdir $project_img_dir

add_files $synth_dcp_path
add_files $abs_shell_dcp
set slash_ip_xcis [lsort [glob -nocomplain $slash_ip_xci_glob]]
if {[llength $slash_ip_xcis] > 0} {
  foreach ip_xci $slash_ip_xcis {
    read_ip -quiet $ip_xci
  }
  puts "INFO: Read [llength $slash_ip_xcis] IP XCI file(s) from $slash_ip_xci_glob"
} else {
  puts "INFO: No IP XCI files found at $slash_ip_xci_glob"
}

set_property SCOPED_TO_CELLS {top_i/slash} [get_files $synth_dcp_file]
link_design -reconfig_partitions {top_i/slash} -top top_wrapper -part xcv80-lsva4737-2MHP-e-S

write_checkpoint -force [file join $project_dcp_dir "top_wrapper_post_link.dcp"]

read_xdc [file join $constraints_dir "impl.pins.xdc"]
read_xdc [file join $constraints_dir "impl.xdc"]
opt_design
write_checkpoint -force [file join $project_dcp_dir "top_wrapper_post_opt.dcp"]
place_design -subdirective Floorplan.BalancedSLR.high
phys_opt_design -directive AggressiveExplore
route_design -directive AlternateCLBRouting
report_drc -file [file join $project_dcp_dir "top_wrapper_drc_routed.rpt"] -pb [file join $project_dcp_dir "top_wrapper_drc_routed.pb"] -rpx [file join $project_dcp_dir "top_wrapper_drc_routed.rpx"]
report_timing_summary -max_paths 10 -report_unconstrained -file [file join $project_dcp_dir "design_1_wrapper_timing_summary_routed.rpt"] -pb [file join $project_dcp_dir "design_1_wrapper_timing_summary_routed.pb"] -rpx [file join $project_dcp_dir "design_1_wrapper_timing_summary_routed.rpx"] -warn_on_violation
pr_verify -full_check -in_memory -additional $abs_shell_dcp -file [file join $project_dcp_dir "rp1_rm2_partial_pr_verify.log"]
write_checkpoint -force [file join $project_dcp_dir "top_wrapper_post_route.dcp"]
write_device_image -force -cell top_i/slash [file join $project_img_dir "slash.pdi"]
