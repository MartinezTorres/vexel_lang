#!/usr/bin/env python3
import base64
import json
import os
from pathlib import Path
import sys

if len(sys.argv) != 5:
    raise SystemExit("usage: embed.py <template> <js> <wasm> <out>")

template_path, js_path, wasm_path, out_path = sys.argv[1:]

template = Path(template_path).read_text()
js = Path(js_path).read_text()
wasm = Path(wasm_path).read_bytes()
wasm_b64 = base64.b64encode(wasm).decode("ascii")

backends_env = os.environ.get("VEXEL_BACKENDS", "").strip()
backends = backends_env.split() if backends_env else ["c"]
backends_json = json.dumps(backends)

playground_dir = Path(__file__).resolve().parent
repo_root = playground_dir.parent
bmp_example_source = (repo_root / "examples" / "bmp_to_matrix.vx").read_text()
bmp_example_source_json = json.dumps(bmp_example_source)
bmp_asset_b64 = base64.b64encode(
    (repo_root / "examples" / "assets" / "random_256x192.bmp").read_bytes()
).decode("ascii")

html = (
    template.replace("/*__VEXEL_JS__*/", js)
    .replace("__VEXEL_WASM_BASE64__", wasm_b64)
    .replace("__VEXEL_BACKENDS_JSON__", backends_json)
    .replace("__EXAMPLE_BMP_TO_MATRIX_SOURCE_JSON__", bmp_example_source_json)
    .replace("__EXAMPLE_BMP_RANDOM_256X192_B64__", bmp_asset_b64)
)
Path(out_path).write_text(html)
