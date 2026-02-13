#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
PIPELINE_ORCH="$ROOT/frontend/src/cli/compiler.cpp"
NATIVE_ORCH="$ROOT/driver/src/native_tcc_runner.cpp"

if ! rg -q "run_frontend_pipeline\\(" "$PIPELINE_ORCH"; then
  echo "frontend orchestrator no longer calls run_frontend_pipeline; audit expectation changed" >&2
  exit 1
fi

if ! rg -q "run_frontend_pipeline\\(" "$NATIVE_ORCH"; then
  echo "expected unresolved native tcc duplicate orchestration path not found" >&2
  exit 1
fi

echo "ok"
