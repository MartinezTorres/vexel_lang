#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
TARGET="$ROOT/frontend/src/transform/evaluator.cpp"
HEADER="$ROOT/frontend/src/transform/evaluator.h"

if rg -q "eval_block_vm\(" "$TARGET" "$HEADER"; then
  echo "compile-time evaluator must not keep a dedicated block VM path" >&2
  exit 1
fi

if rg -q "VmOpKind|vm_steps|back_edge_counts" "$TARGET"; then
  echo "compile-time evaluator must not embed block-VM bytecode machinery" >&2
  exit 1
fi

echo "ok"
