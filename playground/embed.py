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
tutorial_manifest_path = repo_root / "examples" / "tutorial" / "manifest.json"


def encode_example_file(path: Path) -> dict:
    rel_path = path.relative_to(repo_root).as_posix()
    raw = path.read_bytes()
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        return {
            "path": rel_path,
            "encoding": "base64",
            "contentBase64": base64.b64encode(raw).decode("ascii"),
        }
    return {
        "path": rel_path,
        "encoding": "utf8",
        "content": text,
    }


example_root = repo_root / "examples"
example_files = []
if example_root.exists():
    for path in sorted(example_root.rglob("*")):
        if not path.is_file():
            continue
        example_files.append(encode_example_file(path))
example_files_json = json.dumps(example_files).replace("</", "<\\/")

tutorial_manifest = []
if tutorial_manifest_path.exists():
    tutorial_manifest = json.loads(tutorial_manifest_path.read_text())
tutorial_manifest_json = json.dumps(tutorial_manifest).replace("</", "<\\/")

html = (
    template.replace("/*__VEXEL_JS__*/", js)
    .replace("__VEXEL_WASM_BASE64__", wasm_b64)
    .replace("__VEXEL_BACKENDS_JSON__", backends_json)
    .replace("__VEXEL_EXAMPLES_JSON__", example_files_json)
    .replace("__VEXEL_TUTORIAL_JSON__", tutorial_manifest_json)
)
Path(out_path).write_text(html)
