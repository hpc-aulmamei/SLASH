set src_dir [file dirname [file normalize [info script]]]
set cwd     [pwd]

if {[llength $argv] < 1} {
  puts "INFO: No project_name provided via -tclargs; defaulting to 'user'."
  set project_name "user"
} else {
  set project_name [lindex $argv 0]
}

# Design/BD names
set design_name "slash"
set bd_slash_name        "slash_${project_name}"
set bd_service_layer_name "service_layer_${project_name}"

# Generated BD Tcl paths from the linker
# ../results/<project_name>/bd/slash_<project_name>.tcl
set slash_gen_tcl   [file normalize [file join $src_dir ".." ".." ".." "results" $project_name "bd" "slash_${project_name}.tcl"]]
set service_gen_tcl [file normalize [file join $src_dir ".." ".." ".." "results" $project_name "bd" "service_layer_${project_name}.tcl"]]

puts "PROJECT:        $project_name"
puts "SLASH BD TCL:   $slash_gen_tcl"
puts "SERVICE BD TCL: $service_gen_tcl"

set list_projects [get_projects -quiet]
if { $list_projects eq ""} {
  create_project $design_name "$src_dir/../build" -part xcv80-lsva4737-2MHP-e-S -force
  set_property ip_repo_paths "$src_dir/../iprepo" [current_project]
  update_ip_catalog

  # Base shells / containers
  source [file normalize [file join $src_dir "slash_base.tcl"]]
  source [file normalize [file join $src_dir "service_layer.tcl"]]
  source [file normalize [file join $src_dir "top.tcl"]]
  source [file normalize [file join $src_dir "enable_dfx_bdc.tcl"]]

  # --- source the **generated** BDs from the linker ---
  if {![file exists $slash_gen_tcl]} {
    error "Missing generated SLASH BD Tcl: $slash_gen_tcl"
  }
  if {![file exists $service_gen_tcl]} {
    error "Missing generated SERVICE-LAYER BD Tcl: $service_gen_tcl"
  }
  source $slash_gen_tcl
  source $service_gen_tcl

  # Wrapper / XDC / build
  source [file normalize [file join $src_dir "make_wrapper.tcl"]]
  source [file normalize [file join $src_dir "add_constraints.tcl"]]
  source [file normalize [file join $src_dir "build_project.tcl"]]
  build_project $project_name
} else {
  puts "INFO: Project already open; not recreating."
}
