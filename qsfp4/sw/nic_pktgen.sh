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

# Kernel pktgen on a NIC: nic_pktgen.sh IF THREADS COUNT SIZE DST_MAC
# THREADS kernel threads on the NIC's local CPUs, one TX queue each, COUNT frames
# each of SIZE bytes without FCS. Blocks until all are sent; prints each thread's result.
set -e
IF=$1; T=$2; COUNT=$3; SIZE=$4; DST=$5
[ -n "$DST" ] || { echo "usage: $0 IF THREADS COUNT SIZE DST_MAC"; exit 2; }
[ "$(id -u)" = 0 ] || { echo "run as root"; exit 2; }
modprobe pktgen
CPUS=$(python3 -c "
s = open('/sys/class/net/$IF/device/local_cpulist').read().strip()
out = []
for r in s.split(','):
    a, _, b = r.partition('-'); out += range(int(a), int(b or a) + 1)
print(' '.join(map(str, out[:$T])))")
pg() { echo "$2" > /proc/net/pktgen/$1; }
for c in $(ls /proc/net/pktgen | sed -n 's/^kpktgend_//p'); do pg kpktgend_$c rem_device_all; done
i=0
for c in $CPUS; do
  dev=$IF@$i
  pg kpktgend_$c "add_device $dev"
  pg $dev "count $COUNT"
  pg $dev "pkt_size $SIZE"
  pg $dev "clone_skb 1000"
  pg $dev "burst 32"
  pg $dev "delay 0"
  pg $dev "dst_mac $DST"
  pg $dev "dst 10.99.0.2"
  pg $dev "queue_map_min $i"
  pg $dev "queue_map_max $i"
  i=$((i + 1))
done
echo "pktgen: $i threads on CPUs $CPUS, $COUNT x $SIZE B each to $DST"
pg pgctrl start
i=0
for c in $CPUS; do grep -A2 "^Result" /proc/net/pktgen/$IF@$i | tr -s ' ' | head -3; i=$((i + 1)); done
for c in $CPUS; do pg kpktgend_$c rem_device_all; done
