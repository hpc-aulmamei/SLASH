#!/bin/bash
# Full NIC static shell build (slashkit install, all stages: static
# implementation, AVED firmware, PDIs) on afrodita, in a memory-capped user
# scope with a watchdog that stops it if host MemAvailable drops below 30 GB.
#   [NIC_TAG=390] run_nic_shell.sh [jobs]   (start it inside tmux)
# Work tree shell-nic[-TAG] (pylib patched with patch_shell.py, build/ inside),
# unit nicshell[TAG], log builds/nic_shell[_TAG].log.
R=${NIC_WORK:-/home/aulmamei/rdma-v80}   # workspace: shell-nic*/ trees, builds/ logs
W=$R/shell-nic${NIC_TAG:+-$NIC_TAG}
JOBS=${1:-8}
unit=nicshell${NIC_TAG}
log=$R/builds/nic_shell${NIC_TAG:+_$NIC_TAG}.log
( started=""; while sleep 30; do
    if systemctl --user is-active --quiet $unit.scope; then started=1; elif [ -n "$started" ]; then break; fi
    avail=$(awk '/MemAvailable/ {print int($2/1048576)}' /proc/meminfo)
    if [ "$avail" -lt 30 ]; then
      echo "WATCHDOG: MemAvailable ${avail} GB < 30 GB, stopping $unit at $(date -u +%T)" >> $log
      systemctl --user stop $unit.scope; break
    fi
  done ) &
grep -q "NIC static (slash-roce shell/patch_shell.py)" $W/pylib/slashkit/resources/base/service/scripts/top.tcl \
  || { echo "shell-nic pylib is not patched" > $log; exit 1; }
echo "start $(date -u +%FT%TZ) jobs $JOBS" > $log
systemd-run --user --scope -p MemoryMax=100G -p MemorySwapMax=0 --unit=$unit \
  bash -c "source /nfs/amd/2025.1/Vivado/settings64.sh; source /nfs/amd/2025.1/Vitis/settings64.sh;
           cd $W; export PYTHONPATH=$W/pylib;
           python3 -m slashkit install --shell-type service --build-dir $W/build \
             --out-dir $W/pylib/slashkit/resources --jobs $JOBS < /dev/null" >> $log 2>&1
echo "EXIT $? $(date -u +%FT%TZ)" >> $log
wait
