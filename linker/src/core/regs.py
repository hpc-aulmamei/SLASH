# core/regs.py
from __future__ import annotations
from dataclasses import dataclass, field
from typing import List, Optional

@dataclass
class RegField:
    name: str
    description: Optional[str]
    bit_offset: int
    bit_width: int
    access: Optional[str] = None
    modified_write_value: Optional[str] = None
    read_action: Optional[str] = None
    reset_value: Optional[int] = None

@dataclass
class Register:
    name: str
    display_name: Optional[str]
    description: Optional[str]
    address_offset: int
    size: int
    access: Optional[str] = None
    reset_value: Optional[int] = None
    fields: List[RegField] = field(default_factory=list)

@dataclass
class AddressBlock:
    name: str
    base_address: int
    range: int
    width: int
    usage: Optional[str] = None
    access: Optional[str] = None
    offset_base_param: Optional[str] = None
    offset_high_param: Optional[str] = None
    registers: List[Register] = field(default_factory=list)

@dataclass
class MemoryMap:
    name: str
    address_blocks: List[AddressBlock] = field(default_factory=list)
