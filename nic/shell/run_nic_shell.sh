#!/bin/bash
# Full NIC static shell build (slashkit install, all stages: static
# implementation, AVED firmware, PDIs) on afrodita, in a memory-capped user
# scope (MemTotal - NIC_OS_GB) with a watchdog that stops it if host
# MemAvailable drops below NIC_OS_GB (default 2 GB, left for the OS).
#   [NIC_TAG=390] run_nic_shell.sh [jobs]   (start it inside tmux)
# Work tree shell-nic[-TAG] (pylib patched with patch_shell.py, build/ inside),
# unit nicshell[TAG], log builds/nic_shell[_TAG].log.
R=${NIC_WORK:-/home/aulmamei/rdma-v80}   # workspace: shell-nic*/ trees, builds/ logs
W=$R/shell-nic${NIC_TAG:+-$NIC_TAG}
JOBS=${1:-8}
unit=nicshell${NIC_TAG}
log=$R/builds/nic_shell${NIC_TAG:+_$NIC_TAG}.log
OS_GB=${NIC_OS_GB:-2}
MAX_GB=$(( $(awk '/MemTotal/ {print int($2/1048576)}' /proc/meminfo) - OS_GB ))
( started=""; while sleep 5; do
    if systemctl --user is-active --quiet $unit.scope; then started=1; elif [ -n "$started" ]; then break; fi
    avail=$(awk '/MemAvailable/ {print int($2/1048576)}' /proc/meminfo)
    if [ "$avail" -lt $OS_GB ]; then
      echo "WATCHDOG: MemAvailable ${avail} GB < $OS_GB GB, stopping $unit at $(date -u +%T)" >> $log
      systemctl --user stop $unit.scope; break
    fi
  done ) &
grep -q "NIC static (slash-roce shell/patch_shell.py)" $W/pylib/slashkit/resources/base/service/scripts/top.tcl \
  || { echo "shell-nic pylib is not patched" > $log; exit 1; }
# install_full.py: routed_bb name fix, impl results kept in builds/keep[_TAG].
# NIC_IGNORE_TIMING=1 lets the install finish on a timing failure (check the report).
HERE=$(cd "$(dirname "$0")" && pwd)
IGN=""; [ "${NIC_IGNORE_TIMING:-0}" = 1 ] && IGN="--ignore-timing-failure"
echo "start $(date -u +%FT%TZ) jobs $JOBS $IGN MemoryMax ${MAX_GB}G floor ${OS_GB}G" > $log
systemd-run --user --scope -p MemoryMax=${MAX_GB}G -p MemorySwapMax=0 --unit=$unit \
  bash -c "source /nfs/amd/2025.1/Vivado/settings64.sh; source /nfs/amd/2025.1/Vitis/settings64.sh;
           cd $W; export PYTHONPATH=$W/pylib NIC_KEEP=$R/builds/keep${NIC_TAG:+_$NIC_TAG};
           python3 $HERE/install_full.py install --shell-type service --build-dir $W/build \
             --out-dir $W/pylib/slashkit/resources --jobs $JOBS $IGN < /dev/null" >> $log 2>&1
echo "EXIT $? $(date -u +%FT%TZ)" >> $log
wait
