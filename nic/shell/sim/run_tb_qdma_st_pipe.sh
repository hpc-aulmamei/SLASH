#!/bin/bash
# xsim run of tb_qdma_st_pipe for STAGES 1, 2 and 3.
#   run_tb_qdma_st_pipe.sh <work dir>
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
W=$(mkdir -p "${1:?work dir}" && cd "$1" && pwd)
source /nfs/amd/2025.1/Vivado/settings64.sh
cd "$W"
rc=0
for s in 1 2 3; do
    xvlog -nolog "$here/rtl/qdma_st_pipe.v" > xvlog_$s.log
    xvlog -nolog -sv "$here/sim/tb_qdma_st_pipe.sv" >> xvlog_$s.log
    xelab -nolog -generic_top STAGES=$s tb_qdma_st_pipe -s tb_$s > xelab_$s.log
    xsim -nolog tb_$s -R > xsim_$s.log
    grep -E "STAGES=|ERROR|PASS|FAIL" xsim_$s.log | head -12
    grep -q "TB_QDMA_ST_PIPE PASS" xsim_$s.log || rc=1
done
exit $rc
