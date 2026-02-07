#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd ../../../../.. && pwd)"

check_file() {
  local file="$1"
  local found
  found="$(awk '
    /case Expr::Kind::Iteration:/ || /case Expr::Kind::Repeat:/ {
      in_loop_case = 1
      next
    }
    in_loop_case && /expr->left/ {
      print FILENAME ":" NR ":" $0
      bad = 1
    }
    in_loop_case && (/case Expr::Kind::/ || /default:/) {
      in_loop_case = 0
    }
    END {
      if (bad) exit 1
    }
  ' "$file" || true)"

  if [[ -n "$found" ]]; then
    echo "forbidden loop-field access in $file" >&2
    echo "$found" >&2
    return 1
  fi
}

check_file "$ROOT/frontend/src/analysis/analysis.cpp"
check_file "$ROOT/frontend/src/transform/optimizer.cpp"
check_file "$ROOT/frontend/src/resolve/resolver.cpp"
check_file "$ROOT/frontend/src/resolve/module_loader.cpp"

echo "ok"
