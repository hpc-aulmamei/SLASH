from __future__ import annotations
from dataclasses import dataclass, field
from typing import List

# -----------------------------
# Data structures
# -----------------------------

@dataclass(frozen=True)
class NKSpec:
    kernel_type: str
    count: int
    instance_names: List[str]


@dataclass(frozen=True)
class StreamConnect:
    src_inst: str 
    src_port: str               
    dst_inst: str              
    dst_port: str


@dataclass(frozen=True)
class MemoryTarget:
    domain: str 
    index: int


@dataclass(frozen=True)
class SpMapping:
    inst: str
    port: str
    target: MemoryTarget


@dataclass(frozen=True)
class ClockSpec:
    inst: str
    freq_hz: int


@dataclass
class ConnectivityConfig:
    nk: List[NKSpec] = field(default_factory=list)
    streams: List[StreamConnect] = field(default_factory=list)
    sps: List[SpMapping] = field(default_factory=list)
    clocks: List[ClockSpec] = field(default_factory=list)
