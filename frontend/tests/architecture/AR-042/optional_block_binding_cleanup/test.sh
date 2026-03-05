#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
RESOLVER="$ROOT/frontend/src/resolve/resolver.cpp"
TYPECHECK="$ROOT/frontend/src/type/typechecker_expr_control.cpp"
HELPER="$ROOT/frontend/src/support/binding_cleanup.h"

if ! rg -q "unbind_expr_tree\(bindings, current_instance_id, expr\);" "$RESOLVER"; then
  echo "resolver optional semantic block rollback must purge stale bindings" >&2
  exit 1
fi

if ! rg -q "unbind_expr_tree\(\*bindings, current_instance_id, expr\);" "$TYPECHECK"; then
  echo "typechecker optional semantic block rollback must purge stale bindings" >&2
  exit 1
fi

if ! rg -q "void unbind\(int instance_id, const void\* node\)" "$ROOT/frontend/src/core/bindings.h"; then
  echo "Bindings must expose unbind support for rollback cleanup" >&2
  exit 1
fi

if ! rg -q "inline void unbind_expr_tree\(Bindings& bindings, int instance_id, const ExprPtr& expr\)" "$HELPER"; then
  echo "binding cleanup helper must provide expression subtree unbind traversal" >&2
  exit 1
fi

echo "ok"
