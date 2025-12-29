
set src_dir        [file dirname [file normalize [info script]]]
set cwd            [pwd]
set design_name    "slash"

set list_projects [get_projects -quiet]

if { $list_projects eq ""} {
    create_project $design_name "$src_dir/../build" -part xcv80-lsva4737-2MHP-e-S -force
    set_property ip_repo_paths "$src_dir/../iprepo" [current_project]
    update_ip_catalog

    # Create slash base block design container
    source slash_base.tcl
    # Create service layer block design container
    source service_layer.tcl
    # Create top level block design
    source top.tcl
    # Enable DFX on BDCs
    source enable_dfx_bdc.tcl
    # Add slash block design
    source ../../../src/slash.tcl
    # Add service layer block design
    source ../../../src/service_layer_gen.tcl
}

