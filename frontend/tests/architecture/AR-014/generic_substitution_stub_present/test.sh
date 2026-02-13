#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
TARGET="$ROOT/frontend/src/type/typechecker_generics.cpp"

if ! rg -q "This is simplified - full implementation would track type variable names" "$TARGET"; then
  echo "expected unresolved generic substitution stub marker not found" >&2
  exit 1
fi

if ! rg -q "std::unordered_map<std::string, TypePtr> type_map;" "$TARGET"; then
  echo "expected generic substitution local type_map declaration not found" >&2
  exit 1
fi

echo "ok"
