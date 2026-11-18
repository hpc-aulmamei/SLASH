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
# Build a service-layer reconfigurable module of the V80 NIC static shell and
# write its partial PDI. RM kinds (tcl/<kind>_service_layer.tcl):
#   nictest  host_probe + QDMA streaming loopback (qnoc_lb), no DCMAC
#   slvmem   nictest + sl_vmem (user region -> host address translator, in
#            place of axi4_full_passthrough_4) + a second host_probe in front of it
#
#   service_rm.sh <build dir> [jobs]
#
# Environment:
#   RM_KIND      nictest (default) or slvmem
#   SLASH        SLASH checkout (dev): service-layer template and RM build
#                script; default: this repository
#   SLASHKIT     the NIC static's slashkit tree (shell/patch_shell.py on the
#                patched SLASH 1.0.0 service shell); default $V80/shell-nic-noc/pylib/slashkit
#   STATIC_SHELL installed static shell (static_shell_service_layer.dcp, service_layer/service_layer.bd)
#   SLASH_IPREPO SLASH base IP repository (axi4_full_passthrough, ...)
#   FULL_SHELL_DCP routed full design of the static shell; when set, the partial
#                is rewritten from the full design (full_partial.sh), which the
#                patched 1.0.0 shell needs (abstract-shell partials hang the PLM)
#
# Output: <build dir>/images/top_i_service_layer_service_layer_<kind>_inst_0_partial.pdi
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
B=$(mkdir -p "${1:?build dir}" && cd "$1" && pwd)
JOBS=${2:-12}
V80=${V80:-$(dirname "$(dirname "$here")")}     # workspace holding the static shell trees
SLASH=${SLASH:-$(dirname "$here")}
RM_KIND=${RM_KIND:-nictest}
SLASHKIT=${SLASHKIT:-$V80/shell-nic-noc/pylib/slashkit}  # NIC static (shell/patch_shell.py on slashkit 1.0.0 m05)
STATIC_SHELL=${STATIC_SHELL:-$SLASHKIT/resources/static_shell}
SLASH_IPREPO=${SLASH_IPREPO:-$SLASHKIT/resources/base/common/iprepo}

export PYTHONPATH="${PYTHONPATH:-}" LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
source /nfs/amd/2025.1/Vivado/settings64.sh

mkdir -p "$B/results" "$B/iprepo" "$B/logs"
rm -rf "$B/iprepo/slash_base"
cp -r "$SLASH_IPREPO" "$B/iprepo/slash_base"
python3 "$here/tcl/gen_service_layer.py" "$SLASH" "$B/results/service_layer.tcl" "$here" "$RM_KIND"
SLASH_OPT_POST=$SLASH/linker/slashkit/resources/base/service/constraints/service_layer/eth/service_layer_eth.opt.post.tcl
sed "s|@SLASH_OPT_POST@|$SLASH_OPT_POST|" "$here/tcl/nictest_opt_post.tcl.in" > "$B/results/service_layer_eth.opt.post.tcl"

cd "$B"
vivado -mode batch -nojournal -log "$B/logs/service_layer_build.log" \
    -source "$SLASH/linker/slashkit/resources/base/service/scripts/service_layer_build.tcl" \
    -tclargs \
    --project-name "$RM_KIND" \
    --ip-repo "$B/iprepo" \
    --static-shell-dcp "$STATIC_SHELL/static_shell_service_layer.dcp" \
    --base-bd "$STATIC_SHELL/service_layer/service_layer.bd" \
    --opt-post-tcl "$B/results/service_layer_eth.opt.post.tcl" \
    --linker-results-dir "$B/results" \
    --rm-work-dir "$B/rm" \
    --artifact-out-dir "$B/images" \
    --jobs "$JOBS"
ls -l "$B/images"

# Routed timing missed (WNS or WHS < 0): post-route phys_opt on the routed
# design (post_route_fix.tcl); the fixed RM cell replaces impl_1's for the
# full-design partial. Fails the build if timing is still not met.
impl=$B/rm/service_layer_$RM_KIND.runs/impl_1
rpt=$impl/route_report_timing_summary_0.rpt
if [ -f "$rpt" ] && ! grep -q "All user specified timing constraints are met" "$rpt"; then
    echo "build_service_rm: routed timing not met, running post_route_fix"
    rm_dcp=$impl/top_i_service_layer_service_layer_${RM_KIND}_rm_routed.dcp
    [ -f "$rm_dcp.unfixed" ] || cp "$rm_dcp" "$rm_dcp.unfixed"
    (cd "$B/logs" && vivado -mode batch -nojournal -log "$B/logs/post_route_fix.log" \
        -source "$here/tcl/post_route_fix.tcl" \
        -tclargs "$impl/top_wrapper_routed.dcp" "$rm_dcp" "$B/logs/post_route_fix")
    if [ -z "${FULL_SHELL_DCP:-}" ]; then
        echo "build_service_rm: images/ still holds the partial of the unfixed design; set FULL_SHELL_DCP"
    fi
fi
if [ -n "${FULL_SHELL_DCP:-}" ]; then
    "$here/tcl/full_partial.sh" "$B"
fi
