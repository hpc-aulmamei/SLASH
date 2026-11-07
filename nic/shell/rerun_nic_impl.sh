#!/bin/bash
# Re-launch the out-of-context synth runs, synth_1 and impl_1 of a NIC static
# shell build whose slashkit driver was stopped after the project was created
# (e.g. by the memory watchdog). The parent Vivado only opens the project, so it
# holds a few GB instead of the ~25 GB of the slashkit driver with the block
# designs loaded. Finished runs are kept; runs that were killed are reset.
# When this exits 0, finish with resume_nic_shell.sh (same NIC_TAG).
#   [NIC_TAG=noc] rerun_nic_impl.sh [jobs]   (start it inside tmux)
# Same memory cap and watchdog as run_nic_shell.sh.
set -u
JOBS=${1:-8}
NIC_TAG=${NIC_TAG:-}
R=${NIC_WORK:-/home/aulmamei/rdma-v80}   # workspace: shell-nic*/ trees, builds/ logs
W=$R/shell-nic${NIC_TAG:+-$NIC_TAG}
B=$W/build
LOG=$R/builds/nic_rerun${NIC_TAG:+_$NIC_TAG}.log
unit=nicrerun${NIC_TAG}
[ -f $B/slash.xpr ] || { echo "no project at $B/slash.xpr"; exit 2; }
pgrep -u "$USER" -f "unwrapped/lnx64.o/vivado" >/dev/null && { echo "a Vivado is still running"; exit 2; }
cat > $B/rerun_impl.tcl <<TCL
open_project $B/slash.xpr
set n_reset 0
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
( started=""; while sleep 30; do
    if systemctl --user is-active --quiet $unit.scope; then started=1; elif [ -n "$started" ]; then break; fi
    a=$(awk '/MemAvailable/ {print int($2/1048576)}' /proc/meminfo)
    if [ "$a" -lt 30 ]; then
      echo "WATCHDOG: MemAvailable ${a} GB < 30 GB, stopping $unit at $(date -u +%T)" >> $LOG
      systemctl --user stop $unit.scope; break
    fi
  done ) &
echo "start $(date -u +%FT%TZ) jobs $JOBS" > $LOG
systemd-run --user --scope -p MemoryMax=100G -p MemorySwapMax=0 --unit=$unit bash -c "
  source /nfs/amd/2025.1/Vivado/settings64.sh
  cd $B; vivado -mode batch -nojournal -log $B/vivado_rerun.log -source $B/rerun_impl.tcl < /dev/null
" >> $LOG 2>&1
echo "EXIT $? $(date -u +%FT%TZ)" >> $LOG
wait
