# emit/param_ctx.py
from __future__ import annotations
from typing import Dict, List
from core.kernel import KernelInstance
from core.port import PortType

def _param_name_for_busif(busif: str) -> str:
    # HLS/packager convention: C_<IFNAME>_DATA_WIDTH
    # e.g., M_AXI_GMEM0  -> C_M_AXI_GMEM0_DATA_WIDTH
    return f"C_{busif.upper()}_DATA_WIDTH"

def build_data_width_param_context(
    instances: Dict[str, KernelInstance],
    *,
    domains_of_interest = ("HBM", "VIRT"),
    default_width_by_domain = {"HBM": 256, "VIRT": 512}
) -> dict:
    """
    For every instance and each AXI4FULL port that is mapped (via cfg.sps/defaults)
    to a memory domain in 'domains_of_interest', emit a param set:
        set_property CONFIG.C_<BUSIF>_DATA_WIDTH {<width>} [get_bd_cells <inst>]

    Width resolution order:
      1) Use the port width parsed from component.xml if present (Port.width).
      2) Fallback to default_width_by_domain[domain].
    """
    out: List[dict] = []

    for inst in instances.values():
        # mem_sp filled earlier by apply_config_to_instances()
        mem_map = inst.params.get("mem_sp", {}) or {}
        for busif, tgt in mem_map.items():
            dom = str(tgt.get("domain", "")).upper()
            if dom not in domains_of_interest:
                continue
            # Only for AXI4FULL ports
            try:
                p = inst.kernel.port(busif)
            except KeyError:
                continue
            if p.ptype != PortType.AXI4FULL:
                continue

            # Decide width
            width = p.width if p.width else default_width_by_domain.get(dom)
            if not width:
                # If still unknown, skip silently (or raise if you prefer)
                continue

            out.append({
                "inst": inst.name,
                "param": f"CONFIG.{_param_name_for_busif(busif)}",
                "value": int(width),
            })

    # Optional de-dup if multiple entries set the same param for an inst
    dedup = {}
    for e in out:
        key = (e["inst"], e["param"])
        dedup[key] = e  # last wins
    return {"data_width_params": list(dedup.values())}
