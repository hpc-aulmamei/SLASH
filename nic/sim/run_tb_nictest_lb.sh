#!/bin/bash
# xsim test of the nictest RM's QDMA streaming loopback (tb_nictest_lb.sv):
# static adapter + NoC model + qnoc_lb_bd.
#   run_tb_nictest_lb.sh <work dir>
# Needs Vivado 2025.1 settings sourced. noc_ch comes from shell/sim/tb_qdma_noc.sv;
# the golden ECC is the CPM5 QDMA example design's encoder (qdma_ecc_enc.sv).
set -e
W=${1:?work dir}
HERE=$(cd "$(dirname "$0")" && pwd)
S=$HERE/../shell/rtl
R=${LB_RTL:-$HERE/../rtl}
CED=${CED:-/nfs/amd/2025.1/Vivado/data/xhub/ced/XilinxCEDStore/ced/Xilinx/IPI/Versal_CPM_QDMA_EP_Design_PartBased/cpm5_qdma_g5x8_st_perf/src/exdes}
mkdir -p "$W" && cd "$W"
xvlog $S/qnoc_lib.v $S/qnoc_crc512.v $S/qnoc_ecc57.v $S/qdma_st_pipe.v $S/qdma_noc_static.v \
      $S/qdma_noc_rm.v $S/qdma_noc_bridge.v $R/qnoc_lb_bd.v > xvlog.log 2>&1 || { grep -E "ERROR" xvlog.log | head -20; exit 1; }
xvlog -sv $R/qnoc_lb.sv $CED/qdma_ecc_enc.sv $HERE/../shell/sim/tb_qdma_noc.sv $HERE/tb_nictest_lb.sv \
      >> xvlog.log 2>&1 || { grep -E "ERROR" xvlog.log | head -20; exit 1; }
xelab -debug off tb_nictest_lb -s tbl > xelab.log 2>&1 || { grep -E "ERROR" xelab.log | head -20; exit 1; }
xsim tbl -R > sim.log 2>&1 || true
grep -E "ERROR|LOOPBACK|GEN:|PASS|FAIL" sim.log | head -40
