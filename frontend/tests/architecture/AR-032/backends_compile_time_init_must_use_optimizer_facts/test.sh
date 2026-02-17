#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"

check_backend_file() {
  local file="$1"
  local label="$2"
  local fn

  fn="$(awk '
    /bool CodeGenerator::is_compile_time_init\(StmtPtr stmt\) const \{/ { in_fn=1 }
    in_fn { print }
    in_fn && /^}/ { exit }
  ' "$file")"

  if [[ -z "$fn" ]]; then
    echo "$label must define CodeGenerator::is_compile_time_init" >&2
    exit 1
  fi

  if ! rg -q 'constexpr_inits\.count' <<<"$fn"; then
    echo "$label compile-time init classification must use optimizer constexpr_inits facts" >&2
    exit 1
  fi

  if rg -q 'Expr::Kind::ArrayLiteral|Expr::Kind::Range' <<<"$fn"; then
    echo "$label must not hardcode array/range compile-time-init shortcuts" >&2
    exit 1
  fi
}

check_backend_file "$ROOT/backends/c/src/codegen.cpp" "C backend"
check_backend_file "$ROOT/backends/ext/megalinker/src/codegen.cpp" "Megalinker backend"

echo "ok"
