# emit/sw_emu/tb_ctx.py
from __future__ import annotations

from pathlib import Path
import json
import re


def infer_sol1_json_from_component_xml(component_xml: Path) -> Path:
    """
    Given:
      .../sol1/impl/ip/component.xml
    Return:
      .../sol1/sol1_data.json
    """
    p = component_xml.resolve()
    # ip -> impl -> sol1
    sol1_dir = p.parents[2]
    sol1_json = sol1_dir / "sol1_data.json"
    if not sol1_json.exists():
        raise FileNotFoundError(f"Cannot find sol1_data.json inferred from {p} -> {sol1_json}")
    return sol1_json


def _norm_stream_type(src: str) -> str:
    # "stream<ap_uint<32>, 0>&" -> "hls::stream<ap_uint<32>>&"
    s = src.strip()
    m = re.match(r"stream\s*<\s*([^,>]+)\s*,\s*[^>]+>\s*&\s*$", s)
    if m:
        return f"hls::stream<{m.group(1).strip()}>&"
    return s


def _is_stream(cpp_t: str) -> bool:
    return "hls::stream" in cpp_t


def _stream_inner(cpp_t: str) -> str:
    m = re.search(r"hls::stream<\s*(.+?)\s*>", cpp_t)
    return m.group(1) if m else "ap_uint<512>"


def _is_ptr(cpp_t: str) -> bool:
    return "*" in cpp_t


def _strip_ref(cpp_t: str) -> tuple[str, bool]:
    t = cpp_t.strip()
    # do not treat hls::stream<...>& as scalar ref
    if t.endswith("&") and not _is_stream(t):
        return t[:-1].strip(), True
    return t, False


def parse_sol1_data(sol1_json: Path) -> dict:
    d = json.loads(sol1_json.read_text())
    top = d["Top"]
    args = []
    for arg_name, info in d["Args"].items():
        idx = int(info["index"])
        src_type = info["srcType"]
        cpp_type = _norm_stream_type(src_type)

        # interface name if present (axis_in/axis_out/m_axi_gmem0)
        iface = None
        for ref in info.get("hwRefs", []):
            if ref.get("type") == "interface":
                iface = ref.get("interface")
                break

        args.append(
            {
                "name": arg_name,
                "index": idx,
                "srcType": src_type,
                "cppType": cpp_type,
                "iface": iface,
            }
        )
    args.sort(key=lambda a: a["index"])
    return {"Top": top, "Args": args}


def build_tb_context(instances: dict, streams: list, kernel_sol1_by_type: dict[str, Path]) -> dict:
    """
    instances: from apply_config_to_instances(), name -> Instance(kernel=KernelType,...)
    streams: list of edges, each with .src_inst .src_port .dst_inst .dst_port (your parser types)
    kernel_sol1_by_type: kernel-type-name -> sol1_data.json Path
    """

    # Load HLS metadata per kernel type
    hls_meta: dict[str, dict] = {}
    for ktype, sol1p in kernel_sol1_by_type.items():
        hls_meta[ktype] = parse_sol1_data(sol1p)

    # Prototypes: generated from Top + Args (since sol1_data.json doesn't store full prototype)
    prototypes = []
    for ktype, meta in hls_meta.items():
        sig = [f'{a["cppType"]} {a["name"]}' for a in meta["Args"]]
        prototypes.append(f'void {meta["Top"]}({", ".join(sig)});')

    # Streams: wire name per stream_connect edge
    wires = []
    endpoint_to_wire: dict[str, str] = {}

    def get_stream_ctype(inst_name: str, iface: str) -> str:
        ktype = instances[inst_name].kernel.name
        meta = hls_meta[ktype]
        a = next((x for x in meta["Args"] if x["iface"] == iface and _is_stream(x["cppType"])), None)
        return _stream_inner(a["cppType"]) if a else "ap_uint<512>"

    for i, e in enumerate(streams):
        wname = f"stream_{i}"
        ctype = get_stream_ctype(e.src_inst, e.src_port)
        wires.append({"name": wname, "ctype": ctype})
        endpoint_to_wire[f"{e.src_inst}.{e.src_port}"] = wname
        endpoint_to_wire[f"{e.dst_inst}.{e.dst_port}"] = wname

    # Variables + function dispatch blocks
    vars_decl = []
    function_calls = []
    ref_vars = []

    for inst_name, inst in instances.items():
        ktype = inst.kernel.name
        meta = hls_meta[ktype]

        # declare per-arg vars (skip streams)
        for a in meta["Args"]:
            cpp_t, is_ref = _strip_ref(a["cppType"])
            vname = f"{inst_name}_{a['name']}"

            if _is_stream(cpp_t):
                continue
            if _is_ptr(cpp_t):
                base = cpp_t.split("*", 1)[0].strip()
                vars_decl.append(f"{base}* {vname}")
            else:
                vars_decl.append(f"{cpp_t} {vname}")
                if is_ref:
                    ref_vars.append(vname)

        decode_blocks = []
        call_args = []

        # IMPORTANT: this matches your old behavior: argN is only for non-stream args
        argN = 0
        for a in meta["Args"]:
            cpp_t = a["cppType"]
            vname = f"{inst_name}_{a['name']}"

            if _is_stream(cpp_t):
                w = endpoint_to_wire.get(f"{inst_name}.{a['iface']}")
                call_args.append(w if w else "/*MISSING_STREAM*/")
                continue

            decode_blocks.append(f'argType = root["args"]["arg{argN}"]["type"].asString();')
            if _is_ptr(cpp_t):
                base = cpp_t.split("*", 1)[0].strip()
                decode_blocks.append('if (argType == "buffer") {')
                decode_blocks.append(f'  std::string bufferName = root["args"]["arg{argN}"]["name"].asString();')
                decode_blocks.append('  if (buffers.find(bufferName) != buffers.end()) {')
                decode_blocks.append(f'    {vname} = static_cast<{base}*>(buffers[bufferName]);')
                decode_blocks.append('  }')
                decode_blocks.append('}')
            else:
                decode_blocks.append('if (argType == "scalar") {')
                decode_blocks.append(f'  assignValue({vname}, root["args"]["arg{argN}"]["value"]);')
                decode_blocks.append('}')

            call_args.append(vname)
            argN += 1

        function_calls.append(
            {
                "inst": inst_name,        # runtime uses instance name ("dma_0")
                "top": meta["Top"],       # actual HLS top ("dma")
                "decode_blocks": decode_blocks,
                "call_args": call_args,
            }
        )

    return {
        "prototypes": prototypes,
        "vars": vars_decl,
        "wires": wires,
        "function_calls": function_calls,
        "ref_vars": ref_vars,
    }
