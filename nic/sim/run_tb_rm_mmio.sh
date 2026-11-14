#!/bin/bash
# Run tb_rm_mmio (rm_mmio against a behavioural single-beat AXI slave) in xsim on the build host.
#   run_tb_rm_mmio.sh <work dir>
set -e
here=$(cd "$(dirname "$0")" && pwd)
work=${1:?work dir}
export PYTHONPATH="${PYTHONPATH:-}" LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
source /nfs/amd/2025.1/Vivado/settings64.sh
mkdir -p "$work"
cd "$work"
r=$here/../rtl
xvlog -sv "$r/rm_mmio.sv" "$here/tb_rm_mmio.sv" > xvlog.log 2>&1 || { grep -E "ERROR|WARNING: \[VRFC" xvlog.log | head -30; exit 1; }
xvlog "$r/rm_mmio_bd.v" >> xvlog.log 2>&1 || { grep -E "ERROR" xvlog.log | head -30; exit 1; }
xelab -snapshot tb_rm_mmio work.tb_rm_mmio > xelab.log 2>&1; if grep -q ERROR xelab.log; then grep ERROR xelab.log | head -30; exit 1; fi
xsim tb_rm_mmio -R > xsim.log 2>&1 || true
grep -v "^Info\|^Time\|^INFO\|^\s*$" xsim.log | tail -40
