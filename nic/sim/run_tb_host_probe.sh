#!/bin/bash
# Run tb_host_probe (host_probe against a behavioural AXI slave) in xsim on the build host.
#   run_tb_host_probe.sh <work dir>
set -e
here=$(cd "$(dirname "$0")" && pwd)
work=${1:?work dir}
export PYTHONPATH="${PYTHONPATH:-}" LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
source /nfs/amd/2025.1/Vivado/settings64.sh
mkdir -p "$work"
cd "$work"
r=$here/../rtl
xvlog -sv "$r/host_probe.sv" "$here/tb_host_probe.sv" > xvlog.log 2>&1 || { grep -E "ERROR|WARNING: \[VRFC" xvlog.log | head -30; exit 1; }
xvlog "$r/host_probe_bd.v" >> xvlog.log 2>&1 || { grep -E "ERROR" xvlog.log | head -30; exit 1; }
xelab -snapshot tb_host_probe work.tb_host_probe > xelab.log 2>&1; if grep -q ERROR xelab.log; then grep ERROR xelab.log | head -30; exit 1; fi
xsim tb_host_probe -R > xsim.log 2>&1 || true
grep -v "^Info\|^Time\|^INFO\|^\s*$" xsim.log | tail -40
