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

# Build and install the four-port service shell from this SLASH checkout.
#
#   qsfp4/build_shell.sh <build dir> [jobs]
#
# Runs `slashkit install --shell-type service` with this tree's linker, so the
# shell is the one in linker/slashkit/resources/base/service (both DCMACs in
# dual mode, all four QSFP cages on the boundary). Installs into
# linker/slashkit/resources/static_shell:
#   static_shell_service_layer.dcp, service_layer/   abstract shell and base BD (RM builds)
#   amd_v80_gen5x8_25.1_nofpt.pdi                     JTAG boot image (qsfp4/load.sh)
#   amd_v80_gen5x8_25.1.pdi                           flash image (not used by this test)
#   qsfp4_top_wrapper_routed.dcp                      routed full design (RM partials, pr_verify)
#
# Needs Vivado and Vitis 2025.1 on PATH (source both settings64.sh), the
# Versal-DCMAC submodule checked out, Python 3 with jinja2, and the SMBus IP
# (downloaded from AMD, not in the repository) copied into
# linker/slashkit/resources/base/common/iprepo/smbus*/, as
# docs/howto/install-from-packages.rst describes. The HLS IPs of that IP
# repository (traffic_producer, hbm_bandwidth) are built here with v++.
# Takes about 14 hours. Memory: about 8 GB per job on top of ~30 GB for the
# parent Vivado; jobs 8 fits in 128 GB, 12 in 192 GB.
#
# The build dir must not exist: slashkit install deletes --build-dir before it
# starts, so it can neither resume a build nor share a directory.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
SLASH=$(cd "$here/.." && pwd)
B=${1:?usage: build_shell.sh <build dir> [jobs]}
JOBS=${2:-8}
RES=$SLASH/linker/slashkit/resources

die() { echo "build_shell: $*" >&2; exit 1; }
[ ! -e "$B" ] || die "$B exists; slashkit install would delete it. Pick a new directory."
command -v vivado >/dev/null || die "vivado not on PATH: source Vivado 2025.1 settings64.sh"
command -v v++ >/dev/null || die "v++ not on PATH: source Vitis 2025.1 settings64.sh"
IPREPO=$RES/base/common/iprepo
ls -d "$IPREPO"/smbus*/ >/dev/null 2>&1 || die "no SMBus IP in $IPREPO/smbus*/: download it from AMD (docs/howto/install-from-packages.rst)"
case "$(git -C "$SLASH" submodule status submodules/Versal-DCMAC)" in
    " "*) ;;
    *) die "submodules/Versal-DCMAC is not at the recorded commit: git submodule update --init submodules/Versal-DCMAC" ;;
esac

# The shell sources the staged copy in resources/dcmac/versal before the
# submodule, so stage it from the submodule now (a stale copy would win).
"$SLASH/scripts/stage-versal-dcmac.sh"

# HLS IPs of the shell's default service layer and compute region
echo "build_shell: $(date '+%F %T %Z') HLS IPs in $IPREPO"
make -C "$IPREPO"

export PYTHONPATH="$SLASH/linker${PYTHONPATH:+:$PYTHONPATH}"
# slashkit is a namespace package (no __init__.py): check where its first path entry is
mod=$(cd /tmp && python3 -c 'import slashkit; print(list(slashkit.__path__)[0])')
[ "$mod" = "$SLASH/linker/slashkit" ] || die "python imports slashkit from $mod, not from this checkout"

mkdir -p "$(dirname "$B")"
B=$(cd "$(dirname "$B")" && pwd)/$(basename "$B")
echo "build_shell: $(date '+%F %T %Z') start, build dir $B, jobs $JOBS"
python3 -m slashkit install --shell-type service --build-dir "$B" --out-dir "$RES" --jobs "$JOBS" < /dev/null

routed=$B/slash.runs/impl_1/top_wrapper_routed.dcp
[ -f "$routed" ] || die "no routed full design at $routed"
cp "$routed" "$RES/static_shell/qsfp4_top_wrapper_routed.dcp"
grep -E "WNS|WHS" "$B/slash.runs/impl_1/"*timing_summary_routed.rpt 2>/dev/null | head -4 || true
echo "build_shell: $(date '+%F %T %Z') done"
ls -l "$RES/static_shell/"*.pdi "$RES/static_shell/"*.dcp
