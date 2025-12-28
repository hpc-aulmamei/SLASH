# emit/addr_ctx.py
from __future__ import annotations
from typing import Dict, List, Optional
from core.kernel import KernelInstance
from core.port import PortType

def _align_up(x: int, a: int) -> int:
    return (x + (a - 1)) & ~(a - 1)

def _range_for_axilite(inst: KernelInstance, busif: str, default_range: int) -> int:
    """
    Try to find a 'register' usage addressBlock under a memoryMap that matches the busif.
    Heuristics:
      - memoryMap.name equals busif (case-insensitive) → use its first 'register' addressBlock.range
      - otherwise, first 'register' block in any map
      - fallback to default_range
    """
    k = inst.kernel
    mmaps = getattr(k, "memory_maps", []) or []

    # Try exact map by name (case-insensitive)
    for mm in mmaps:
        if mm.name and mm.name.lower() == busif.lower():
            for ab in mm.address_blocks:
                if (ab.usage or "").lower() == "register" and ab.range:
                    return int(ab.range)
    # Otherwise first register block anywhere
    for mm in mmaps:
        for ab in mm.address_blocks:
            if (ab.usage or "").lower() == "register" and ab.range:
                return int(ab.range)

    return default_range

def build_axilite_address_context(
    instances: Dict[str, KernelInstance],
    *,
    addr_space: str = "S_AXILITE_INI",
    base_offset: int = 0x0202_0000_0000,   # your example
    min_align: int = 0x0001_0000           # 64KB alignment
) -> dict:
    """
    Returns:
      {
        "axilite_addr": [
           { "inst": "...", "busif": "S_AXI_CONTROL", "segment": "Reg",
             "offset": 0x..., "range": 0x..., "addr_space": "S_AXILITE_INI" },
           ...
        ]
      }
    """
    # Build a stable list (sorted for determinism)
    items: List[dict] = []
    next_off = base_offset

    for iname in sorted(instances.keys()):
        inst = instances[iname]
        # For each AXI-Lite interface on this kernel
        for p in inst.kernel.ports_of_type(PortType.AXILITE):
            # Decide the range from memory maps (or fallback)
            rg = _range_for_axilite(inst, p.name, default_range=min_align)
            # Hardware address windows should be aligned to their size (and at least min_align)
            align = max(min_align, rg)
            next_off = _align_up(next_off, align)

            items.append({
                "inst": iname,
                "busif": p.name,
                "segment": "Reg",           # IP integrator segment for control regs
                "offset": next_off,
                "range": rg,
                "addr_space": addr_space,
            })
            next_off += _align_up(rg, align)

    return {"axilite_addr": items}
