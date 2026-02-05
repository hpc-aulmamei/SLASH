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
from typing import Dict, List, Optional, Tuple

from core.kernel import KernelInstance
from core.port import BusType
from core.regs import AddressBlock

DEFAULT_CLOCK_HZ = 200_000_000


def resolve_system_map_clock(
    clock_override: Optional[int],
    instances: Dict[str, KernelInstance],
    *,
    default_hz: int = DEFAULT_CLOCK_HZ,
) -> int:
    if clock_override is not None:
        return int(clock_override)
    freqs = sorted(
        {
            int(inst.params.get("clock_hz"))
            for inst in instances.values()
            if inst.params.get("clock_hz") is not None
        }
    )
    if freqs:
        return freqs[0]
    return default_hz


def _format_hex(value: int) -> str:
    if value == 0:
        return "0"
    return hex(value)


def _normalize_access(access: Optional[str]) -> str:
    if not access:
        return ""
    key = access.strip().lower().replace("-", "_")
    return {
        "read_only": "R",
        "readonly": "R",
        "ro": "R",
        "r": "R",
        "write_only": "W",
        "writeonly": "W",
        "wo": "W",
        "w": "W",
        "read_write": "RW",
        "readwrite": "RW",
        "rw": "RW",
    }.get(key, access)


def _select_register_block(kernel, busif: str) -> Optional[AddressBlock]:
    mmaps = getattr(kernel, "memory_maps", []) or []

    for mm in mmaps:
        if mm.name and mm.name.lower() == busif.lower():
            for ab in mm.address_blocks:
                if (ab.usage or "").lower() == "register":
                    return ab
            if mm.address_blocks:
                return mm.address_blocks[0]

    for mm in mmaps:
        for ab in mm.address_blocks:
            if (ab.usage or "").lower() == "register":
                return ab

    for mm in mmaps:
        if mm.address_blocks:
            return mm.address_blocks[0]

    return None


def _coerce_optional_int(v) -> Optional[int]:
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


def _assign_mem_indices(
    instances: Dict[str, KernelInstance],
    *,
    num_mem_ports: int = 8,
) -> Dict[Tuple[str, str], int]:
    buckets: Dict[int, List[Tuple[str, str]]] = {i: [] for i in range(num_mem_ports)}
    rr = 0

    for inst in instances.values():
        mem_sp = inst.params.get("mem_sp", {}) or {}
        for k_port, tgt in mem_sp.items():
            if tgt.get("domain") != "MEM":
                continue
            if inst.kernel.port(k_port).ptype != BusType.AXI4FULL:
                continue
            idx = _coerce_optional_int(tgt.get("index"))
            if idx is not None and not (0 <= idx < num_mem_ports):
                raise ValueError(
                    f"MEM index {idx} out of range (0..{num_mem_ports - 1}) for {inst.name}/{k_port}"
                )
            if idx is None:
                idx = rr % num_mem_ports
                rr += 1
            buckets[idx].append((inst.name, k_port))

    mapping: Dict[Tuple[str, str], int] = {}
    for idx, items in buckets.items():
        for inst_name, port in items:
            mapping[(inst_name, port)] = idx
    return mapping


def _format_target(domain: str, index: Optional[int]) -> str:
    dom = domain.upper()
    if index is None or index == "":
        return dom
    return f"{dom}{index}"


def build_system_map_context(
    instances: Dict[str, KernelInstance],
    axilite_addr: List[dict],
    *,
    clock_hz: int,
    platform: str = "Hardware",
    num_mem_ports: int = 8,
    num_virt: int = 4,
    network: Optional[object] = None,
) -> dict:
    axilite_by_inst: Dict[str, List[dict]] = {}
    for entry in axilite_addr:
        axilite_by_inst.setdefault(entry["inst"], []).append(entry)

    mem_indices = _assign_mem_indices(instances, num_mem_ports=num_mem_ports)

    kernels: List[dict] = []
    for inst_name in sorted(instances.keys()):
        inst = instances[inst_name]
        entries = sorted(axilite_by_inst.get(inst_name, []), key=lambda e: e["busif"])
        if not entries:
            continue

        selected = None
        for e in entries:
            if "control" in e["busif"].lower():
                selected = e
                break
        if selected is None:
            for e in entries:
                block = _select_register_block(inst.kernel, e["busif"])
                if block and block.registers:
                    selected = e
                    break
        if selected is None:
            selected = entries[0]

        reg_block = _select_register_block(inst.kernel, selected["busif"])
        registers: List[dict] = []
        if reg_block and reg_block.registers:
            for reg in sorted(reg_block.registers, key=lambda r: r.address_offset):
                registers.append(
                    {
                        "offset": _format_hex(reg.address_offset),
                        "name": reg.name,
                        "access": _normalize_access(reg.access),
                        "description": reg.description or "",
                        "range": str(reg.size),
                    }
                )

        connections: List[dict] = []
        mem_sp = inst.params.get("mem_sp", {}) or {}
        for port in inst.kernel.ports_of_type(BusType.AXI4FULL):
            tgt = mem_sp.get(port.name)
            if not tgt:
                continue
            domain = str(tgt.get("domain", "")).upper()
            idx = _coerce_optional_int(tgt.get("index"))
            if domain == "MEM" and idx is None:
                idx = mem_indices.get((inst.name, port.name))
            connections.append(
                {
                    "port": port.name,
                    "target": _format_target(domain, idx),
                }
            )

        kernels.append(
            {
                "name": inst.name,
                "base_addr": _format_hex(int(selected["offset"])),
                "range": _format_hex(int(selected["range"])),
                "registers": registers,
                "connections": connections,
            }
        )

    enabled_eth = []
    if network is not None:
        enabled_eth = sorted(getattr(network, "enabled_eth", set()) or [])

    service_layer = {
        "eth_enabled": bool(enabled_eth),
        "eth_indices": enabled_eth,
        "virt": [{"index": i, "connection": "unused"} for i in range(num_virt)],
    }

    return {
        "platform": platform,
        "clock_hz": int(clock_hz),
        "kernels": kernels,
        "service_layer": service_layer,
    }
