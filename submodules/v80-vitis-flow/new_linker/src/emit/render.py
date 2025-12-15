# emit/render_min.py
from __future__ import annotations
from pathlib import Path
from jinja2 import Environment, FileSystemLoader, StrictUndefined

def render_template(template_dir: str | Path, template_name: str, out_path: str | Path, context: dict) -> None:
    env = Environment(
        loader=FileSystemLoader(str(template_dir)),
        undefined=StrictUndefined,
        trim_blocks=True,
        lstrip_blocks=True,
    )
    env.filters["zip"] = lambda a, b: zip(a, b)
    tmpl = env.get_template(template_name)
    Path(out_path).write_text(tmpl.render(**context), encoding="utf-8")
