from __future__ import annotations
from dataclasses import dataclass
from enum import Enum, auto
from typing import Optional


class PortType(Enum):
    """Enumerates the supported port types in the design."""
    CLOCK = auto()
    RESET = auto()
    AXILITE = auto()
    AXI4FULL = auto()
    AXIS = auto()
    INTERRUPT = auto()  # present, but currently unused


@dataclass(frozen=True)
class Port:
    """
    Represents a single port belonging to a kernel definition.
    For AXI/AXIS, width refers to data width (e.g., 32/64/128).
    For CLOCK, RESET, and INTERRUPT, width is forced to 1.
    """
    name: str
    ptype: PortType
    width: Optional[int] = None

    def __post_init__(self):
        if self.ptype in {PortType.CLOCK, PortType.RESET, PortType.INTERRUPT}:
            object.__setattr__(self, "width", 1)

    def __repr__(self) -> str:
        return f"<Port {self.name} ({self.ptype.name}, width={self.width})>"
