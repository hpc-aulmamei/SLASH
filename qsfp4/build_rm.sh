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

# Build the four-port Ethernet test RM (service layer) for the four-port shell,
# write its partial PDI from the full design and pr_verify it against the shell.
#
#   qsfp4/build_rm.sh <build dir> [jobs]
#
# Needs the shell installed by qsfp4/build_shell.sh (or set the variables below),
# Vivado 2025.1 on PATH and the Versal-DCMAC submodule checked out. Takes about
# 5 hours at 8 jobs.
#
# Environment (defaults in brackets):
#   SLASH_RESOURCES  [<this checkout>/linker/slashkit/resources]  installed shell resources
#   FULL_SHELL_DCP   [$SLASH_RESOURCES/static_shell/qsfp4_top_wrapper_routed.dcp]
#   QSFP_DYNAMIC     [1]  runtime 200G / 100G switching; 0 builds 200G only
#
# Output: <build dir>/images/qsfp4_partial.pdi (written from the full design;
# the RM run's own partial, from the abstract shell, is kept as *_abstract.pdi).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
SLASH=$(cd "$here/.." && pwd)
B=$(mkdir -p "${1:?usage: build_rm.sh <build dir> [jobs]}" && cd "$1" && pwd)
JOBS=${2:-8}
RES=${SLASH_RESOURCES:-$SLASH/linker/slashkit/resources}
STATIC_SHELL=$RES/static_shell
FULL_SHELL_DCP=${FULL_SHELL_DCP:-$STATIC_SHELL/qsfp4_top_wrapper_routed.dcp}
export QSFP_DYNAMIC=${QSFP_DYNAMIC:-1}

die() { echo "build_rm: $*" >&2; exit 1; }
command -v vivado >/dev/null || die "vivado not on PATH: source Vivado 2025.1 settings64.sh"
for f in "$STATIC_SHELL/static_shell_service_layer.dcp" "$STATIC_SHELL/service_layer/service_layer.bd" "$FULL_SHELL_DCP"; do
    [ -f "$f" ] || die "missing $f (build the shell first: qsfp4/build_shell.sh)"
done
case "$(git -C "$SLASH" submodule status submodules/Versal-DCMAC)" in
    " "*) ;;
    *) die "submodules/Versal-DCMAC is not at the recorded commit: git submodule update --init submodules/Versal-DCMAC" ;;
esac
DCMAC=$SLASH/submodules/Versal-DCMAC

echo "build_rm: $(date '+%F %T %Z') start, build dir $B, jobs $JOBS, QSFP_DYNAMIC=$QSFP_DYNAMIC"
{
    echo "SLASH $(git -C "$SLASH" rev-parse HEAD) $(git -C "$SLASH" status --porcelain -- qsfp4 | wc -l) changed files in qsfp4/"
    echo "Versal-DCMAC $(git -C "$DCMAC" rev-parse HEAD)"
    echo "shell $FULL_SHELL_DCP $(md5sum < "$FULL_SHELL_DCP" | cut -c1-32)"
    echo "QSFP_DYNAMIC=$QSFP_DYNAMIC"
} > "$B/sources.txt"

mkdir -p "$B/results" "$B/iprepo" "$B/logs"
rm -rf "$B/iprepo/slash_base"
cp -r "$RES/base/common/iprepo" "$B/iprepo/slash_base"
python3 "$here/tcl/gen_service_layer.py" "$SLASH" "$B/results/service_layer.tcl" "$here" "$DCMAC"
SLASH_OPT_POST=$SLASH/linker/slashkit/resources/base/service/constraints/service_layer/eth/service_layer_eth.opt.post.tcl
sed "s|@SLASH_OPT_POST@|$SLASH_OPT_POST|" "$here/tcl/opt_post.tcl.in" > "$B/results/service_layer_eth.opt.post.tcl"

cd "$B"
vivado -mode batch -nojournal -log "$B/logs/service_layer_build.log" \
    -source "$SLASH/linker/slashkit/resources/base/service/scripts/service_layer_build.tcl" \
    -tclargs \
    --project-name qsfploop \
    --ip-repo "$B/iprepo" \
    --static-shell-dcp "$STATIC_SHELL/static_shell_service_layer.dcp" \
    --base-bd "$STATIC_SHELL/service_layer/service_layer.bd" \
    --opt-post-tcl "$B/results/service_layer_eth.opt.post.tcl" \
    --linker-results-dir "$B/results" \
    --rm-work-dir "$B/rm" \
    --artifact-out-dir "$B/images" \
    --jobs "$JOBS"

# The 391 MHz DCMAC paths are tight: a post-route phys_opt pass if routed timing missed
impl=$B/rm/service_layer_qsfploop.runs/impl_1
rpt=$impl/route_report_timing_summary_0.rpt
rm_dcp=$impl/top_i_service_layer_service_layer_qsfploop_rm_routed.dcp
if [ -f "$rpt" ] && ! grep -q "All user specified timing constraints are met" "$rpt"; then
    echo "build_rm: routed timing not met, running post_route_fix"
    [ -f "$rm_dcp.unfixed" ] || cp "$rm_dcp" "$rm_dcp.unfixed"
    if ! (cd "$B/logs" && vivado -mode batch -nojournal -log "$B/logs/post_route_fix.log" \
            -source "$here/tcl/post_route_fix.tcl" \
            -tclargs "$impl/top_wrapper_routed.dcp" "$rm_dcp" "$B/logs/post_route_fix"); then
        # Missed by more than phys_opt recovers: place and route again from the
        # opt checkpoint with other directives (tcl/replace_rm.tcl) until one
        # closes; F, with the DCMAC1 fabric pblock hard, first.
        met=0
        for v in F B C A; do
            echo "build_rm: $(date '+%F %T %Z') re-place with set $v"
            if (cd "$B/logs" && vivado -mode batch -nojournal -log "$B/logs/replace_rm_$v.log" \
                    -source "$here/tcl/replace_rm.tcl" \
                    -tclargs "$impl/top_wrapper_opt.dcp" "$v" "$rm_dcp" "$B/logs/replace_rm_$v"); then
                met=1; break
            fi
        done
        [ $met = 1 ] || die "timing not met with any directive set, see $B/logs/replace_rm_*"
    fi
fi

rm_pdi=$B/images/top_i_service_layer_service_layer_qsfploop_inst_0_partial.pdi
[ -f "$rm_pdi" ] && mv "$rm_pdi" "$B/images/qsfp4_partial_abstract.pdi"
pdi=$B/images/qsfp4_partial.pdi
cd "$B/images"
vivado -mode batch -nojournal -log "$B/logs/full_partial.log" -source "$here/tcl/full_partial.tcl" \
    -tclargs "$FULL_SHELL_DCP" "$rm_dcp" "$pdi" "$B/rm/full_design_assembled.dcp"

cd "$B/logs"
vivado -mode batch -nojournal -log "$B/logs/pr_verify.log" -source "$here/tcl/pr_verify.tcl" \
    -tclargs "$FULL_SHELL_DCP" "$B/rm/full_design_assembled.dcp" "$B/logs/pr_verify.rpt"
grep -q "^PR_VERIFY: compatible" "$B/logs/pr_verify.log" || die "pr_verify failed, see $B/logs/pr_verify.log"

md5sum "$pdi" | tee "$pdi.md5"
echo "build_rm: $(date '+%F %T %Z') done: $pdi"
