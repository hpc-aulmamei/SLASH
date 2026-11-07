#!/bin/bash
# Summarise a finished RM build: routed timing (with the clocks it covers and
# the unconstrained pin count), service-layer utilization, partial PDI.
#   check_rm.sh <build dir>
B=${1:?build dir}
impl=$(ls -d $B/rm/service_layer_*.runs/impl_1 2>/dev/null | head -1)
rpt=$(ls $impl/*timing_summary*routed*.rpt $impl/route_report_timing_summary_*.rpt 2>/dev/null | head -1)
if [ -z "$rpt" ]; then echo "no routed timing report in $impl"; exit 1; fi
echo "== timing ($rpt)"
grep -A3 "WNS(ns)" "$rpt" | head -4
grep -E "Timing constraints are not met|All user specified timing constraints are met" "$rpt" | head -1
grep "pins that are not constrained for maximum delay\." "$rpt"
echo "== clocks (intra-clock WNS / WHS)"
awk '/^\| Intra Clock Table/,/^\| Inter Clock Table/' "$rpt" | awk 'NF >= 3 && $2 ~ /^-?[0-9.]+$/ {printf "  %-70s WNS %8s  WHS %8s\n", $1, $2, $6}' | head -40
util=$(ls $impl/*utilization_placed.rpt $impl/place_report_utilization_*.rpt 2>/dev/null | head -1)
echo "== utilization ($util)"
grep -E "^\| (CLB LUTs|CLB Registers|Block RAM Tile|URAM|DSP Slices)" "$util" | head -6
echo "== route status"
grep -E "nets with routing errors" $impl/route_report_route_status_*.rpt 2>/dev/null | head -1
echo "== images"
ls -l $B/images/*.pdi 2>/dev/null
