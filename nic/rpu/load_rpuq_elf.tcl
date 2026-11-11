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

# Load the RPU QDMA test ELF onto R5_1 of the V80 (replaces RP1 until the next static load).
# Never touches R5_0 (AMC). Picks "Cortex-R5 #1" under the one "Versal xcv80" device only,
# so another board on the same hw_server (e.g. a VPK180) is never selected.
#   xsdb load_rpuq_elf.tcl            (optional: V80_CABLE_SERIAL=<serial prefix>)
set elf [expr {[info exists ::env(RPUQ_ELF)] ? $::env(RPUQ_ELF) : [file join [file dirname [file normalize [info script]]] rpuq.elf]}]
connect

proc find_r51 {} {
    set props [targets -target-properties]
    set parent [dict create]
    set name [dict create]
    set id [dict create]
    set v80 {}
    foreach t $props {
        if {![dict exists $t target_ctx]} { continue }
        set c [dict get $t target_ctx]
        dict set parent $c [expr {[dict exists $t parent_ctx] ? [dict get $t parent_ctx] : ""}]
        dict set name $c [expr {[dict exists $t name] ? [dict get $t name] : ""}]
        dict set id $c [expr {[dict exists $t target_id] ? [dict get $t target_id] : ""}]
        if {[dict get $name $c] eq "Versal xcv80"} {
            if {[info exists ::env(V80_CABLE_SERIAL)] && !([dict exists $t jtag_cable_serial] &&
                    [string match "$::env(V80_CABLE_SERIAL)*" [dict get $t jtag_cable_serial]])} { continue }
            lappend v80 $c
        }
    }
    if {[llength $v80] != 1} { return [list "" "Versal xcv80 targets: [llength $v80]"] }
    set hits {}
    dict for {c n} $name {
        if {![string match "Cortex-R5 #1*" $n]} { continue }
        set p $c
        for {set i 0} {$i < 6 && $p ne ""} {incr i} {
            set p [dict get $parent $p]
            if {$p eq [lindex $v80 0]} { lappend hits [dict get $id $c]; break }
            if {![dict exists $parent $p]} { break }
        }
    }
    if {[llength $hits] != 1} { return [list "" "Cortex-R5 #1 under the xcv80: [llength $hits]"] }
    return [list [lindex $hits 0] ok]
}

# hw_server may present a partial list right after it starts: poll for up to 10 s
set r [list "" "not polled"]
for {set w 0} {$w < 40} {incr w} {
    set r [find_r51]
    if {[lindex $r 0] ne ""} { break }
    after 250
}
if {[lindex $r 0] eq ""} {
    puts "RPUQ: no unique V80 R5_1 target ([lindex $r 1]); nothing was stopped or loaded"
    puts [targets]
    disconnect
    exit 1
}
set tid [lindex $r 0]
puts "RPUQ: V80 R5_1 is target $tid"
targets $tid
catch {stop}
# No "rst -processor": on this card it first activates the default subsystem in the PLM,
# and after it the PCIe functions had lost their BARs. The ELF masks interrupts and
# resets its MPU, caches and vectors itself.
dow $elf
con
after 300
# the list shows both R5 cores of the V80: #0 (AMC) must still be Running
puts [targets]
disconnect
