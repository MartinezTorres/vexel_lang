#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
TC_CPP="$ROOT/frontend/src/type/typechecker.cpp"

if rg -q 'constexpr_init\s*=\s*true' "$TC_CPP"; then
  echo "TypeChecker constexpr-init classification must not use hardcoded true shortcuts" >&2
  exit 1
fi

if ! rg -q 'constexpr_init\s*=\s*try_evaluate_constexpr\(stmt->var_init,\s*result\)' "$TC_CPP"; then
  echo "TypeChecker constexpr-init classification must be owned by evaluator query" >&2
  exit 1
fi

echo "ok"
