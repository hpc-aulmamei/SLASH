#!/bin/bash
# Re-launch the out-of-context synth runs, synth_1 and impl_1 of a NIC static
# shell build whose slashkit driver was stopped after the project was created
# (e.g. by the memory watchdog). The parent Vivado only opens the project, so it
# holds a few GB instead of the ~25 GB of the slashkit driver with the block
# designs loaded. Finished runs are kept; runs that were killed are reset.
# When this exits 0, finish with resume_nic_shell.sh (same NIC_TAG).
#   [NIC_TAG=noc] [NIC_REDO_IMPL=1] [NIC_DMA_OVERCONSTRAIN=0.2] rerun_nic_impl.sh [jobs]
#   (start it inside tmux)
# NIC_REDO_IMPL=1: reset impl_1 even if it completed (a timing retry; synthesis
#   is kept).
# NIC_DMA_OVERCONSTRAIN=<ns>: extra setup uncertainty on dma_clk for placement
#   and pre-route phys_opt only (UG949 over-constraining), cleared again before
#   route_design, so the routed design is timed against the real constraint.
#   The critical dma_clk logic is the CPM5 QDMA soft wrapper (cpm_0 qdma_1_wrapper_i
#   AXIST H2C desegmenter), which is placement-sensitive at 390 MHz.
# Same memory cap and watchdog as run_nic_shell.sh (MemTotal - NIC_OS_GB, floor NIC_OS_GB).
set -u
JOBS=${1:-8}
NIC_TAG=${NIC_TAG:-}
R=${NIC_WORK:-/home/aulmamei/rdma-v80}   # workspace: shell-nic*/ trees, builds/ logs
W=$R/shell-nic${NIC_TAG:+-$NIC_TAG}
B=$W/build
LOG=$R/builds/nic_rerun${NIC_TAG:+_$NIC_TAG}.log
unit=nicrerun${NIC_TAG}
REDO=${NIC_REDO_IMPL:-0}
OVC=${NIC_DMA_OVERCONSTRAIN:-}
OS_GB=${NIC_OS_GB:-2}
MAX_GB=$(( $(awk '/MemTotal/ {print int($2/1048576)}' /proc/meminfo) - OS_GB ))
DMA_PIN=top_i/static_region/aved/dma_clk_wiz/inst/clock_primitive_inst/MMCME5_inst/CLKOUT0
[ -f $B/slash.xpr ] || { echo "no project at $B/slash.xpr"; exit 2; }
pgrep -u "$USER" -f "unwrapped/lnx64.o/vivado" >/dev/null && { echo "a Vivado is still running"; exit 2; }
cat > $B/rerun_impl.tcl <<TCL
open_project $B/slash.xpr
set n_reset 0
if {$REDO} {
  puts "RERUN reset impl_1 (NIC_REDO_IMPL=1, was [get_property STATUS [get_runs impl_1]])"
  reset_run impl_1
  incr n_reset
}
if {"$OVC" ne ""} {
  # place.pre: the project's own hook, then the over-constraint
  set orig [get_property STEPS.PLACE_DESIGN.TCL.PRE [get_runs impl_1]]
  if {[string match "*nic_place_pre.tcl" \$orig]} { set orig [get_files *place.pre.tcl] }
  set f [open $B/nic_place_pre.tcl w]
  if {\$orig ne ""} { puts \$f "source \$orig" }
  puts \$f "set_clock_uncertainty -setup $OVC \\[get_clocks -of_objects \\[get_pins $DMA_PIN\\]\\]"
  puts \$f "puts \"NIC: dma_clk setup uncertainty +$OVC ns for place/phys_opt\""
  close \$f
  set f [open $B/nic_route_pre.tcl w]
  puts \$f "set_clock_uncertainty -setup 0 \\[get_clocks -of_objects \\[get_pins $DMA_PIN\\]\\]"
  puts \$f "puts \"NIC: dma_clk over-constraint cleared before route_design\""
  close \$f
  set_property STEPS.PLACE_DESIGN.TCL.PRE $B/nic_place_pre.tcl [get_runs impl_1]
  set_property STEPS.ROUTE_DESIGN.TCL.PRE $B/nic_route_pre.tcl [get_runs impl_1]
  puts "RERUN place.pre [get_property STEPS.PLACE_DESIGN.TCL.PRE [get_runs impl_1]] route.pre [get_property STEPS.ROUTE_DESIGN.TCL.PRE [get_runs impl_1]]"
}
foreach r [get_runs] {
  set s [get_property STATUS \$r]
  set p [get_property PROGRESS \$r]
  if {\$p ne "100%" && ![string match "Not started*" \$s]} {
    puts "RERUN reset \$r (\$s, \$p)"
    reset_run \$r
    incr n_reset
  }
}
puts "RERUN reset \$n_reset runs"
launch_runs impl_1 -to_step write_bitstream -jobs $JOBS
wait_on_run impl_1
set s [get_property STATUS [get_runs impl_1]]
puts "RERUN impl_1 status: \$s"
if {![string match "*Complete*" \$s]} { exit 1 }
TCL
( started=""; while sleep 5; do
    if systemctl --user is-active --quiet $unit.scope; then started=1; elif [ -n "$started" ]; then break; fi
    a=$(awk '/MemAvailable/ {print int($2/1048576)}' /proc/meminfo)
    if [ "$a" -lt $OS_GB ]; then
      echo "WATCHDOG: MemAvailable ${a} GB < $OS_GB GB, stopping $unit at $(date -u +%T)" >> $LOG
      systemctl --user stop $unit.scope; break
    fi
  done ) &
echo "start $(date -u +%FT%TZ) jobs $JOBS redo $REDO overconstrain ${OVC:-none} MemoryMax ${MAX_GB}G" > $LOG
systemd-run --user --scope -p MemoryMax=${MAX_GB}G -p MemorySwapMax=0 --unit=$unit bash -c "
  source /nfs/amd/2025.1/Vivado/settings64.sh
  cd $B; vivado -mode batch -nojournal -log $B/vivado_rerun.log -source $B/rerun_impl.tcl < /dev/null
" >> $LOG 2>&1
echo "EXIT $? $(date -u +%FT%TZ)" >> $LOG
wait
