
# generate_target all [get_files "top.bd"]

# create_pr_configuration -name config_1 -partitions [list top_i/slash:slash_base_inst_0 top_i/service_layer:service_layer_inst_0]
# create_pr_configuration -name config_2 -partitions [list top_i/slash:slash_user_inst_0 top_i/service_layer:service_layer_user_inst_0]
# set_property PR_CONFIGURATION config_1 [get_runs impl_1]
# create_run child_0_impl_1 -parent_run impl_1 -flow {Vivado Advanced Implementation 2024} -pr_config config_2
# #set_property strategy {Vivado Advanced Implementation Defaults} [get_runs impl_1]
# launch_runs impl_1 child_0_impl_1 -to_step write_bitstream -jobs 8
# wait_on_run child_0_impl_1

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

  # Child run renamed to '<project_name>_impl_1'
  create_run $child_run -parent_run impl_1 \
    -flow {Vivado Advanced Implementation 2024} \
    -pr_config $cfg2_name

  # Launch and wait
  launch_runs impl_1 $child_run -to_step write_bitstream -jobs 8
  wait_on_run $child_run
  puts "INFO: Implementation complete for run '$child_run'."
}

# Example:
# build_project my_project


