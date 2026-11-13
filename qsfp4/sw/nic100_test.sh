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

# V80 QSFP port P <-> NIC interface IF at 100G (100GBASE-CR4, RS-FEC), both directions.
#   ./nic100_test.sh P IF        e.g. ./nic100_test.sh 3 enp83s0f0np0
# Needs the four-port RM built with QSFP_DYNAMIC=1 (the default) loaded. DEV (default
# 0000:1b:00) is the card's bus:dev. Output also goes to nic100_test.log.
# The NIC's settings are changed (autoneg off, 100G, RS-FEC) and not restored; the
# restore commands are printed at the end.
cd "$(dirname "$0")" || exit 1
exec > >(tee nic100_test.log) 2>&1
P=${1:?port}; IF=${2:?interface}
DEV=${DEV:-0000:1b:00}; DC=$((P / 2))
V="sudo ./qsfp_loop.py -d $DEV"; N="sudo ./nic100.py -d $DEV"
die() { echo "STOP: $*"; exit 1; }
snap() { sudo ethtool -S $IF | awk -F: '/_phy:|out_of_buffer|discard/ {gsub(/ /,"",$1); gsub(/ /,"",$2); print $1, $2}' | sort > $1; }
delta() { join $1 $2 | awk '$3 != $2 {printf "  %-36s %d\n", $1, $3 - $2}'; }
T=$(mktemp -d)

echo "=== 0. $(date '+%F %H:%M')  QSFP$P (cage $((P + 1)), DCMAC$DC) <-> $IF"
md5sum qsfp_loop.py nic100.py nic_pktgen.sh
echo "NIC before: $(ip -br link show $IF)"
sudo ethtool --show-fec $IF | grep -i "active\|configured"
sudo ethtool -m $IF 2>&1 | grep -iE "identifier|vendor name|vendor pn|length|compliance" | head -8

echo "=== 1. V80 DCMAC$DC at 100G, NIC forced 100G RS-FEC"
# optical modules: the V80 leaves LPMode to the pin, so force high power in software
# (SFF-8636 byte 93: power override, high power); harmless for passive copper
sudo ami_tool module_byte_wr -d ${DEV#0000:}.0 -c $P -p 0 -b 93 -i 0x01 | tail -1
$V speed 100 --dcmac $DC | grep -E "TX_MODE|not done|does not follow"
sudo ip link set $IF up
sudo ethtool -s $IF autoneg off speed 100000 duplex full || die "NIC speed"
sudo ethtool --set-fec $IF encoding rs || echo "NIC: --set-fec rs refused"
$N up $P --timeout 20 || die "V80 link"
for i in $(seq 1 100); do [ "$(cat /sys/class/net/$IF/carrier 2>/dev/null)" = 1 ] && break; sleep 0.2; done
sudo ethtool $IF | grep -E "Speed|Duplex|Link detected"
sudo ethtool --show-fec $IF | grep -i active
[ "$(cat /sys/class/net/$IF/carrier)" = 1 ] || die "NIC link down"

for L in 1496 60 8996; do
  echo "=== 2. V80 -> NIC, tester at line rate, $L B frames, 10 s"
  snap $T/a; $N tx $P --seconds 10 --len $L; snap $T/b
  echo "NIC counters that changed:"; delta $T/a $T/b
done

for L in 1496 60; do
  echo "=== 3. NIC -> V80, pktgen 4 threads x 2000000 frames of $L B"
  snap $T/a
  $N rx $P --cmd "./nic_pktgen.sh $IF 4 2000000 $L 02:00:00:00:00:0$P"
  snap $T/b
  echo "NIC counters that changed:"; delta $T/a $T/b
done
$V stop > /dev/null
rm -rf $T
echo "=== done $(date '+%F %H:%M')"
echo "NIC back to autonegotiation later:  sudo ethtool -s $IF autoneg on; sudo ethtool --set-fec $IF encoding auto"
