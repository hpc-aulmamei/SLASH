# ##################################################################################################
#  The MIT License (MIT)
#  Copyright (c) 2025-2026 Advanced Micro Devices, Inc. All rights reserved.
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
set iprepos $default_iprepos

# Optional action via -tclargs: create | build | all (default: all)
set action "all"

if {[llength $argv] >= 2} {
  set arg1 [lindex $argv 1]
  if {[llength $argv] == 2} {
    if {[lsearch -exact {create build all} $arg1] >= 0} {
      set action $arg1
    } else {
      set iprepos $arg1
    }
  } else {
    set iprepos $arg1
  }
}

if {[llength $argv] >= 3} {
  set action [lindex $argv 2]
}

set do_create 0
set do_build 0
switch -exact -- $action {
  "create" { set do_create 1 }
  "build"  { set do_build 1 }
  "all"    { set do_create 1; set do_build 1 }
  default  { error "Unknown action '$action'. Expected: create, build, or all." }
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
puts "ACTION:         $action"


set proj_exists [file normalize "${src_dir}/../build/${design_name}.xpr"]
if {![file exists $proj_exists]} {
  if {!$do_create} {
    error "Project not found at $proj_exists. Run with action 'create' first."
  }
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
  if {$do_build} {
    source [file normalize [file join $src_dir "build_project.tcl"]]
    build_project $project_name
    puts "INFO: Project build complete."
  } else {
    puts "INFO: Project creation complete (build skipped)."
  }
} else {
  puts "INFO: Project already open; not recreating."
  open_project "$src_dir/../build/slash.xpr"

  # insert if config already exists ... delete old sources then build the new ones

  if {$do_create} {
    set repos   [get_property ip_repo_paths [current_project]]
    set iprepos [concat $iprepos $repos]
    set_property ip_repo_paths $iprepos [current_project]

    update_ip_catalog
    open_bd_design "$src_dir/../build/slash.srcs/sources_1/bd/top/top.bd"
    # TODO: if either run or any of the bd exist, remove old runs, bd sources, and generated bd sources before sourcing new ones
    set impl_run [get_runs -quiet "${project_name}_impl_1"] 
    if {[llength $impl_run] > 0} {
      puts "Removing stale design for project '$project_name' ..."
      set project_build_dir [file normalize [file join $src_dir ".." "build"]]
      set bd_service_dir [file normalize [file join $project_build_dir "slash.srcs" "sources_1" "bd" "service_layer_${project_name}"]]
      set bd_slash_dir [file normalize [file join $project_build_dir "slash.srcs" "sources_1" "bd" "slash_${project_name}"]]

      remove_files [list \
        [file normalize [file join $bd_service_dir "service_layer_${project_name}.bd"]] \
        [file normalize [file join $bd_slash_dir "slash_${project_name}.bd"]] \
      ]

      file delete -force $bd_service_dir
      file delete -force [file normalize [file join $project_build_dir "slash.gen" "sources_1" "bd" "service_layer_${project_name}"]]
      file delete -force $bd_slash_dir
      file delete -force [file normalize [file join $project_build_dir "slash.gen" "sources_1" "bd" "slash_${project_name}"]]

      delete_runs "${project_name}_impl_1"
    }
    
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
    save_bd_design
    generate_target all [get_files "top.bd"]
    current_bd_design [get_bd_designs top]
  }

  if {$do_build} {
    source [file normalize [file join $src_dir "build_project.tcl"]]

    set needs_full_build 0
    set pr_flow [get_property PR_FLOW [current_project]]
    if {!$pr_flow} {
      puts "INFO: PR_FLOW disabled; enabling for PR build."
      set needs_full_build 1
    }

    if {$needs_full_build} {
      puts "INFO: Base PR configuration missing; running full build."
      build_project $project_name
    } else {
      # Only touch existing runs when base build already exists.
      set base_run [get_runs -quiet slash_base_inst_0_synth_1]
      if {[llength $base_run] > 0} {
        set_property needs_refresh true $base_run
      }
      set service_run [get_runs -quiet service_layer_inst_0_synth_1]
      if {[llength $service_run] > 0} {
        set_property needs_refresh true $service_run
      }

      set synth_run [get_runs -quiet synth_1]
      if {[llength $synth_run] > 0} {
        set_property needs_refresh false $synth_run
      }
      set impl_runs [get_runs -quiet -filter {NAME =~ "*impl_1"}]
      if {[llength $impl_runs] > 0} {
        set_property needs_refresh false $impl_runs
      }
      # Launch OOC synth for new BDs
      set slash_prev_synth_run [get_runs -quiet slash_${project_name}_inst_0_synth_1]
      if {[llength $slash_prev_synth_run] > 0} {
        reset_runs $slash_prev_synth_run
      }

      set service_layer_prev_synth_run [get_runs -quiet service_layer_${project_name}_inst_0_synth_1]
      if {[llength $service_layer_prev_synth_run] > 0} {
        reset_runs $service_layer_prev_synth_run
      }
      
      launch_runs "slash_${project_name}_inst_0_synth_1" -jobs 8
      wait_on_run "slash_${project_name}_inst_0_synth_1"
      launch_runs "service_layer_${project_name}_inst_0_synth_1" -jobs 8
      wait_on_run "service_layer_${project_name}_inst_0_synth_1"

      build_new_config $project_name
    }
  } else {
    puts "INFO: Project update complete (build skipped)."
  }
}
