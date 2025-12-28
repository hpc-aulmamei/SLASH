# emit/hbm_ctx.py
from __future__ import annotations
from collections import defaultdict
from typing import Dict, List
from core.kernel import KernelInstance
from core.port import PortType
from core.bd_ports import BlockDesignPorts

def build_hbm_smartconnect_context(
    instances: Dict[str, KernelInstance],
    bd: BlockDesignPorts,
    *,
    max_si: int = 16,
    base_name: str = "sc_hbm",
) -> dict:
    """
    SmartConnect reduction per HBM<i>. If only 1 source -> direct connect.
    If >1, build a tree with nodes having up to 'max_si' SIs, 1 MI.

    Returns keys for the template:
      - hbm_direct:      [{src_pin, dst_port}]
      - hbm_smart_nodes: [{name, num_si, si:[{slot, src}], ...}]
      - hbm_smart_roots: [{sc_name, dst_port}]
    """
    # Gather AXI4FULL kernel pins targeting HBM<i>
    by_hbm: Dict[int, List[str]] = defaultdict(list)
    for inst in instances.values():
        mem_sp = inst.params.get("mem_sp", {})
        for k_port, tgt in mem_sp.items():
            if tgt.get("domain") == "HBM" and tgt.get("index") is not None:
                if inst.kernel.port(k_port).ptype == PortType.AXI4FULL:
                    by_hbm[int(tgt["index"])].append(f"{inst.name}/{k_port}")

    hbm_direct: List[dict] = []
    hbm_smart_nodes: List[dict] = []
    hbm_smart_roots: List[dict] = []

    for h_idx in sorted(by_hbm.keys()):
        dst_bd = bd.mem("HBM", h_idx)
        dst_port = dst_bd.rtl_name or dst_bd.name

        sources = by_hbm[h_idx]
        if len(sources) == 1:
            hbm_direct.append({"src_pin": sources[0], "dst_port": dst_port})
            continue

        # Build reduction tree
        level = 0
        current = [{"src": s} for s in sources]
        root_sc_name = None

        while len(current) > 1:
            groups = [current[i:i + max_si] for i in range(0, len(current), max_si)]
            next_level = []

            for g_idx, group in enumerate(groups):
                sc_name = f"{base_name}_{h_idx:02d}_{level}_{g_idx}"
                node = {
                    "name": sc_name,
                    "num_si": len(group),
                    "si": [{"slot": i, "src": g["src"]} for i, g in enumerate(group)],
                }
                hbm_smart_nodes.append(node)
                # Output of this SC is single MI M00_AXI
                next_level.append({"src": f"{sc_name}/M00_AXI"})
                root_sc_name = sc_name

            current = next_level
            level += 1

        # Root SC connects to HBM port
        if root_sc_name:
            hbm_smart_roots.append({"sc_name": root_sc_name, "dst_port": dst_port})

    return {
        "hbm_direct": hbm_direct,
        "hbm_smart_nodes": hbm_smart_nodes,
        "hbm_smart_roots": hbm_smart_roots,
    }
 