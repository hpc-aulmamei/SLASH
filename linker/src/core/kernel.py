from __future__ import annotations
from dataclasses import dataclass, field
from typing import Dict, Iterable, Optional, List

from core.port import Port, PortType
from core.regs import MemoryMap

@dataclass(frozen=True)
class Kernel:
    """
    Generic kernel/IP *type* definition.
    Contains only port definitions — not instance-specific data.
    """
    name: str
    ports: Dict[str, Port] = field(default_factory=dict)
    vlnv: Optional[str] = None
    memory_maps: List[MemoryMap] = field(default_factory=list)   # NEW

    def port(self, name: str) -> Port:
        """Retrieve a port by name."""
        try:
            return self.ports[name]
        except KeyError as e:
            raise KeyError(f"Kernel '{self.name}' has no port named '{name}'.") from e

    def ports_of_type(self, ptype: PortType) -> Iterable[Port]:
        """Iterate over all ports of a given type."""
        return (p for p in self.ports.values() if p.ptype == ptype)

    @staticmethod
    def from_spec(name: str, spec: Dict[str, Dict]) -> "Kernel":
        """
        Build a Kernel from a spec dictionary, for example:
            spec = {
                "ap_clk":     {"ptype": "CLOCK"},
                "ap_rst_n":   {"ptype": "RESET"},
                "s_axilite":  {"ptype": "AXILITE", "width": 32},
                "m_axi_gmem": {"ptype": "AXI4FULL", "width": 128},
                "axis_in":    {"ptype": "AXIS", "width": 64},
            }
        """
        type_map = {
            "CLOCK": PortType.CLOCK,
            "RESET": PortType.RESET,
            "AXILITE": PortType.AXILITE,
            "AXI4FULL": PortType.AXI4FULL,
            "AXIS": PortType.AXIS,
            "INTERRUPT": PortType.INTERRUPT,
        }

        ports: Dict[str, Port] = {}
        for pname, attrs in spec.items():
            ptype = type_map[attrs["ptype"].strip().upper()]
            width = attrs.get("width")
            ports[pname] = Port(name=pname, ptype=ptype, width=width)

        return Kernel(name=name, ports=ports)


@dataclass
class KernelInstance:
    """
    A specific instance of a Kernel (e.g., 'dma_0').
    Holds a pointer to the Kernel type and optional parameters.
    """
    name: str
    kernel: Kernel
    params: Dict[str, object] = field(default_factory=dict)

    def port(self, name: str) -> Port:
        return self.kernel.port(name)

    def __repr__(self) -> str:
        return f"<KernelInstance {self.name} : {self.kernel.name}>"
