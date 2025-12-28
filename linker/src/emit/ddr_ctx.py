# emit/ddr_ctx.py
from __future__ import annotations
from collections import defaultdict
from typing import Dict, List
from core.kernel import KernelInstance
from core.port import PortType

def build_ddr_smartconnect_context(
    instances: Dict[str, KernelInstance],
    *,
    max_si: int = 16,
    base_name: str = "sc_ddr",
    noc_pin_fmt: str = "/ddr_noc_{index}/S00_AXI",  # absolute path to NoC slave pin
) -> dict:
    """
    Plan SmartConnect reduction per DDR<i>. If only 1 source -> direct connect to
    '/ddr_noc_<i>/S00_AXI'. If >1, build a reduction tree with nodes having up to
    'max_si' SIs and 1 MI.

    Returns keys for the template:
      - ddr_direct:      [{src_pin, dst_pin}]
      - ddr_smart_nodes: [{name, num_si, si:[{slot, src}], ...}]
      - ddr_smart_roots: [{sc_name, dst_pin}]
    """
    # 1) Collect all AXI4FULL kernel pins targeting DDR<i>
    by_ddr: Dict[int, List[str]] = defaultdict(list)
    for inst in instances.values():
        mem_sp = inst.params.get("mem_sp", {})
        for k_port, tgt in mem_sp.items():
            if tgt.get("domain") == "DDR" and tgt.get("index") is not None:
                if inst.kernel.port(k_port).ptype == PortType.AXI4FULL:
                    by_ddr[int(tgt["index"])].append(f"{inst.name}/{k_port}")

    ddr_direct: List[dict] = []
    ddr_smart_nodes: List[dict] = []
    ddr_smart_roots: List[dict] = []

    # 2) For each DDR<i>, either direct connect or build a reduction tree
    for d_idx in sorted(by_ddr.keys()):
        dst_pin = noc_pin_fmt.format(index=d_idx)
        sources = by_ddr[d_idx]

        if len(sources) == 1:
            ddr_direct.append({"src_pin": sources[0], "dst_pin": dst_pin})
            continue

        # Build reduction tree with <= max_si SIs per node
        level = 0
        current = [{"src": s} for s in sources]
        root_sc_name = None

        while len(current) > 1:
            groups = [current[i:i + max_si] for i in range(0, len(current), max_si)]
            next_level = []

            for g_idx, group in enumerate(groups):
                sc_name = f"{base_name}_{d_idx}_{level}_{g_idx}"
                node = {
                    "name": sc_name,
                    "num_si": len(group),
                    "si": [{"slot": i, "src": g["src"]} for i, g in enumerate(group)],
                }
                ddr_smart_nodes.append(node)
                # Output of this SC is M00_AXI
                next_level.append({"src": f"{sc_name}/M00_AXI"})
                root_sc_name = sc_name

            current = next_level
            level += 1

        if root_sc_name:
            ddr_smart_roots.append({"sc_name": root_sc_name, "dst_pin": dst_pin})

    return {
        "ddr_direct": ddr_direct,
        "ddr_smart_nodes": ddr_smart_nodes,
        "ddr_smart_roots": ddr_smart_roots,
    }
