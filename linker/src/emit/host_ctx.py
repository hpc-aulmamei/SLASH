from __future__ import annotations
from typing import Dict, List, Tuple
from collections import defaultdict

from core.kernel import KernelInstance
from core.port import PortType
from core.bd_ports import BlockDesignPorts

def _collect_host_sources(instances: Dict[str, KernelInstance]) -> List[str]:
    """
    Find all AXI4FULL master ports mapped to HOST. Return list of 'inst/port' pin names.
    """
    si: List[str] = []
    for inst in instances.values():
        mem_map = inst.params.get("mem_sp", {})
        for p in inst.kernel.ports_of_type(PortType.AXI4FULL):
            tgt = mem_map.get(p.name)
            if not tgt:
                continue
            if str(tgt.get("domain", "")).upper() == "HOST":
                si.append(f"{inst.name}/{p.name}")
    return si

def _stage_smartconnects(prefix: str, sources: List[str], max_si: int) -> Tuple[List[dict], str]:
    """
    Build a reduction tree of SmartConnects:
      - inputs: list of SI endpoints (inst/port)
      - max_si: SmartConnect SI capacity (e.g., 16)
    Returns:
      (smartconnect_defs, final_sc_name)
    where smartconnect_defs is a list of dicts:
      {
        "name": "host_sc_0",
        "num_si":  N,
        "si_srcs": ["inst/port", ...]  # if leaf level
        "si_up":   ["host_sc_X/M00_AXI", ...]  # if fed by previous stage
      }
    """
    if not sources:
        return [], ""

    level = 0
    leaves = []
    # chunk into groups of max_si
    def chunks(lst, n):
        for i in range(0, len(lst), n):
            yield lst[i:i+n]

    current_inputs = [sources]
    sc_defs: List[dict] = []

    current = sources[:]

    while len(current) > max_si:
        next_level_inputs: List[str] = []
        for idx, group in enumerate(chunks(current, max_si)):
            sc_name = f"{prefix}_lvl{level}_{idx}"
            sc_defs.append({
                "name": sc_name,
                "num_si": len(group),
                "si_srcs": group,   # raw inst/port sources at this level
                "si_up":   None,
            })
            next_level_inputs.append(f"{sc_name}/M00_AXI")
        current = next_level_inputs
        level += 1

    # final toppest SC (or single-level if <= max_si)
    final_sc = f"{prefix}_lvl{level}_0"
    if len(current) == 1 and current[0].count("/") == 1 and current[0].endswith("/M00_AXI"):
        # degenerate case: exactly one upstream SC output, no need for a final SC
        # but to keep TCL simple/regular, we still add a 1-SI node to connect clocks/reset uniformly
        sc_defs.append({
            "name": final_sc,
            "num_si": 1,
            "si_srcs": None,
            "si_up":   current,
        })
    else:
        sc_defs.append({
            "name": final_sc,
            "num_si": len(current),
            "si_srcs": current if current and "/" in current[0] else None,
            "si_up":   None if (current and "/" not in current[0]) else current,
        })

    return sc_defs, final_sc


def build_host_smartconnect_context(
    instances: Dict[str, KernelInstance],
    bd: BlockDesignPorts,
    max_si: int = 16
) -> dict:
    si_sources = _collect_host_sources(instances)
    host_target_rtl = bd.get("HOST").rtl_name  # "QDMA_SLAVE_BRIDGE"

    if not si_sources:
        # Nothing to aggregate → request a terminator
        return {
            "host_sc_defs": [],
            "host_final": "",
            "host_target_rtl": host_target_rtl,
            "host_clk": "aclk1",
            "host_clk_aux": "s_axi_aclk",
            "host_rstn": "ap_rst_n",
            "host_use_two_clks": True,
            "host_need_term": True,
            "host_term_name": "qdma_host_term_0",
        }

    sc_defs, final_sc = _stage_smartconnects("host_sc", si_sources, max_si=max_si)
    return {
        "host_sc_defs": sc_defs,
        "host_final": final_sc,
        "host_target_rtl": host_target_rtl,
        "host_clk": "aclk1",
        "host_clk_aux": "s_axi_aclk",
        "host_rstn": "ap_rst_n",
        "host_use_two_clks": True,
        "host_need_term": False,
        "host_term_name": "qdma_host_term_0",
    }
