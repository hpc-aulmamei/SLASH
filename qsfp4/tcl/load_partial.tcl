# ##################################################################################################
#  The MIT License (MIT)
#  Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
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

# Program a partial PDI into a running V80 over JTAG.
#
#   PDI_PATH=<partial.pdi> xsdb load_partial.tcl
#
# The target is the one "Versal xcv80" device on the JTAG chain; with several,
# pick one with V80_CABLE_SERIAL (cable serial, or a prefix of it) or
# V80_TARGET_ID (xsdb target id), as smi/resources/versal_flash_pdi.tcl does.
if {![info exists ::env(PDI_PATH)] || ![file exists $::env(PDI_PATH)]} {
    error "set PDI_PATH to the partial PDI"
}
connect
set ids {}
foreach t [targets -target-properties] {
    if {![dict exists $t name] || [dict get $t name] ne "Versal xcv80"} continue
    if {[info exists ::env(V80_CABLE_SERIAL)]} {
        if {![dict exists $t jtag_cable_serial] ||
            ![string match "$::env(V80_CABLE_SERIAL)*" [dict get $t jtag_cable_serial]]} continue
    }
    if {[info exists ::env(V80_TARGET_ID)] && [dict get $t target_id] ne $::env(V80_TARGET_ID)} continue
    lappend ids [dict get $t target_id]
}
if {[llength $ids] != 1} {
    puts [targets]
    error "load_partial: expected one Versal xcv80 target, found [llength $ids] ($ids); set V80_TARGET_ID or V80_CABLE_SERIAL"
}
targets [lindex $ids 0]
puts "load_partial: target [lindex $ids 0], device program $::env(PDI_PATH)"
device program $::env(PDI_PATH)
disconnect
puts "load_partial: done"
