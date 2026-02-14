#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
TARGET="$ROOT/frontend/src/pipeline/analyzed_program_builder.cpp"

if rg -q 'checker\.constexpr_condition\(' "$TARGET"; then
  echo "AnalyzedProgram constexpr callback must not recompute conditions via TypeChecker" >&2
  exit 1
fi

if ! rg -q 'optimization(->|\.)constexpr_conditions' "$TARGET"; then
  echo "AnalyzedProgram constexpr callback must source decisions from optimizer constexpr facts" >&2
  exit 1
fi

echo "ok"
