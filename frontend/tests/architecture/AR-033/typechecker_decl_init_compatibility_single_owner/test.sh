#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
TC_H="$ROOT/frontend/src/type/typechecker.h"
TC_CPP="$ROOT/frontend/src/type/typechecker.cpp"
TC_CTRL="$ROOT/frontend/src/type/typechecker_expr_control.cpp"

if ! rg -q 'enforce_declared_initializer_type\(' "$TC_H"; then
  echo "TypeChecker must declare a shared declared-initializer compatibility helper" >&2
  exit 1
fi

if ! rg -q 'void TypeChecker::enforce_declared_initializer_type\(' "$TC_CPP"; then
  echo "TypeChecker must define the declared-initializer compatibility helper" >&2
  exit 1
fi

if ! rg -q 'enforce_declared_initializer_type\(type, stmt->var_init, init_type, stmt->location\)' "$TC_CPP"; then
  echo "check_var_decl must route declared-initializer compatibility through the shared helper" >&2
  exit 1
fi

if ! rg -q 'enforce_declared_initializer_type\(var_type, expr->right, rhs_type, expr->location\)' "$TC_CTRL"; then
  echo "declaration-assignment checking must route compatibility through the shared helper" >&2
  exit 1
fi

echo "ok"
