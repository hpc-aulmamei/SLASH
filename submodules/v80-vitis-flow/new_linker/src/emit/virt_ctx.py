# emit/virt_ctx.py
from __future__ import annotations
from collections import defaultdict
from typing import Dict, List
from core.kernel import KernelInstance
from core.port import PortType
from core.bd_ports import BlockDesignPorts

def build_virt_smartconnect_context(
    instances: Dict[str, KernelInstance],
    bd: BlockDesignPorts,
    *,
    max_si: int = 16,
    base_name: str = "sc_virt",
    domain: str = "VIRT",     # logical domain name in bd_ports.txt (VIRT0..VIRT3)
) -> dict:
    """
    Plan SmartConnect fan-in per VIRT<i>. If only 1 source targets a VIRT<i>,
    connect directly to the BD pin from bd_ports; else build a reduction tree
    with SmartConnect nodes (<= max_si SIs, NUM_MI=1, NUM_CLKS=1).

    Returns:
      - virt_direct:      [{src_pin, dst_pin}]
      - virt_smart_nodes: [{name, num_si, si:[{slot, src}], ...}]
      - virt_smart_roots: [{sc_name, dst_pin}]
    """
    # 1) Collect all AXI4FULL masters targeting VIRT<i>
    by_virt: Dict[int, List[str]] = defaultdict(list)
    for inst in instances.values():
        mem_sp = inst.params.get("mem_sp", {})
        for k_port, tgt in mem_sp.items():
            if tgt.get("domain") == domain and tgt.get("index") is not None:
                if inst.kernel.port(k_port).ptype == PortType.AXI4FULL:
                    by_virt[int(tgt["index"])].append(f"{inst.name}/{k_port}")

    virt_direct: List[dict] = []
    virt_smart_nodes: List[dict] = []
    virt_smart_roots: List[dict] = []

    # 2) For each VIRT<i>, either direct connect or reduction tree
    for v_idx in sorted(by_virt.keys()):
        dst_bd = bd.mem(domain, v_idx)          # resolves VIRT<i> from bd_ports.txt
        dst_pin = dst_bd.rtl_name or dst_bd.name

        sources = by_virt[v_idx]
        if len(sources) == 1:
            virt_direct.append({"src_pin": sources[0], "dst_pin": dst_pin})
            continue

        # Reduction tree (max_si SIs per node)
        level = 0
        current = [{"src": s} for s in sources]
        root_sc_name = None

        while len(current) > 1:
            groups = [current[i:i + max_si] for i in range(0, len(current), max_si)]
            next_level: List[dict] = []

            for g_idx, group in enumerate(groups):
                sc_name = f"{base_name}_{v_idx}_{level}_{g_idx}"
                node = {
                    "name": sc_name,
                    "num_si": len(group),
                    "si": [{"slot": i, "src": g["src"]} for i, g in enumerate(group)],
                }
                virt_smart_nodes.append(node)
                next_level.append({"src": f"{sc_name}/M00_AXI"})
                root_sc_name = sc_name

            current = next_level
            level += 1

        if root_sc_name:
            virt_smart_roots.append({"sc_name": root_sc_name, "dst_pin": dst_pin})

    return {
        "virt_direct": virt_direct,
        "virt_smart_nodes": virt_smart_nodes,
        "virt_smart_roots": virt_smart_roots,
    }
