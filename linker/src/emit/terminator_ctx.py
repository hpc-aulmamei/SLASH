# emit/terminators_ctx.py
from __future__ import annotations
from typing import Dict, List, Set
import re
from core.port import PortType
from core.bd_ports import BlockDesignPorts, BdPort

_RX_SKIP_TOP = re.compile(r"^(M\d{2}_INI|HBM_VNOC_INI_\d{2})$", re.IGNORECASE)

def _is_bd_port(p: BdPort) -> bool:
    """True if destination is a *BD interface port* (not a NoC/pin path)."""
    return not ((p.rtl_name or "").startswith("/"))

def _want_generic_term(p: BdPort) -> bool:
    """
    Only terminate HBM & VIRT BD ports here.
    Skip DDR/MEM top ports (Mxx_INI, HBM_VNOC_INI_xx) – they get NoC-side terminators.
    """
    if p.ptype != PortType.AXI4FULL:
        return False
    dom = (p.domain or "").upper()
    if dom not in {"HBM", "VIRT"}:
        return False
    if not _is_bd_port(p):
        return False
    rtl = (p.rtl_name or p.name)
    if _RX_SKIP_TOP.match(rtl):
        return False
    return True

def build_axi_terminators_context(
    bd: BlockDesignPorts,
    used_targets: Set[str],
    *,
    base_name: str = "axi_register_slice_term",
) -> dict:
    """
    Plan AXI Register Slices ONLY for unused HBM/VIRT BD ports.
    DDR and MEM are handled by NoC-specific builders.
    """
    terms: List[dict] = []
    seq = 0

    for lst in bd.ports.values():
        for p in lst:
            if not _want_generic_term(p):
                continue
            dst = (p.rtl_name or p.name)
            if dst in used_targets:
                continue
            terms.append({
                "name": f"{base_name}_{seq}",
                "dst": dst,
                "dst_kind": "port",  # generic covers only BD ports
            })
            seq += 1

    return {"axi_terminators": terms}

def build_ddr_noc_terminators(
    used_targets: Set[str],
    *,
    num_ddr: int = 4,
    noc_pin_fmt: str = "/ddr_noc_{index}/S00_AXI",
    base_name: str = "axi_register_slice_ddrterm",
) -> dict:
    """Terminate unused DDR NoC pins."""
    axi_terms: List[dict] = []
    seq = 0
    for i in range(num_ddr):
        dst = noc_pin_fmt.format(index=i)
        if dst in used_targets:
            continue
        axi_terms.append({
            "name": f"{base_name}_{seq}",
            "dst": dst,
            "dst_kind": "pin",
        })
        seq += 1
    return {"axi_terminators": axi_terms}

def build_mem_noc_terminators(
    used_targets: Set[str],
    *,
    num_mem: int = 8,
    noc_pin_fmt: str = "/hbm_vnoc_0{index}/S00_AXI",
    base_name: str = "axi_register_slice_memterm",
) -> dict:
    """Terminate unused MEM (VNOC) NoC pins."""
    axi_terms: List[dict] = []
    seq = 0
    for i in range(num_mem):
        dst = noc_pin_fmt.format(index=i)
        if dst in used_targets:
            continue
        axi_terms.append({
            "name": f"{base_name}_{seq}",
            "dst": dst,
            "dst_kind": "pin",
        })
        seq += 1
    return {"axi_terminators": axi_terms}
