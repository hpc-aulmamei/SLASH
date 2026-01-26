set src_dir [file dirname [file normalize [info script]]]
set cwd     [pwd]

if {[llength $argv] < 1} {
  puts "INFO: No project_name provided via -tclargs; defaulting to 'user'."
  set project_name "user"
} else {
  set project_name [lindex $argv 0]
}

# Optional IP repository path(s) via -tclargs; defaults to ../iprepo
set default_iprepos [file normalize [file join $src_dir ".." "iprepo"]]
if {[llength $argv] < 2} {
  set iprepos $default_iprepos
} else {
  set iprepos [lindex $argv 1]
}

# Design/BD names
set design_name "slash"
set bd_slash_name        "slash_${project_name}"
set bd_service_layer_name "service_layer_${project_name}"

# Generated BD Tcl paths from the linker
set slash_gen_tcl   [file normalize [file join $src_dir ".." ".." ".." "results" $project_name "bd" "slash_${project_name}.tcl"]]
set service_gen_tcl [file normalize [file join $src_dir ".." ".." ".." "results" $project_name "bd" "service_layer_${project_name}.tcl"]]

puts "PROJECT:        $project_name"
puts "SLASH BD TCL:   $slash_gen_tcl"
puts "SERVICE BD TCL: $service_gen_tcl"
puts "IP REPOS:       $iprepos"


set proj_exists [file normalize "${src_dir}/../build/${design_name}.xpr"]
if {![file exists $proj_exists]} {
  lappend iprepos $default_iprepos
  puts "INFO: Creating new project '$design_name' in '${src_dir}/../build' ..."
  create_project $design_name "$src_dir/../build" -part xcv80-lsva4737-2MHP-e-S -force
  set_property ip_repo_paths $iprepos [current_project]
  update_ip_catalog

  # Base shell / containers
  source [file normalize [file join $src_dir "slash_base.tcl"]]
  source [file normalize [file join $src_dir "service_layer.tcl"]]
  source [file normalize [file join $src_dir "top.tcl"]]
  source [file normalize [file join $src_dir "enable_dfx_bdc.tcl"]]

  # --- source the **generated** BDs from the linker ---
  # @TODO: find a way to rebuild if they already exist?
  if {![file exists $slash_gen_tcl]} {
    error "Missing generated SLASH BD Tcl: $slash_gen_tcl"
  }
  if {![file exists $service_gen_tcl]} {
    error "Missing generated SERVICE-LAYER BD Tcl: $service_gen_tcl"
  }
  source $slash_gen_tcl
  current_bd_design [get_bd_designs top]
  source $service_gen_tcl
  current_bd_design [get_bd_designs top]
  validate_bd_design
  save_bd_design

  # Wrapper / XDC / build
  source [file normalize [file join $src_dir "make_wrapper.tcl"]]
  source [file normalize [file join $src_dir "add_constraints.tcl"]]
  source [file normalize [file join $src_dir "build_project.tcl"]]
  build_project $project_name
  puts "INFO: Project build complete."
} else {
  puts "INFO: Project already open; not recreating."
  open_project "$src_dir/../build/slash.xpr"
  set repos   [get_property ip_repo_paths [current_project]]
  set iprepos [concat $iprepos $repos]
  set_property ip_repo_paths $iprepos [current_project]

  update_ip_catalog

  # insert if config already exists ... delete old sources then build the new ones

  open_bd_design "$src_dir/../build/slash.srcs/sources_1/bd/top/top.bd"
  set slash_gen_tcl   [file normalize [file join $src_dir ".." ".." ".." "results" $project_name "bd" "slash_${project_name}.tcl"]]
  set service_gen_tcl [file normalize [file join $src_dir ".." ".." ".." "results" $project_name "bd" "service_layer_${project_name}.tcl"]]
  if {![file exists $slash_gen_tcl]} {
    error "Missing generated SLASH BD Tcl: $slash_gen_tcl"
  }
  if {![file exists $service_gen_tcl]} {
    error "Missing generated SERVICE-LAYER BD Tcl: $service_gen_tcl"
  }
  source $slash_gen_tcl
  current_bd_design [get_bd_designs top]
  source $service_gen_tcl
  current_bd_design [get_bd_designs top]
  
  generate_target all [get_files "top.bd"]
  current_bd_design [get_bd_designs top]
  save_bd_design
  #TODO: do we need this here? or somewhere else?
  set_property needs_refresh true [get_runs slash_base_inst_0_synth_1]
  set_property needs_refresh true [get_runs service_layer_inst_0_synth_1]
  set_property needs_refresh true [get_runs top]

  set_property needs_refresh false [get_runs synth_1]
  set_property needs_refresh false [get_runs -filter {NAME =~ "*impl_1"}]
  # Launch OOC synth for new BDs
  launch_runs "slash_${project_name}_inst_0_synth_1" -jobs 8
  wait_on_run "slash_${project_name}_inst_0_synth_1"
  launch_runs "service_layer_${project_name}_inst_0_synth_1" -jobs 8
  wait_on_run "service_layer_${project_name}_inst_0_synth_1"

  source [file normalize [file join $src_dir "build_project.tcl"]]
  build_new_config $project_name
}
