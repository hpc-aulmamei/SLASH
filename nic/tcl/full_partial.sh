#!/bin/bash
# Replace the RM build's partial PDI (written from its abstract-shell run) by
# one written from the full design (full_partial.tcl). The abstract-shell one
# is kept as *_abstract.pdi.
#   full_partial.sh <build dir>
# Environment: FULL_SHELL_DCP  routed full design of the static shell
#              (shell build: slash.runs/impl_1/top_wrapper_routed.dcp)
set -e
here=$(cd "$(dirname "$0")" && pwd)
B=$(cd "${1:?build dir}" && pwd)
: "${FULL_SHELL_DCP:?set FULL_SHELL_DCP to the routed full design of the static shell}"
source /nfs/amd/2025.1/Vivado/settings64.sh
rm_dcp=$(ls "$B"/rm/service_layer_*.runs/impl_1/top_i_service_layer_service_layer_*_rm_routed.dcp)
pdi=$(ls "$B"/images/top_i_service_layer_service_layer_*_inst_0_partial.pdi)
abstract=${pdi%.pdi}_abstract.pdi
[ -f "$abstract" ] || cp "$pdi" "$abstract"
cd "$B/images"
vivado -mode batch -nojournal -log "$B/logs/full_partial.log" -source "$here/full_partial.tcl" \
    -tclargs "$FULL_SHELL_DCP" "$rm_dcp" "$pdi" "$B/rm/full_design_assembled.dcp"
ls -l "$pdi" "$abstract"
