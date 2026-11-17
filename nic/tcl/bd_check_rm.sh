#!/bin/bash
# Block-design check of a service-layer RM (bd_check_rm.tcl): renders
# service_layer.tcl as build_service_rm.sh does and validates the block design,
# no synthesis. Same environment as build_service_rm.sh.
#   bd_check_rm.sh <work dir>
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
W=$(mkdir -p "${1:?work dir}" && cd "$1" && pwd)
V80=${V80:-$(dirname "$(dirname "$here")")}
SLASH=${SLASH:-$(dirname "$here")}
SLASHKIT=${SLASHKIT:-$V80/shell-nic-noc/pylib/slashkit}
STATIC_SHELL=${STATIC_SHELL:-$SLASHKIT/resources/static_shell}
SLASH_IPREPO=${SLASH_IPREPO:-$SLASHKIT/resources/base/common/iprepo}
export NIC_RM_KIND=${NIC_RM_KIND:-nictest}
if [ "$NIC_RM_KIND" = nic ]; then
    export NIC_DCMAC=${NIC_DCMAC:-$V80/dcmac-nic}
    export NIC_DYNAMIC=${NIC_DYNAMIC:-1}
fi
export PYTHONPATH="${PYTHONPATH:-}" LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
source /nfs/amd/2025.1/Vivado/settings64.sh
rm -rf "${W:?}/iprepo" "${W:?}/prj"
mkdir -p "$W/iprepo"
cp -r "$SLASH_IPREPO" "$W/iprepo/slash_base"
python3 "$here/tcl/gen_service_layer.py" "$SLASH" "$W/service_layer.tcl" "$here"
cd "$W"
vivado -mode batch -nojournal -log "$W/bd_check.log" -source "$here/tcl/bd_check_rm.tcl" \
    -tclargs "$W/prj" "$W/iprepo" "$STATIC_SHELL/static_shell_service_layer.dcp" \
    "$STATIC_SHELL/service_layer/service_layer.bd" "$W/service_layer.tcl"
grep -q "^BD_CHECK: done" "$W/bd_check.log"
