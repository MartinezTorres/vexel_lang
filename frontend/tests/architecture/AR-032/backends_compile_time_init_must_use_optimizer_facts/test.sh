#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"

check_backend_file() {
  local file="$1"
  local label="$2"

  if rg -q 'bool CodeGenerator::is_compile_time_init\(StmtPtr stmt\) const' "$file"; then
    echo "$label must not define a duplicate is_compile_time_init helper" >&2
    exit 1
  fi

  if ! rg -q 'lookup_constexpr_value\(stmt->var_init, result\)' "$file"; then
    echo "$label var-init folding must use lookup_constexpr_value(stmt->var_init, result)" >&2
    exit 1
  fi

  if rg -q 'constexpr_inits\.count\(' "$file"; then
    echo "$label must not read optimizer constexpr_inits directly from backend codegen" >&2
    exit 1
  fi
}

check_backend_file "$ROOT/backends/c/src/codegen.cpp" "C backend"
if [ -f "$ROOT/backends/ext/megalinker/src/codegen.cpp" ]; then
  check_backend_file "$ROOT/backends/ext/megalinker/src/codegen.cpp" "Megalinker backend"
fi

echo "ok"
