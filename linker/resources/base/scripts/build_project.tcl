proc build_project {{proj_name "user"}} {
  # Derive names
  set cfg2_name   "config_$proj_name"
  set child_run   "${proj_name}_impl_1"
  set slash_user_inst       "slash_${proj_name}_inst_0"
  set service_user_inst     "service_layer_${proj_name}_inst_0"

  puts "INFO: Using proj_name='$proj_name'"
  puts "INFO: Config name='$cfg2_name', child_run='$child_run'"
  puts "INFO: PR instances: slash='$slash_user_inst', service='$service_user_inst'"

  # Ensure top BD is generated
  generate_target all [get_files "top.bd"]

  # Static/base configuration
  create_pr_configuration -name config_1 \
    -partitions [list \
      top_i/slash:slash_base_inst_0 \
      top_i/service_layer:service_layer_inst_0 \
    ]

  # Project/user configuration
  create_pr_configuration -name $cfg2_name \
    -partitions [list \
      top_i/slash:$slash_user_inst \
      top_i/service_layer:$service_user_inst \
    ]

  # Parent impl run remains 'impl_1'
  set_property PR_CONFIGURATION config_1 [get_runs impl_1]
  set_property strategy Congestion_SSI_SpreadLogic_high [get_runs impl_1]
  set_property STEPS.OPT_DESIGN.TCL.POST         [get_files *opt.post.tcl]                [get_runs impl_1]
  set_property STEPS.PLACE_DESIGN.TCL.PRE        [get_files *place.pre.tcl]               [get_runs impl_1]
  set_property STEPS.WRITE_DEVICE_IMAGE.TCL.PRE  [get_files *write_device_image.pre.tcl]  [get_runs impl_1]
  
# Child run renamed to '<project_name>_impl_1'
  create_run $child_run -parent_run impl_1 \
    -flow {Vivado Advanced Implementation 2024} \
    -pr_config $cfg2_name
  set_property strategy Congestion_SSI_SpreadLogic_high [get_runs $child_run]
  write_hw_platform -force -fixed -minimal "../results/${proj_name}/top.xsa"
  # Launch and wait
  launch_runs impl_1 $child_run -to_step write_bitstream -jobs 8
  wait_on_run $child_run
  puts "INFO: Implementation complete for run '$child_run'."
  puts "INFO: Generating resource utilization report ..."
  open_run $child_run
  report_utilization -hierarchical -hierarchical_depth 3 -hierarchical_percentages -file "../results/${proj_name}/report_utilization_${proj_name}.txt"
  
}

proc build_new_config {{proj_name "user"}} {
  # Derive names
  set cfg2_name   "config_$proj_name"
  set child_run   "${proj_name}_impl_1"
  set slash_user_inst       "slash_${proj_name}_inst_0"
  set service_user_inst     "service_layer_${proj_name}_inst_0"

  puts "INFO: Building new PR configuration for proj_name='$proj_name'"

  # Project/user configuration
  create_pr_configuration -name $cfg2_name \
    -partitions [list \
      top_i/slash:$slash_user_inst \
      top_i/service_layer:$service_user_inst \
    ]

  # Child run renamed to '<project_name>_impl_1'
  create_run $child_run -parent_run impl_1 \
    -flow {Vivado Advanced Implementation 2024} \
    -pr_config $cfg2_name
  set_property strategy Congestion_SSI_SpreadLogic_high [get_runs $child_run]

  # Launch and wait
  launch_runs $child_run -to_step write_bitstream -jobs 8
  wait_on_run $child_run
  puts "INFO: Implementation complete for run '$child_run'."
  puts "INFO: Generating resource utilization report ..."
  open_run $child_run
  report_utilization -hierarchical -hierarchical_depth 3 -hierarchical_percentages -file "../results/${proj_name}/report_utilization_${proj_name}.txt"
}

