#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"

AP="$ROOT/frontend/src/core/analyzed_program.h"
C_BACKEND="$ROOT/backends/c/src"
ML_BACKEND="$ROOT/backends/ext/megalinker/src"
BACKEND_DIRS=("$C_BACKEND")
if [ -d "$ML_BACKEND" ]; then
  BACKEND_DIRS+=("$ML_BACKEND")
fi

if rg -q "try_evaluate" "$AP"; then
  echo "AnalyzedProgram must not expose backend-side evaluator callbacks" >&2
  exit 1
fi

if rg -q "#include \"evaluator.h\"" "${BACKEND_DIRS[@]}"; then
  echo "Backends must not include evaluator ownership headers" >&2
  exit 1
fi

if rg -q "CompileTimeEvaluator|set_symbol_constant|set_value_observer|set_symbol_read_observer|analyzed_program->try_evaluate" \
    "${BACKEND_DIRS[@]}"; then
  echo "Backends must not perform semantic compile-time evaluation" >&2
  exit 1
fi

if ! rg -q "lookup_constexpr_value\\(" "$C_BACKEND/codegen.cpp"; then
  echo "C backend must consume frontend constexpr facts" >&2
  exit 1
fi

if [ -f "$ML_BACKEND/codegen.cpp" ] && ! rg -q "lookup_constexpr_value\\(" "$ML_BACKEND/codegen.cpp"; then
  echo "Megalinker backend must consume frontend constexpr facts" >&2
  exit 1
fi

echo "ok"
