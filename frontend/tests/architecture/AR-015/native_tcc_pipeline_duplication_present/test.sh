#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
PIPELINE_ORCH="$ROOT/frontend/src/cli/compiler.cpp"
NATIVE_ORCH="$ROOT/driver/src/native_tcc_runner.cpp"

if rg -q "run_frontend_pipeline\\(" "$NATIVE_ORCH"; then
  echo "native tcc mode must reuse canonical frontend orchestration (no duplicate pipeline call)" >&2
  exit 1
fi

echo "ok"
