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

# Test all four QSFP cages of a V80 loaded with qsfp4/load.sh.
#
#   qsfp4/test.sh [-d 0000:1b:00] [--seconds S] [--pair A-B ...] [--no-100g]
#
# Without --pair, all four ports run in GT near-end PMA loopback: no modules or
# cables needed. Each --pair A-B adds a cable between cages A and B (ports are
# SLASH numbers QSFPn, cage n + 1 on the bracket); the ports not in a pair stay
# in loopback. Steps:
#   1. all four testers answer (id)
#   2. 200G: traffic on every path for S seconds (default 30)
#   3. 100G (RMs built with QSFP_DYNAMIC=1, the default): both DCMACs switched
#      to 100CAUI-4 with RS(528) FEC; frame lengths 60 .. 8996 B, then S seconds
#   4. back to 200G, S seconds again
# Exits non-zero at the first failing step. Output also goes to qsfp4_test.log.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
D=0000:1b:00
SECS=30
PAIRS=()
DO100=1
while [ $# -gt 0 ]; do
    case "$1" in
        -d) D=$2; shift 2 ;;
        --seconds) SECS=$2; shift 2 ;;
        --pair) PAIRS+=("$2"); shift 2 ;;
        --no-100g) DO100=0; shift ;;
        -h|--help) awk 'NR > 21 && /^#/ { sub(/^# ?/, ""); print; next } NR > 21 { exit }' "$0"; exit 0 ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
done
exec > >(tee qsfp4_test.log) 2>&1
Q="sudo $here/sw/qsfp_loop.py -d $D"
die() { echo "STOP: $*"; $Q stop > /dev/null 2>&1; exit 1; }

# paths: the pairs, and every port not in a pair in loopback
PATHS=()
used=" "
for p in "${PAIRS[@]}"; do
    [[ $p =~ ^[0-3]-[0-3]$ ]] || die "--pair $p: expected A-B with A, B in 0..3"
    PATHS+=(--pair "$p"); used+="${p%-*} ${p#*-} "
done
for n in 0 1 2 3; do
    [[ $used == *" $n "* ]] || PATHS+=(--loop "$n")
done

echo "=== qsfp4 test $(date '+%F %T %Z'), $D, paths: ${PATHS[*]}"
md5sum "$here/sw/qsfp_loop.py"
echo "=== 1. testers"
$Q id || die "the testers do not answer: is the RM loaded (qsfp4/load.sh)?"
$Q stop > /dev/null

echo "=== 2. 200G, $SECS s"
$Q speed 200 > /dev/null 2>&1 || true          # 200G-only builds have no rate switch
$Q run "${PATHS[@]}" --seconds "$SECS" --interval 5 | tail -12
[ "${PIPESTATUS[0]}" = 0 ] || die "200G"

if [ $DO100 = 1 ]; then
    echo "=== 3. 100G"
    $Q speed 100 || die "rate switch to 100G (RM built with QSFP_DYNAMIC=0? then use --no-100g)"
    $Q up "${PATHS[@]}" | tail -4
    [ "${PIPESTATUS[0]}" = 0 ] || die "100G links"
    for L in 60 61 124 125 252 1496 8996; do
        echo "== 100G, $L B frames"
        $Q run --no-up "${PATHS[@]}" --len $L --seconds 2 --interval 1 | grep -E "^QSFP|^PASS|^FAIL"
        [ "${PIPESTATUS[0]}" = 0 ] || die "100G, $L B frames"
    done
    $Q run --no-up "${PATHS[@]}" --seconds "$SECS" --interval 5 | tail -12
    [ "${PIPESTATUS[0]}" = 0 ] || die "100G"

    echo "=== 4. back to 200G, $SECS s"
    $Q speed 200 || die "rate switch to 200G"
    $Q run "${PATHS[@]}" --seconds "$SECS" --interval 5 | tail -12
    [ "${PIPESTATUS[0]}" = 0 ] || die "200G after 100G"
fi
$Q stop > /dev/null
echo "=== PASS $(date '+%F %T %Z')"
