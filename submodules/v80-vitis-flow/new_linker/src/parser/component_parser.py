# parser/component_parser.py
from __future__ import annotations
from pathlib import Path
from typing import Dict, Optional
import xml.etree.ElementTree as ET

from core.port import Port, PortType
from core.kernel import Kernel


# Namespaces used in Xilinx IP-XACT component.xml
NS = {
    "spirit": "http://www.spiritconsortium.org/XMLSchema/SPIRIT/1685-2009",
    "xilinx": "http://www.xilinx.com",
}

def _is_slave(busif: ET.Element) -> bool:
    # In IP-XACT, a busInterface has either <spirit:master/> or <spirit:slave/>
    return busif.find("spirit:slave", NS) is not None

def _text(el: Optional[ET.Element]) -> Optional[str]:
    return el.text if el is not None else None

def _param_map(busif: ET.Element) -> Dict[str, str]:
    """Collect <spirit:parameters> -> {name: value} for a busInterface."""
    params = {}
    for p in busif.findall("spirit:parameters/spirit:parameter", NS):
        name = _text(p.find("spirit:name", NS))
        val  = _text(p.find("spirit:value", NS))
        if name and val is not None:
            params[name.strip().upper()] = val.strip()
    return params

def _bus_type(busif: ET.Element) -> tuple[str, str, str, str]:
    """Return (vendor, library, name, version) from <spirit:busType>."""
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
        # If AXI-MM is declared as AXI4-Lite → AXILITE
        if params.get("PROTOCOL", "").strip().upper() == "AXI4LITE":
            return PortType.AXILITE
        # Your rule: any *slave* AXI-MM → treat as AXI-Lite
        if is_slave:
            return PortType.AXILITE
        # Otherwise treat as full AXI
        return PortType.AXI4FULL

    if key == ("xilinx.com", "signal", "clock"):
        return PortType.CLOCK
    if key == ("xilinx.com", "signal", "reset"):
        return PortType.RESET
    if key == ("xilinx.com", "signal", "interrupt"):
        return PortType.INTERRUPT

    return None


def _axis_width_from_params(params: Dict[str, str]) -> Optional[int]:
    # AXIS usually exposes TDATA width via TDATA_NUM_BYTES
    # e.g., TDATA_NUM_BYTES=8 -> width 64
    tbytes = params.get("TDATA_NUM_BYTES")
    if tbytes and tbytes.isdigit():
        return int(tbytes) * 8
    return None

def _aximm_width_from_params(params: Dict[str, str]) -> Optional[int]:
    # Some HLS/RTL components expose DATA_WIDTH for AXI-Lite; AXI4 data width
    # is not always present in busInterface params. Fall back to None if missing.
    dw = params.get("DATA_WIDTH")
    if dw and dw.isdigit():
        return int(dw)
    return None

def parse_component_xml(path: str | Path) -> Kernel:
    path = Path(path)
    tree = ET.parse(path)
    root = tree.getroot()

    k_vendor = _text(root.find("spirit:vendor", NS)) or ""
    k_lib    = _text(root.find("spirit:library", NS)) or ""
    k_name   = _text(root.find("spirit:name", NS)) or "unknown"
    k_ver    = _text(root.find("spirit:version", NS)) or ""

    vlnv = f"{k_vendor}:{k_lib}:{k_name}:{k_ver}"

    # Use component name as kernel name, e.g., "multi_port_kernel"
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
            continue  # skip unsupported buses

        width: Optional[int] = None
        if ptype == PortType.AXIS:
            width = _axis_width_from_params(params)
        elif ptype in (PortType.AXILITE, PortType.AXI4FULL):
            width = _aximm_width_from_params(params)
        else:
            # CLOCK/RESET/INTERRUPT are scalar
            width = 1

        ports[busif_name] = Port(name=busif_name, ptype=ptype, width=width)

    return Kernel(name=kernel_name, ports=ports, vlnv=vlnv)
