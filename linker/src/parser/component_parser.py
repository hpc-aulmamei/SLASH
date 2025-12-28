# parser/component_parser.py
from __future__ import annotations
from pathlib import Path
from typing import Dict, Optional, List
import xml.etree.ElementTree as ET

from core.port import Port, PortType
from core.kernel import Kernel
from core.regs import MemoryMap, AddressBlock, Register, RegField  # NEW

# Namespaces used in Xilinx IP-XACT component.xml
NS = {
    "spirit": "http://www.spiritconsortium.org/XMLSchema/SPIRIT/1685-2009",
    "xilinx": "http://www.xilinx.com",
}

def _is_slave(busif: ET.Element) -> bool:
    return busif.find("spirit:slave", NS) is not None

def _text(el: Optional[ET.Element]) -> Optional[str]:
    return el.text.strip() if el is not None and el.text is not None else None

def _int(el: Optional[ET.Element]) -> Optional[int]:
    t = _text(el)
    if t is None:
        return None
    try:
        return int(t, 0)  # supports hex/dec
    except ValueError:
        return None

def _param_map(busif: ET.Element) -> Dict[str, str]:
    params = {}
    for p in busif.findall("spirit:parameters/spirit:parameter", NS):
        name = _text(p.find("spirit:name", NS))
        val  = _text(p.find("spirit:value", NS))
        if name and val is not None:
            params[name.strip().upper()] = val.strip()
    return params

def _bus_type(busif: ET.Element) -> tuple[str, str, str, str]:
    b = busif.find("spirit:busType", NS)
    return (
        b.get(f"{{{NS['spirit']}}}vendor", ""),
        b.get(f"{{{NS['spirit']}}}library", ""),
        b.get(f"{{{NS['spirit']}}}name", ""),
        b.get(f"{{{NS['spirit']}}}version", ""),
    )

def _to_port_type(bus_vendor: str, bus_lib: str, bus_name: str,
                  params: Dict[str, str], is_slave: bool) -> Optional[PortType]:
    key = (bus_vendor, bus_lib, bus_name)

    if key == ("xilinx.com", "interface", "axis"):
        return PortType.AXIS

    if key == ("xilinx.com", "interface", "aximm"):
        if params.get("PROTOCOL", "").strip().upper() == "AXI4LITE":
            return PortType.AXILITE
        if is_slave:
            return PortType.AXILITE
        return PortType.AXI4FULL

    if key == ("xilinx.com", "signal", "clock"):
        return PortType.CLOCK
    if key == ("xilinx.com", "signal", "reset"):
        return PortType.RESET
    if key == ("xilinx.com", "signal", "interrupt"):
        return PortType.INTERRUPT

    return None

def _axis_width_from_params(params: Dict[str, str]) -> Optional[int]:
    tbytes = params.get("TDATA_NUM_BYTES")
    if tbytes and tbytes.isdigit():
        return int(tbytes) * 8
    return None

def _aximm_width_from_params(params: Dict[str, str]) -> Optional[int]:
    dw = params.get("DATA_WIDTH")
    if dw and dw.isdigit():
        return int(dw)
    return None

# ---------- NEW: memory map parsing ----------

def _parse_fields(reg_el: ET.Element) -> List[RegField]:
    fields: List[RegField] = []
    for f in reg_el.findall("spirit:field", NS):
        fields.append(RegField(
            name=_text(f.find("spirit:name", NS)) or "",
            description=_text(f.find("spirit:description", NS)),
            bit_offset=_int(f.find("spirit:bitOffset", NS)) or 0,
            bit_width=_int(f.find("spirit:bitWidth", NS)) or 1,
            access=_text(f.find("spirit:access", NS)),
            modified_write_value=_text(f.find("spirit:modifiedWriteValue", NS)),
            read_action=_text(f.find("spirit:readAction", NS)),
            reset_value=_int(f.find("spirit:reset/spirit:value", NS)),
        ))
    return fields

def _parse_registers(ab_el: ET.Element) -> List[Register]:
    regs: List[Register] = []
    for r in ab_el.findall("spirit:register", NS):
        regs.append(Register(
            name=_text(r.find("spirit:name", NS)) or "",
            display_name=_text(r.find("spirit:displayName", NS)),
            description=_text(r.find("spirit:description", NS)),
            address_offset=_int(r.find("spirit:addressOffset", NS)) or 0,
            size=_int(r.find("spirit:size", NS)) or 32,
            access=_text(r.find("spirit:access", NS)),
            reset_value=_int(r.find("spirit:reset/spirit:value", NS)),
            fields=_parse_fields(r),
        ))
    return regs

def _parse_address_blocks(mm_el: ET.Element) -> List[AddressBlock]:
    blocks: List[AddressBlock] = []
    for ab in mm_el.findall("spirit:addressBlock", NS):
        # Optional named params inside addressBlock/parameters
        obp = None
        ohp = None
        for p in ab.findall("spirit:parameters/spirit:parameter", NS):
            pname = _text(p.find("spirit:name", NS))
            pval  = _text(p.find("spirit:value", NS))
            if pname == "OFFSET_BASE_PARAM":
                obp = pval
            elif pname == "OFFSET_HIGH_PARAM":
                ohp = pval

        blocks.append(AddressBlock(
            name=_text(ab.find("spirit:name", NS)) or "unnamed",
            base_address=_int(ab.find("spirit:baseAddress", NS)) or 0,
            range=_int(ab.find("spirit:range", NS)) or 0,
            width=_int(ab.find("spirit:width", NS)) or 32,
            usage=_text(ab.find("spirit:usage", NS)),
            access=_text(ab.find("spirit:access", NS)),
            offset_base_param=obp,
            offset_high_param=ohp,
            registers=_parse_registers(ab),
        ))
    return blocks

def _parse_memory_maps(root: ET.Element) -> List[MemoryMap]:
    maps: List[MemoryMap] = []
    for mm in root.findall("spirit:memoryMaps/spirit:memoryMap", NS):
        maps.append(MemoryMap(
            name=_text(mm.find("spirit:name", NS)) or "unnamed",
            address_blocks=_parse_address_blocks(mm),
        ))
    return maps

# ---------- main entry ----------

def parse_component_xml(path: str | Path) -> Kernel:
    path = Path(path)
    tree = ET.parse(path)
    root = tree.getroot()

    k_vendor = _text(root.find("spirit:vendor", NS)) or ""
    k_lib    = _text(root.find("spirit:library", NS)) or ""
    k_name   = _text(root.find("spirit:name", NS)) or "unknown"
    k_ver    = _text(root.find("spirit:version", NS)) or ""
    vlnv = f"{k_vendor}:{k_lib}:{k_name}:{k_ver}"

    kernel_name = k_name
    ports: Dict[str, Port] = {}

    for busif in root.findall("spirit:busInterfaces/spirit:busInterface", NS):
        busif_name = _text(busif.find("spirit:name", NS))
        if not busif_name:
            continue
        params = _param_map(busif)
        vendor, lib, bname, _ = _bus_type(busif)
        is_slave = _is_slave(busif)
        ptype = _to_port_type(vendor, lib, bname, params, is_slave)
        if ptype is None:
            continue

        width: Optional[int] = None
        if ptype == PortType.AXIS:
            width = _axis_width_from_params(params)
        elif ptype in (PortType.AXILITE, PortType.AXI4FULL):
            width = _aximm_width_from_params(params)
        else:
            width = 1

        ports[busif_name] = Port(name=busif_name, ptype=ptype, width=width)

    memory_maps = _parse_memory_maps(root)  # NEW

    return Kernel(name=kernel_name, ports=ports, vlnv=vlnv, memory_maps=memory_maps)
