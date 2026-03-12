# ##################################################################################################
#  The MIT License (MIT)
#  Copyright (c) 2025-2026 Advanced Micro Devices, Inc. All rights reserved.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy of this software
#  and associated documentation files (the "Software"), to deal in the Software without restriction,
#  including without limitation the rights to use, copy, modify, merge, publish, distribute,
#  sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in all copies or
#  substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
# NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
# ##################################################################################################

from __future__ import annotations
from typing import Dict, List
from core.kernel import KernelInstance
from core.regs import AddressBlock
from core.port import BusType


def _align_up(x: int, a: int) -> int:
    return (x + (a - 1)) & ~(a - 1)


def _register_block_for_axilite(inst: KernelInstance, busif: str):
    """
    Try to find a 'register' usage AddressBlock under a MemoryMap that matches the busif.
    Heuristics:
      - memoryMap.name equals busif; use its first 'register' addressBlock
      - otherwise, first 'register' block in any map
      - otherwise, raise a ValueError
    """
    k = inst.kernel
    for mm in k.memory_maps:
        if (not mm.name) or (mm.name.lower() != busif.lower()):
            continue

        for ab in mm.address_blocks:
            if (ab.usage or "").lower() == "register" and ab.range:
                return ab
    
    # Otherwise first register block anywhere
    for mm in k.memory_maps:
        for ab in mm.address_blocks:
            if (ab.usage or "").lower() == "register" and ab.range:
                return ab

    raise ValueError(f"AXI-Lite interface '{busif}' on kernel '{k.name}' has no matching 'register' address block in its memory maps.")


def build_axilite_address_context(
    instances: Dict[str, KernelInstance],
    *,
    addr_space: str = "S_AXILITE_INI",
    base_offset: int = 0x0202_0000_0000,   # your example
    min_align: int = 0x0000_0100           # 256Bx alignment
) -> dict:
    """
    Returns:
      {
        "axilite_addr": [
           { "inst": "...", "busif": "S_AXI_CONTROL", "segment": "<addressBlock.name>",
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
        for p in inst.kernel.ports_of_type(BusType.AXILITE):
            # Decide the address block from memory maps
            ab = _register_block_for_axilite(inst, p.name)
            range = int(ab.range)
            # Hardware address windows should be aligned to their size (and at least min_align)
            align = max(min_align, range)
            next_off = _align_up(next_off, align)

            items.append({
                "inst": iname,
                "busif": p.name,
                "segment": ab.name,
                "offset": next_off,
                "range": range,
                "addr_space": addr_space,
            })
            next_off += _align_up(range, align)

    return {"axilite_addr": items}
