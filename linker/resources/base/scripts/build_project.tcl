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

proc build_project {{proj_name "user"}} {
<<<<<<< dev
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
  write_hw_platform -force -fixed -minimal "../results/${proj_name}/top.xsa"
=======
  puts "INFO: Using proj_name='$proj_name'"

  # Ensure top BD is generated
  generate_target all [get_files "top.bd"]
>>>>>>> dev

  # Static/base configuration
  create_pr_configuration -name config_1 \
    -partitions [list \
      top_i/slash:slash_base_inst_0 \
      top_i/service_layer:service_layer_inst_0 \
    ]

<<<<<<< dev
  # Project/user configuration
  create_pr_configuration -name $cfg2_name \
    -partitions [list \
      top_i/slash:$slash_user_inst \
      top_i/service_layer:$service_user_inst \
    ]

=======
>>>>>>> dev
  # Parent impl run remains 'impl_1'
  set_property PR_CONFIGURATION config_1 [get_runs impl_1]
  set_property strategy Congestion_SSI_SpreadLogic_high [get_runs impl_1]
  set_property STEPS.OPT_DESIGN.TCL.POST         [get_files *opt.post.tcl]                [get_runs impl_1]
  set_property STEPS.PLACE_DESIGN.TCL.PRE        [get_files *place.pre.tcl]               [get_runs impl_1]
  set_property STEPS.WRITE_DEVICE_IMAGE.TCL.PRE  [get_files *write_device_image.pre.tcl]  [get_runs impl_1]
<<<<<<< dev
  
# Child run renamed to '<project_name>_impl_1'
  create_run $child_run -parent_run impl_1 \
    -flow {Vivado Advanced Implementation 2025} \
    -pr_config $cfg2_name
  set_property strategy Congestion_SSI_SpreadLogic_high [get_runs $child_run]
  set_property STEPS.OPT_DESIGN.TCL.POST [ get_files *opt.post.tcl -of [get_fileset utils_1] ] [get_runs $child_run]

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

  set existing_cfg2 [get_pr_configurations -quiet $cfg2_name]
  if {[llength $existing_cfg2] > 0} {
    delete_pr_configurations $cfg2_name
  }

  puts "INFO: Building new PR configuration for proj_name='$proj_name'"
  write_hw_platform -force -fixed -minimal "../results/${proj_name}/top.xsa"
  # Project/user configuration
  create_pr_configuration -name $cfg2_name \
    -partitions [list \
      top_i/slash:$slash_user_inst \
      top_i/service_layer:$service_user_inst \
    ]

  # Child run renamed to '<project_name>_impl_1'
  create_run $child_run -parent_run impl_1 \
    -flow {Vivado Advanced Implementation 2025} \
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

=======

  # Launch and wait
  launch_runs impl_1 -to_step write_bitstream -jobs 14
  wait_on_run impl_1
  open_run impl_1
  file mkdir ../results/base
  write_abstract_shell -cell top_i/slash -force ../results/base/abs_shell_slash.dcp
  write_abstract_shell -cell top_i/service_layer -force ../results/base/abs_shell_service_layer.dcp

  puts "INFO: Implementation complete for run 'impl_1'."
}
>>>>>>> dev
