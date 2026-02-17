#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
AN_RE="$ROOT/frontend/src/analysis/analysis_reentrancy.cpp"
AN_H="$ROOT/frontend/src/analysis/analysis.h"
AN_CPP="$ROOT/frontend/src/analysis/analysis.cpp"

if rg -q '\bfallback\b' "$AN_RE"; then
  echo "Reentrancy analysis must use default-context naming, not fallback naming" >&2
  exit 1
fi

if ! rg -q 'mark_reachable\(const Symbol\* func_sym, AnalysisFacts& facts\)' "$AN_H"; then
  echo "Reachability helper signature must not carry unused Module parameter in header" >&2
  exit 1
fi

if ! rg -q 'void Analyzer::mark_reachable\(const Symbol\* func_sym, AnalysisFacts& facts\)' "$AN_CPP"; then
  echo "Reachability helper signature must not carry unused Module parameter in implementation" >&2
  exit 1
fi

if rg -q '\(void\)instance_scope|\(void\)callee_scope' "$AN_CPP"; then
  echo "Instance scope guards must use explicit maybe_unused ownership instead of void-casts" >&2
  exit 1
fi

echo "ok"
