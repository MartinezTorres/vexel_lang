#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INDEX_HTML="${ROOT_DIR}/docs/index.html"

if [[ ! -f "${INDEX_HTML}" ]]; then
  echo "docs/index.html not found" >&2
  exit 1
fi

# Landing page must stay source-controlled and must not embed the wasm payload.
if rg -q "VEXEL_WASM_BASE64|createVexelModule|wasmBinaryFile=\"vexel.wasm\"|\\/\\*__VEXEL_JS__\\*\\/" "${INDEX_HTML}"; then
  echo "docs/index.html appears to contain generated playground payload" >&2
  exit 1
fi

if ! rg -q "playground\\.html" "${INDEX_HTML}"; then
  echo "docs/index.html must link to docs/playground.html" >&2
  exit 1
fi

echo "ok"
