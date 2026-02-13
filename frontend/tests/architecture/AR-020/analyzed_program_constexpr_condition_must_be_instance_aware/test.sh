#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
ANALYZED_HEADER="$ROOT/frontend/src/core/analyzed_program.h"
ANALYZED_BUILDER="$ROOT/frontend/src/pipeline/analyzed_program_builder.cpp"

if ! rg -q "std::function<std::optional<bool>\(int instance_id, ExprPtr\)>[[:space:]]+constexpr_condition;" "$ANALYZED_HEADER"; then
  echo "AnalyzedProgram constexpr_condition contract must include instance_id" >&2
  exit 1
fi

if ! rg -q "out\.constexpr_condition[[:space:]]*= \[&checker\]\(int instance_id, ExprPtr expr\)" "$ANALYZED_BUILDER"; then
  echo "analyzed_program_builder must wire constexpr_condition with instance_id" >&2
  exit 1
fi

echo "ok"
