# emit/build_ctx.py
from __future__ import annotations
from collections import OrderedDict
from typing import Dict
from core.kernel import KernelInstance
from core.port import PortType

def build_kernel_add_context(instances: Dict[str, KernelInstance]) -> dict:
    """
    Context for your Jinja template:
      - instances: OrderedDict[name -> KernelInstance]
      - clocks:    [{"src_pin": "<inst>/<pin>"} ...]
    """
    ordered = OrderedDict((name, instances[name]) for name in sorted(instances.keys()))

    clocks = []
    resets = []
    for name, inst in ordered.items():
        for p in inst.kernel.ports_of_type(PortType.CLOCK):
            clocks.append({"src_pin": f"{inst.name}/{p.name}"})

        for p in inst.kernel.ports_of_type(PortType.RESET):
            resets.append({"src_pin": f"{inst.name}/{p.name}"})

    return {"instances": ordered, "clocks": clocks, "resets": resets}
