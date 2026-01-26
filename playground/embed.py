#!/usr/bin/env python3
import base64
from pathlib import Path
import sys

if len(sys.argv) != 5:
    raise SystemExit("usage: embed.py <template> <js> <wasm> <out>")

template_path, js_path, wasm_path, out_path = sys.argv[1:]

template = Path(template_path).read_text()
js = Path(js_path).read_text()
wasm = Path(wasm_path).read_bytes()
wasm_b64 = base64.b64encode(wasm).decode("ascii")

html = template.replace("/*__VEXEL_JS__*/", js).replace("__VEXEL_WASM_BASE64__", wasm_b64)
Path(out_path).write_text(html)
