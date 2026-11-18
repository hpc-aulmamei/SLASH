#!/bin/bash
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
# Self-checking xsim run of sl_vmem (tb_sl_vmem.sv).
#   run_tb_sl_vmem.sh <work dir> [seed ...]     (default seed 1)
set -e
here=$(cd "$(dirname "$0")" && pwd)
work=${1:?work dir}; shift
seeds=${*:-1}
export PYTHONPATH="${PYTHONPATH:-}" LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
source /nfs/amd/2025.1/Vivado/settings64.sh
mkdir -p "$work"
cd "$work"
r=$here/../rtl
xvlog -sv "$r/sl_vmem.sv" "$here/tb_sl_vmem.sv" > xvlog.log 2>&1 || { grep -E "ERROR|WARNING: \[VRFC" xvlog.log | head -30; exit 1; }
xvlog "$r/sl_vmem_bd.v" >> xvlog.log 2>&1 || { grep -E "ERROR" xvlog.log | head -30; exit 1; }
xelab -snapshot tb_sl_vmem work.tb_sl_vmem > xelab.log 2>&1 || true; if grep -q ERROR xelab.log; then grep ERROR xelab.log | head -30; exit 1; fi
rc=0
for s in $seeds; do
    xsim tb_sl_vmem -R -testplusarg seed=$s > xsim_$s.log 2>&1 || true
    grep -v "^Info\|^Time\|^INFO\|^\s*$\|^#\|^source\|^run\|^quit\|^//" xsim_$s.log | tail -40
    grep -q "TB_SL_VMEM PASS" xsim_$s.log || rc=1
done
exit $rc
