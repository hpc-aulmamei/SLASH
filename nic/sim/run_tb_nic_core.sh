#!/bin/bash
# Run tb_nic_core (nic_core, two ports joined by a cable model) in xsim on the build host.
#   run_tb_nic_core.sh <work dir>
set -e
here=$(cd "$(dirname "$0")" && pwd)
work=${1:?work dir}
export PYTHONPATH="${PYTHONPATH:-}" LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
source /nfs/amd/2025.1/Vivado/settings64.sh
mkdir -p "$work"
cd "$work"
r=$here/../rtl
xvlog "$here/../shell/rtl/qnoc_lib.v" > xvlog.log 2>&1 || { grep -E "ERROR" xvlog.log | head -30; exit 1; }
xvlog -sv "$r/nic_core.sv" "$here/tb_nic_core.sv" >> xvlog.log 2>&1 || { grep -E "ERROR|WARNING: \[VRFC" xvlog.log | head -30; exit 1; }
xelab -snapshot tb_nic_core work.tb_nic_core > xelab.log 2>&1; if grep -q ERROR xelab.log; then grep ERROR xelab.log | head -30; exit 1; fi
xsim tb_nic_core -R > xsim.log 2>&1 || true
grep -v "^Info\|^Time\|^INFO\|^\s*$" xsim.log | tail -40
