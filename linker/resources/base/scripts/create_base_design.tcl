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

create_project "slash" "$src_dir/../build" -part xcv80-lsva4737-2MHP-e-S -force

set previous_ip_repo_paths [get_property ip_repo_paths [current_project]]
set slash_ip_repo_path [file normalize [file join $src_dir ".." "iprepo"]]
set_property ip_repo_paths [concat $previous_ip_repo_paths $slash_ip_repo_path] [current_project]
update_ip_catalog

# Base shell / containers
source [file normalize [file join $src_dir "slash_base.tcl"]]
source [file normalize [file join $src_dir "service_layer.tcl"]]
source [file normalize [file join $src_dir "top.tcl"]]
source [file normalize [file join $src_dir "enable_dfx_bdc.tcl"]]

archive_project -exclude_run_results -force $src_dir/slash_base.zip
