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

# Load the four-port shell and the test RM onto a V80 over JTAG. Nothing is
# written to flash: after a power cycle the card boots its flash image again.
#
#   qsfp4/load.sh [-d 0000:1b:00] [--rm-only] <shell _nofpt.pdi> <partial.pdi>
#   qsfp4/load.sh [-d 0000:1b:00] --rm-only <partial.pdi>
#
# Run as a user with sudo, with xsdb on PATH (Vivado or Vivado Lab 2025.1).
# Order: vrtd stopped; the card's PCIe functions removed; the shell PDI booted
# over JTAG (smi/resources/versal_flash_pdi.tcl); functions rescanned; removed
# again; the partial programmed (tcl/load_partial.tcl); rescanned. Only the
# card's own functions are removed, never the bridge above it: removing a root
# port loses its BAR windows until the next reboot.
#
# Several V80s on the JTAG chain: set V80_CABLE_SERIAL or V80_TARGET_ID.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
SLASH=$(cd "$here/.." && pwd)
D=0000:1b:00
RM_ONLY=0
while [ $# -gt 0 ]; do
    case "$1" in
        -d) D=$2; shift 2 ;;
        --rm-only) RM_ONLY=1; shift ;;
        -h|--help) awk 'NR > 21 && /^#/ { sub(/^# ?/, ""); print; next } NR > 21 { exit }' "$0"; exit 0 ;;
        *) break ;;
    esac
done
die() { echo "load: STOP: $*" >&2; exit 1; }
if [ $RM_ONLY = 1 ]; then
    [ $# = 1 ] || die "usage: load.sh [-d BDF] --rm-only <partial.pdi>"
    PARTIAL=$(readlink -f "$1")
else
    [ $# = 2 ] || die "usage: load.sh [-d BDF] <shell _nofpt.pdi> <partial.pdi>"
    SHELL_PDI=$(readlink -f "$1"); PARTIAL=$(readlink -f "$2")
    [ -f "$SHELL_PDI" ] || die "no $SHELL_PDI"
    case "$SHELL_PDI" in *_nofpt.pdi) ;; *) die "$SHELL_PDI: JTAG needs the _nofpt image (no flash partition table)" ;; esac
fi
[ -f "$PARTIAL" ] || die "no $PARTIAL"
command -v xsdb >/dev/null || die "xsdb not on PATH"
[ -e /sys/bus/pci/devices/$D.0 ] || die "no PCIe function $D.0"

pfs() { ls -d /sys/bus/pci/devices/$D.* 2>/dev/null | sort -r; }
remove_pfs() {
    for f in $(pfs); do echo 1 | sudo tee "$f/remove" > /dev/null; done
}
rescan() {
    sleep 1
    echo 1 | sudo tee /sys/bus/pci/rescan > /dev/null
    sleep 2
    [ -e /sys/bus/pci/devices/$D.0 ] || die "$D.0 did not come back"
    for f in $(pfs | sort); do echo "  $(basename "$f") $(cat "$f/device") $(cat "$f/logic_uuid" 2>/dev/null || true)"; done
}
# BAR i of a function = line i + 1 of its sysfs resource file
bar2() {
    local b
    b=$(awk 'NR==3 {print $1}' /sys/bus/pci/devices/$D.2/resource)
    echo "  $D.2 BAR2 $b"
    [ "$b" != 0x0000000000000000 ] || die "PF2 BAR2 not assigned (the service-layer registers are behind it)"
}

vrtd_was=$(systemctl is-active vrtd.socket 2>/dev/null || true)
sudo systemctl stop vrtd.socket vrtd.service 2>/dev/null || true

if [ $RM_ONLY = 0 ]; then
    echo "=== shell over JTAG: $SHELL_PDI"
    md5sum "$SHELL_PDI"
    remove_pfs
    PDI_PATH=$SHELL_PDI xsdb "$SLASH/smi/resources/versal_flash_pdi.tcl" || die "shell load"
    rescan
    bar2
fi

echo "=== RM partial over JTAG: $PARTIAL"
md5sum "$PARTIAL"
remove_pfs
PDI_PATH=$PARTIAL xsdb "$here/tcl/load_partial.tcl" || die "partial load"
rescan
bar2

[ "$vrtd_was" = active ] && sudo systemctl start vrtd.socket
echo "=== loaded. Next: sudo $here/sw/qsfp_loop.py -d $D id"
