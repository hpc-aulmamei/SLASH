set src_dir        [file dirname [file normalize [info script]]]
set cwd            [pwd]

import_files -fileset constrs_1 -norecurse "$src_dir/../constraints/impl.xdc"
import_files -fileset constrs_1 -norecurse "$src_dir/../constraints/impl.pins.xdc"
import_files -fileset utils_1   -norecurse "$src_dir/../constraints/opt.post.tcl"
import_files -fileset utils_1   -norecurse "$src_dir/../constraints/place.pre.tcl"
import_files -fileset utils_1   -norecurse "$src_dir/../constraints/write_device_image.pre.tcl"
set_property -dict { used_in_synthesis false    processing_order NORMAL } [get_files *impl.xdc]
set_property -dict { used_in_synthesis false    processing_order NORMAL } [get_files *impl.pins.xdc]

set_property STEPS.OPT_DESIGN.TCL.POST         [get_files *opt.post.tcl]                [get_runs impl_1]
set_property STEPS.PLACE_DESIGN.TCL.PRE        [get_files *place.pre.tcl]               [get_runs impl_1]
set_property STEPS.WRITE_DEVICE_IMAGE.TCL.PRE  [get_files *write_device_image.pre.tcl]  [get_runs impl_1]