#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"

UTIL_H="$ROOT/frontend/src/core/cte_value_utils.h"
UTIL_CPP="$ROOT/frontend/src/core/cte_value_utils.cpp"
OPT_CPP="$ROOT/frontend/src/transform/optimizer.cpp"
RES_CPP="$ROOT/frontend/src/transform/residualizer.cpp"
AP_CPP="$ROOT/frontend/src/pipeline/analyzed_program_builder.cpp"
EV_CPP="$ROOT/frontend/src/transform/evaluator.cpp"
C_CG="$ROOT/backends/c/src/codegen.cpp"
M_CG="$ROOT/backends/ext/megalinker/src/codegen.cpp"

if ! rg -q 'bool cte_scalar_to_bool\(const CTValue& value, bool& out\)' "$UTIL_H"; then
  echo "CTE scalar-to-bool conversion must be declared in core utility header" >&2
  exit 1
fi

if ! rg -q 'bool cte_scalar_to_bool\(const CTValue& value, bool& out\)' "$UTIL_CPP"; then
  echo "CTE scalar-to-bool conversion must be defined in core utility source" >&2
  exit 1
fi

if rg -q 'static bool ct_scalar_to_bool\(' "$C_CG" "$M_CG"; then
  echo "Backends must not duplicate scalar-to-bool conversion helpers" >&2
  exit 1
fi

if rg -q '(^|[[:space:]])bool scalar_to_bool\(' "$OPT_CPP" "$RES_CPP" "$AP_CPP" "$EV_CPP"; then
  echo "Frontend must not duplicate scalar-to-bool conversion helpers" >&2
  exit 1
fi

if ! rg -q 'cte_scalar_to_bool\(' "$OPT_CPP" "$RES_CPP" "$AP_CPP" "$EV_CPP" "$C_CG" "$M_CG"; then
  echo "Call sites must use core cte_scalar_to_bool helper" >&2
  exit 1
fi

echo "ok"
