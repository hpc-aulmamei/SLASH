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
from dataclasses import dataclass, field
from typing import Dict, Optional

from core.port import PortType


@dataclass(frozen=True)
class Bus:
    """
    Represents a bus interface, including a logical->physical port map.
    """
    name: str
    ptype: PortType
    width: Optional[int] = None
    logical_to_physical: Dict[str, str] = field(default_factory=dict)

    def __post_init__(self):
        if self.ptype in {PortType.CLOCK, PortType.RESET, PortType.INTERRUPT}:
            object.__setattr__(self, "width", 1)

    def physical_port(self, logical: Optional[str] = None) -> Optional[str]:
        """Return a physical port name for the given logical port (or best default)."""
        if not self.logical_to_physical:
            return None
        if logical:
            return self.logical_to_physical.get(logical)
        if len(self.logical_to_physical) == 1:
            return next(iter(self.logical_to_physical.values()))
        for key in ("CLK", "RESET", "RST", "INT", "IRQ"):
            if key in self.logical_to_physical:
                return self.logical_to_physical[key]
        for _, val in self.logical_to_physical.items():
            return val
        return None

    def __repr__(self) -> str:
        return f"<Bus {self.name} ({self.ptype.name}, width={self.width})>"
