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

import sys
import logging
import re
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple
import xml.etree.ElementTree as ET


@dataclass
class ValueWithPercent:
    value: Optional[int]
    pct: Optional[float]


@dataclass
class UtilRow:
    instance_raw: str
    instance: str
    module: str
    pr_attribute: str

    total_pplocs: ValueWithPercent
    total_luts: ValueWithPercent
    lutrams: ValueWithPercent
    srls: ValueWithPercent
    ffs: ValueWithPercent
    ramb36: ValueWithPercent
    ramb18: ValueWithPercent
    uram: ValueWithPercent
    dsp: ValueWithPercent

    depth: int = 0

    def ramb_equivalent_18k(self) -> int:
        return (self.ramb18.value or 0) + 2 * (self.ramb36.value or 0)


@dataclass
class TreeNode:
    row: UtilRow
    children: List["TreeNode"] = field(default_factory=list)


@dataclass
class ResourceTotals:
    total_pplocs: int = 0
    total_luts: int = 0
    lutrams: int = 0
    srls: int = 0
    ffs: int = 0
    ramb36: int = 0
    ramb18: int = 0
    uram: int = 0
    dsp: int = 0

    def add(self, r: UtilRow) -> None:
        self.total_pplocs += r.total_pplocs.value or 0
        self.total_luts += r.total_luts.value or 0
        self.lutrams += r.lutrams.value or 0
        self.srls += r.srls.value or 0
        self.ffs += r.ffs.value or 0
        self.ramb36 += r.ramb36.value or 0
        self.ramb18 += r.ramb18.value or 0
        self.uram += r.uram.value or 0
        self.dsp += r.dsp.value or 0

    def ramb_equivalent_18k(self) -> int:
        return self.ramb18 + 2 * self.ramb36


CELL_VALUE_PCT = re.compile(
    r"^\s*(?P<val>\d+)\s*(?:\(\s*(?P<pct>\d+(?:\.\d+)?)%\s*\))?\s*$")


def parse_cell_value_and_percent(cell: str) -> ValueWithPercent:
    """! @brief Parse a utilization cell containing a value and optional percent.

    @param cell Raw cell string from the utilization table.
    @return Parsed value and percent (if present).
    """
    cell = (cell or "").strip()
    if not cell or cell == "-":
        return ValueWithPercent(None, None)

    m = CELL_VALUE_PCT.match(cell)
    if not m:
        digits = re.match(r"^\s*(\d+)", cell)
        return ValueWithPercent(int(digits.group(1)), None) if digits else ValueWithPercent(None, None)

    val = int(m.group("val"))
    pct = float(m.group("pct")) if m.group("pct") is not None else None
    return ValueWithPercent(val, pct)


def count_leading_spaces(s: str) -> int:
    """! @brief Count leading spaces in a string.

    @param s Input string.
    @return Number of leading space characters.
    """
    return len(s) - len(s.lstrip(" "))


def is_parenthesized_instance(instance: str) -> bool:
    """! @brief Check whether an instance name is wrapped in parentheses.

    @param instance Instance name string.
    @return True if the instance is parenthesized.
    """
    inst = instance.strip()
    return inst.startswith("(") and inst.endswith(")")


def parse_vivado_hierarchical_utilization_table(text: str) -> List[UtilRow]:
    """! @brief Parse the Vivado hierarchical utilization table into rows.

    @param text Full report file contents.
    @return List of parsed utilization rows.
    """
    lines = text.splitlines()
    rows: List[UtilRow] = []
    in_table = False

    for ln in lines:
        if ln.startswith("|") and "Instance" in ln and "Module" in ln and "Total LUTs" in ln:
            in_table = True
            continue

        if not in_table:
            continue

        if ln.startswith("+") and ln.count("+") > 5:
            continue

        if not ln.startswith("|"):
            if rows:
                break
            continue

        raw_parts = ln.strip("\n").strip("|").split("|")
        if len(raw_parts) < 13:
            continue

        instance_col_raw = raw_parts[0]
        module_col_raw = raw_parts[1]
        pr_attr_col_raw = raw_parts[2]

        depth = count_leading_spaces(instance_col_raw) // 2
        instance = instance_col_raw.strip()
        module = module_col_raw.strip()
        pr_attr = pr_attr_col_raw.strip()

        rows.append(
            UtilRow(
                instance_raw=instance_col_raw,
                instance=instance,
                module=module,
                pr_attribute=pr_attr,
                total_pplocs=parse_cell_value_and_percent(raw_parts[3]),
                total_luts=parse_cell_value_and_percent(raw_parts[4]),
                lutrams=parse_cell_value_and_percent(raw_parts[6]),
                srls=parse_cell_value_and_percent(raw_parts[7]),
                ffs=parse_cell_value_and_percent(raw_parts[8]),
                ramb36=parse_cell_value_and_percent(raw_parts[9]),
                ramb18=parse_cell_value_and_percent(raw_parts[10]),
                uram=parse_cell_value_and_percent(raw_parts[11]),
                dsp=parse_cell_value_and_percent(raw_parts[12]),
                depth=depth,
            )
        )

    return rows


def build_hierarchy_tree(rows: List[UtilRow]) -> Dict[str, TreeNode]:
    """! @brief Build a parent/child hierarchy from flat utilization rows.

    @param rows Utilization rows with depth set.
    @return Map of instance name to tree node.
    """
    nodes_by_instance: Dict[str, TreeNode] = {}
    stack: List[TreeNode] = []

    for r in rows:
        node = TreeNode(row=r)
        nodes_by_instance[r.instance] = node

        while stack and stack[-1].row.depth >= r.depth:
            stack.pop()

        if stack:
            stack[-1].children.append(node)

        stack.append(node)

    return nodes_by_instance


def walk_subtree(node: TreeNode) -> List[TreeNode]:
    """! @brief Walk a subtree in pre-order.

    @param node Root node to walk.
    @return List of nodes in traversal order.
    """
    out: List[TreeNode] = []
    stack = [node]
    while stack:
        n = stack.pop()
        out.append(n)
        stack.extend(reversed(n.children))
    return out


def slash_hidden_cell_patterns() -> List[re.Pattern]:
    """! @brief Return regex patterns for cells hidden in the SLASH report.

    @return List of compiled regex patterns.
    """
    return [
        re.compile(r".*_sc_.*"),        # hbm_sc_01, etc.
        re.compile(r"^smartconnect.*"),  # smartconnect_0, etc.
    ]


def is_hidden_in_slash_report(instance_name: str) -> bool:
    """! @brief Check whether an instance should be hidden in SLASH report.

    @param instance_name Instance name to test.
    @return True if the instance is hidden.
    """
    return any(p.search(instance_name) for p in slash_hidden_cell_patterns())


def pretty_indent_xml(elem: ET.Element, level: int = 0) -> None:
    """! @brief In-place pretty indent for XML output.

    @param elem Root XML element.
    @param level Starting indent level.
    """
    i = "\n" + "  " * level
    if len(elem):
        if not elem.text or not elem.text.strip():
            elem.text = i + "  "
        for child in elem:
            pretty_indent_xml(child, level + 1)
        if not elem.tail or not elem.tail.strip():
            elem.tail = i
    else:
        if level and (not elem.tail or not elem.tail.strip()):
            elem.tail = i


def _set_val_pct(elem: ET.Element, base: str, v: ValueWithPercent) -> None:
    """! @brief Set XML attributes for a value and optional percent.

    @param elem XML element to update.
    @param base Base attribute name.
    @param v Value and percent container.
    """
    elem.set(base, str(v.value or 0))
    if v.pct is not None:
        elem.set(f"{base}_pct", f"{v.pct:.2f}")


def write_totals_attributes_from_row(elem: ET.Element, r: UtilRow) -> None:
    """! @brief Write utilization totals (including pct) from a row.

    @param elem XML element to update.
    @param r Utilization row source.
    """
    _set_val_pct(elem, "total_pplocs", r.total_pplocs)
    _set_val_pct(elem, "total_luts", r.total_luts)
    _set_val_pct(elem, "lutram", r.lutrams)
    _set_val_pct(elem, "srl", r.srls)
    _set_val_pct(elem, "ff", r.ffs)
    _set_val_pct(elem, "ramb36", r.ramb36)
    _set_val_pct(elem, "ramb18", r.ramb18)
    elem.set("ramb", str(r.ramb_equivalent_18k()))
    _set_val_pct(elem, "uram", r.uram)
    _set_val_pct(elem, "dsp", r.dsp)


def write_totals_attributes_from_totals(elem: ET.Element, t: ResourceTotals) -> None:
    """! @brief Write utilization totals from accumulated totals.

    @param elem XML element to update.
    @param t Resource totals source.
    """
    elem.set("total_pplocs", str(t.total_pplocs))
    elem.set("total_luts", str(t.total_luts))
    elem.set("lutram", str(t.lutrams))
    elem.set("srl", str(t.srls))
    elem.set("ff", str(t.ffs))
    elem.set("ramb36", str(t.ramb36))
    elem.set("ramb18", str(t.ramb18))
    elem.set("ramb", str(t.ramb_equivalent_18k()))
    elem.set("uram", str(t.uram))
    elem.set("dsp", str(t.dsp))


def write_cell(elem: ET.Element, r: UtilRow) -> None:
    """! @brief Write per-cell utilization attributes.

    @param elem XML element to update.
    @param r Utilization row source.
    """
    write_totals_attributes_from_row(elem, r)


def create_utilization_xml(nodes: Dict[str, TreeNode]) -> ET.ElementTree:
    """! @brief Create the utilization XML tree from parsed nodes.

    @param nodes Map of instance name to tree node.
    @return XML element tree representing utilization report.
    """
    SERVICE_LAYER = "service_layer"
    SLASH = "slash"

    if SLASH not in nodes:
        raise SystemExit(
            f"Could not find instance '{SLASH}' in the utilization table.")

    root = ET.Element("utilization_report")

    if SERVICE_LAYER in nodes:
        sl_node = nodes[SERVICE_LAYER]
        sl_block = ET.SubElement(root, "block", name="service_layer",
                                 instance=sl_node.row.instance, pr=sl_node.row.pr_attribute)
        write_totals_attributes_from_row(
            ET.SubElement(sl_block, "totals"), sl_node.row)
    else:
        logger.warning(
            "Could not find instance '%s' in utilization table. Emitting zeroed service_layer block.", SERVICE_LAYER)
        sl_block = ET.SubElement(
            root, "block", name="service_layer", instance=SERVICE_LAYER, pr="Reconfigurable")
        write_totals_attributes_from_totals(
            ET.SubElement(sl_block, "totals"), ResourceTotals())

    sh_node = nodes[SLASH]
    sh_block = ET.SubElement(root, "block", name="slash",
                             instance=sh_node.row.instance, pr=sh_node.row.pr_attribute)
    write_totals_attributes_from_row(
        ET.SubElement(sh_block, "totals"), sh_node.row)

    sub = ET.SubElement(sh_block, "subhierarchy")
    visible_cells = ET.SubElement(sub, "cells")
    hidden_cells = ET.SubElement(sub, "slash_logic")

    visible_sum = ResourceTotals()
    hidden_sum = ResourceTotals()

    for child in sh_node.children:
        for n in walk_subtree(child):
            inst = n.row.instance
            if is_parenthesized_instance(inst):
                continue

            if is_hidden_in_slash_report(inst):
                c = ET.SubElement(hidden_cells, "cell", instance=inst,
                                  module=n.row.module, pr=n.row.pr_attribute)
                write_cell(c, n.row)
                hidden_sum.add(n.row)
            else:
                c = ET.SubElement(visible_cells, "cell", instance=inst,
                                  module=n.row.module, pr=n.row.pr_attribute)
                write_cell(c, n.row)
                visible_sum.add(n.row)

    write_totals_attributes_from_totals(
        ET.SubElement(sub, "subhierarchy_sum"), visible_sum)
    write_totals_attributes_from_totals(
        ET.SubElement(sub, "slash_logic_sum"), hidden_sum)

    tree = ET.ElementTree(root)
    pretty_indent_xml(root)
    return tree


def convert_report_utilization_to_xml(report_path: str, out_xml_path: str) -> None:
    """! @brief Convert Vivado utilization report text to XML.

    @param report_path Path to the report_utilization_*.txt input.
    @param out_xml_path Path to the report_utilization_*.xml output.
    """
    logger.info("Converting utilization report to XML")
    logger.info("Utilization report input: %s", report_path)
    logger.info("Utilization report output: %s", out_xml_path)
    with open(report_path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

    rows = parse_vivado_hierarchical_utilization_table(text)
    logger.info("Parsed utilization rows: %d", len(rows))
    nodes = build_hierarchy_tree(rows)
    logger.info("Built utilization node map: %d", len(nodes))
    tree = create_utilization_xml(nodes)
    tree.write(out_xml_path, encoding="utf-8", xml_declaration=True)
    logger.info("Utilization report XML generation complete")


logger = logging.getLogger(__name__)
