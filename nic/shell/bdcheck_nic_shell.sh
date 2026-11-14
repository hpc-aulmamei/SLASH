#!/bin/bash
# Block-design check of a patched NIC static tree: create_project action
# create (block designs, address map, validate_bd_design with the NoC
# compiler, wrapper, constraints), no synthesis. Minutes, not hours.
#   [NIC_TAG=smart] bdcheck_nic_shell.sh <iprepo dir>
# Work tree shell-nic[-TAG]; output builds/bdcheck[_TAG]/, log create.log there.
R=${NIC_WORK:-/home/aulmamei/rdma-v80}
W=$R/shell-nic${NIC_TAG:+-$NIC_TAG}
B=$R/builds/bdcheck${NIC_TAG:+_$NIC_TAG}
IPREPO=${1:?iprepo dir (e.g. builds/nic_bdcheck/iprepo)}
[ -e "$B" ] && { echo "$B exists"; exit 1; }
mkdir -p "$B" && cp -a "$IPREPO" "$B/iprepo"
cd "$B" || exit 1
echo "start $(date -u +%FT%TZ)" > run.log
systemd-run --user --scope -p MemoryMax=40G -p MemorySwapMax=0 --unit=bdcheck${NIC_TAG} \
  bash -c "source /nfs/amd/2025.1/Vivado/settings64.sh; cd $B;
           vivado -mode batch -nojournal -log $B/create.log \
             -source $W/pylib/slashkit/resources/base/service/scripts/create_project.tcl \
             -tclargs slash_install $B/iprepo create 8 < /dev/null" >> run.log 2>&1
echo "EXIT $? $(date -u +%FT%TZ)" >> run.log
