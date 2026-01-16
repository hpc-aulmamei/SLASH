# emit/hbm_ctx.py
from __future__ import annotations
from collections import defaultdict
from typing import Dict, List, Optional
from core.kernel import KernelInstance
from core.port import PortType
from core.bd_ports import BlockDesignPorts

def _to_int_or_none(v: Optional[object]) -> Optional[int]:
    if v is None:
        return None
    if isinstance(v, int):
        return v
    s = str(v).strip()
    if s == "":
        return None
    try:
        return int(s, 0)
    except ValueError:
        return None

def build_hbm_smartconnect_context(
    instances: Dict[str, KernelInstance],
    bd: BlockDesignPorts,
    *,
    max_si: int = 16,
    base_name: str = "hbm_sc"
) -> dict:
    """
    Instantiate per-HBM-channel SmartConnect ONLY if that channel has ≥1 AXI4FULL writers.
    Otherwise leave the HBM port unused so the generic terminator can handle it.
    """
    # Gather AXI4FULL kernel pins targeting HBM<i>
    by_hbm: Dict[int, List[str]] = defaultdict(list)

    for inst in instances.values():
        mem_sp = inst.params.get("mem_sp", {})
        for k_port, tgt in mem_sp.items():
            # Only care about AXI4FULL ports
            try:
                if inst.kernel.port(k_port).ptype != PortType.AXI4FULL:
                    continue
            except KeyError:
                continue

            dom = str(tgt.get("domain", "")).upper()
            if dom != "HBM":
                continue

            idx = _to_int_or_none(tgt.get("index"))
            if idx is None:
                # HBM requires an index; skip malformed entries
                continue

            by_hbm[idx].append(f"{inst.name}/{k_port}")

    hbm_reduce_nodes: List[dict] = []
    hbm_root_create:  List[dict] = []
    hbm_root_in:      List[dict] = []
    hbm_root_out:     List[dict] = []

    for h_idx in sorted(by_hbm.keys()):
        sources = by_hbm[h_idx]
        if not sources:
            continue

        # Destination BD port name (HBM_AXI_XX)
        dst_bd   = bd.mem("HBM", h_idx)
        dst_port = dst_bd.rtl_name or dst_bd.name

        # Root SC config: 2 clocks (aclk, aclk1), 1 SI, 1 MI
        root_name = f"{base_name}_{h_idx:02d}"
        clk0 = "clk_wizard_0/clk_out1"
        clk1 = "[get_bd_ports static_region_clk]"
        rst  = "proc_sys_reset_0/peripheral_aresetn"

        hbm_root_create.append({
            "name": root_name,
            "idx":  h_idx,
            "clk0": clk0,
            "clk1": clk1,
            "rst":  rst,
        })

        if len(sources) == 1:
            # Single writer → feed root directly
            hbm_root_in.append({
                "src_pin": sources[0],
                "dst_pin": f"{root_name}/S00_AXI",
            })
        else:
            # Reduction tree → last MI feeds root
            level = 0
            current = [{"src": s} for s in sources]
            while len(current) > 1:
                groups = [current[i:i+max_si] for i in range(0, len(current), max_si)]
                next_level = []
                for g_idx, group in enumerate(groups):
                    scn = f"{base_name}_{h_idx:02d}_L{level}_{g_idx}"
                    hbm_reduce_nodes.append({
                        "name": scn,
                        "num_si": len(group),
                        "si": [{"slot": i, "src": g["src"]} for i, g in enumerate(group)],
                        "clk": clk0,
                        "rst": rst,
                    })
                    next_level.append({"src": f"{scn}/M00_AXI"})
                current = next_level
                level += 1

            hbm_root_in.append({
                "src_pin": current[0]["src"],
                "dst_pin": f"{root_name}/S00_AXI",
            })

        # Root MI -> real HBM port
        hbm_root_out.append({
            "src_pin": f"{root_name}/M00_AXI",
            "dst_port": dst_port,
        })

    return {
        "hbm_reduce_nodes": hbm_reduce_nodes,
        "hbm_root_create":  hbm_root_create,
        "hbm_root_in":      hbm_root_in,
        "hbm_root_out":     hbm_root_out,
    }
