#!/bin/bash
# xsim end-to-end test of the QDMA-over-NoC path (tb_qdma_noc.sv).
#   run_tb_qdma_noc.sh <work dir> [stress|perf ...]   (default: stress perf)
# Needs Vivado 2025.1 settings sourced. The golden ECC is the encoder of the
# CPM5 QDMA example design (CED cpm5_qdma_g5x8_st_perf, qdma_ecc_enc.sv).
set -e
W=${1:?work dir}; shift
MODES=${*:-stress perf}
HERE=$(cd "$(dirname "$0")" && pwd)
R=$HERE/../rtl
CED=${CED:-/nfs/amd/2025.1/Vivado/data/xhub/ced/XilinxCEDStore/ced/Xilinx/IPI/Versal_CPM_QDMA_EP_Design_PartBased/cpm5_qdma_g5x8_st_perf/src/exdes}
mkdir -p "$W" && cd "$W"
xvlog $R/qnoc_lib.v $R/qnoc_crc512.v $R/qnoc_ecc57.v $R/qdma_st_pipe.v $R/qdma_noc_static.v \
      $R/qdma_noc_rm.v $R/qdma_noc_bridge.v > xvlog.log 2>&1 || { tail -20 xvlog.log; exit 1; }
xvlog -sv $CED/qdma_ecc_enc.sv $HERE/tb_qdma_noc.sv >> xvlog.log 2>&1 || { grep -E "ERROR" xvlog.log | head -20; exit 1; }
xelab -debug off tb_qdma_noc -s tbq > xelab.log 2>&1 || { grep -E "ERROR" xelab.log | head -20; exit 1; }
for m in $MODES; do
    xsim tbq -R --testplusarg MODE=$m > sim_$m.log 2>&1 || true
    grep -E "ERROR|STRESS|PERF|PASS|FAIL" sim_$m.log | head -30
done
