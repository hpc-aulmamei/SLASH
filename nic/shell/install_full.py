#!/usr/bin/env python3
"""slashkit install with two fixes for the NIC static flow.

    PYTHONPATH=<tree>/pylib install_full.py <slashkit install args>
    NIC_KEEP=<dir>   where to keep copies of the implementation results

- impl_1 runs post-route phys_opt_design (patch_shell.py), which names the
  black-box checkpoint *_postroute_physopt_bb.dcp; slashkit's later stages
  want *_routed_bb.dcp. It is copied under that name.
- "--stage all" wipes and recreates --build-dir, so the implementation
  results are copied to NIC_KEEP right after the project build.
"""
import os
import pathlib
import runpy
import shutil
import sys

import slashkit.emit.hw.project_gen as pg

KEEP = pathlib.Path(os.environ["NIC_KEEP"]) if os.environ.get("NIC_KEEP") else None
_orig = pg.create_build_project


def _cbp(config, action=None):
    _orig(config, action)
    impl = config.build_dir / "slash.runs" / "impl_1"
    src, dst = impl / "top_wrapper_postroute_physopt_bb.dcp", impl / "top_wrapper_routed_bb.dcp"
    if not dst.exists() and src.exists():
        shutil.copy2(src, dst)
        print(f"NIC: copied {src.name} -> {dst.name}", flush=True)
    if KEEP is None:
        return
    KEEP.mkdir(parents=True, exist_ok=True)
    for f in ["top_wrapper_postroute_physopt.dcp", "top_wrapper_routed_bb.dcp", "static_shell_slash.dcp",
              "static_shell_service_layer.dcp", "debug_nets.ltx", "runme.log", "top_wrapper.pdi",
              "post_route_phys_opt_report_timing_summary_0.rpt", "top_wrapper.ncr"]:
        if (impl / f).exists():
            shutil.copy2(impl / f, KEEP / f)
    r = config.build_dir / "report_timing_slash_install.txt"
    if r.exists():
        shutil.copy2(r, KEEP / r.name)
    print(f"NIC: kept impl results in {KEEP}", flush=True)


pg.create_build_project = _cbp
sys.argv = ["slashkit"] + sys.argv[1:]
runpy.run_module("slashkit", run_name="__main__")
