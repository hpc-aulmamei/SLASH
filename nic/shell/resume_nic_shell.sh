#!/bin/bash
# Finish a NIC static shell build whose slashkit/Vivado driver died after impl_1
# was launched, while impl_1 itself ran to completion (write_device_image).
#   [NIC_TAG=390] [SKIP_POST_IMPL=1] resume_nic_shell.sh [jobs] [--ignore-timing-failure]
# SKIP_POST_IMPL=1: build_project.tcl already finished (only slashkit's timing
# check stopped the install), so step 1 is skipped and its outputs are reused.
# Step 1: open the project, open_run impl_1, write the timing report and the two
#         abstract shells (the tail of build_project.tcl).
# Step 2: slashkit install --stage base-shell with the project step replaced by
#         step 1's output, then --stage firmware (AVED firmware + PDI repack).
# Same memory cap and watchdog as run_nic_shell.sh.
set -u
JOBS=${1:-8}
IGN=${2:-}
NIC_TAG=${NIC_TAG:-}
R=${NIC_WORK:-/home/aulmamei/rdma-v80}   # workspace: shell-nic*/ trees, builds/ logs
W=$R/shell-nic${NIC_TAG:+-$NIC_TAG}
LOG=$R/builds/nic_resume${NIC_TAG:+_$NIC_TAG}.log
unit=nicresume${NIC_TAG}
B=$W/build
grep -q "write_device_image" $B/slash.runs/impl_1/runme.log && \
  ls $B/slash.runs/impl_1/top_wrapper.pdi >/dev/null 2>&1 || { echo "impl_1 not complete"; exit 2; }
pgrep -f "top_wrapper.vdi" >/dev/null && { echo "impl_1 still running"; exit 2; }

cat > $B/resume_post_impl.tcl <<'TCL'
open_project @B@/slash.xpr
open_run impl_1
set timing_report_file [file join [file normalize [pwd]] "report_timing_slash_install.txt"]
report_timing_summary -delay_type min_max -check_timing_verbose -max_paths 1 -input_pins -routable_nets -file $timing_report_file
puts "TIMING REPORT: $timing_report_file"
set impl_output_dir [get_property DIRECTORY [current_run]]
write_abstract_shell -cell top_i/slash -force [file join $impl_output_dir "static_shell_slash.dcp"]
write_abstract_shell -cell top_i/service_layer -force [file join $impl_output_dir "static_shell_service_layer.dcp"]
# with post-route phys_opt the run writes no debug_nets.ltx; write it from the routed design
if {![file exists [file join $impl_output_dir "debug_nets.ltx"]]} {
  write_debug_probes -force [file join $impl_output_dir "debug_nets.ltx"]
}
puts "INFO: Implementation complete for run 'impl_1'."
TCL
sed -i "s#@B@#$B#" $B/resume_post_impl.tcl
# with post-route phys_opt the final black-box checkpoint is *_postroute_physopt_bb.dcp
I=$B/slash.runs/impl_1
[ -f $I/top_wrapper_routed_bb.dcp ] || cp -p $I/top_wrapper_postroute_physopt_bb.dcp $I/top_wrapper_routed_bb.dcp

cat > $B/resume_install.py <<'PY'
import subprocess, sys
import slashkit.emit.hw.project_gen as pg
import os
def post_impl(config, action=None):
    if os.environ.get("SKIP_POST_IMPL") == "1":
        for f in ("report_timing_slash_install.txt", "slash.runs/impl_1/static_shell_slash.dcp",
                  "slash.runs/impl_1/static_shell_service_layer.dcp"):
            if not (config.build_dir / f).is_file():
                raise SystemExit(f"SKIP_POST_IMPL=1 but {f} is missing")
        print("SKIP_POST_IMPL=1: reusing build_project.tcl outputs")
        return
    subprocess.run([str(config.vivado_bin), "-mode", "batch", "-nojournal",
                    "-log", str(config.build_dir / "vivado_resume.log"),
                    "-source", str(config.build_dir / "resume_post_impl.tcl")],
                   cwd=str(config.build_dir), check=True)
pg.create_build_project = post_impl
sys.argv[0] = "slashkit"
from slashkit.__main__ import main
main()
PY

( while sleep 30; do
    a=$(awk '/MemAvailable/{print int($2/1048576)}' /proc/meminfo)
    systemctl --user is-active --quiet $unit.scope || { sleep 60; systemctl --user is-active --quiet $unit.scope || exit 0; }
    [ "$a" -lt 30 ] && { echo "WATCHDOG MemAvailable ${a}G, stopping" >> $LOG; systemctl --user stop $unit.scope; exit 0; }
  done ) &

echo "start $(date -u +%FT%TZ)" > $LOG
systemd-run --user --scope -p MemoryMax=100G -p MemorySwapMax=0 --unit=$unit bash -c "
  source /nfs/amd/2025.1/Vivado/settings64.sh; source /nfs/amd/2025.1/Vitis/settings64.sh
  cd $W; export PYTHONPATH=$W/pylib SKIP_POST_IMPL=${SKIP_POST_IMPL:-0}
  python3 $B/resume_install.py install --shell-type service --stage base-shell $IGN --build-dir $B --out-dir $W/pylib/slashkit/resources --jobs $JOBS &&
  python3 -m slashkit install --shell-type service --stage firmware --build-dir $B --out-dir $W/pylib/slashkit/resources --jobs $JOBS
" >> $LOG 2>&1
echo "EXIT $? $(date -u +%FT%TZ)" >> $LOG
