#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
AP="$ROOT/frontend/src/core/analyzed_program.h"
REG="$ROOT/frontend/src/support/backend_registry.h"
BUILDER="$ROOT/frontend/src/pipeline/analyzed_program_builder.cpp"

if ! rg -q '^\s*const AnalysisFacts\* analysis\s*=\s*nullptr;' "$AP"; then
  echo "AnalyzedProgram contract must expose analysis facts to backends" >&2
  exit 1
fi

if ! rg -q '^\s*const OptimizationFacts\* optimization\s*=\s*nullptr;' "$AP"; then
  echo "AnalyzedProgram contract must expose optimization facts to backends" >&2
  exit 1
fi

for hook in binding_for resolve_type constexpr_condition lookup_type_symbol; do
  if ! rg -q "std::function<.*${hook}" "$AP"; then
    echo "AnalyzedProgram contract missing required query hook: ${hook}" >&2
    exit 1
  fi
done

if rg -q 'CompileTimeEvaluator|CTEEngine|try_evaluate|set_symbol_constant|set_value_observer|set_symbol_read_observer' "$AP" "$REG" "$BUILDER"; then
  echo "Frontend->backend contract must remain semantics-free (facts + pure queries only)" >&2
  exit 1
fi

if ! rg -q '^\s*const AnalyzedProgram& program;' "$REG"; then
  echo "BackendInput must be anchored on AnalyzedProgram handoff" >&2
  exit 1
fi

if rg -q 'BackendContext' "$REG"; then
  echo "Legacy backend context must not re-enter frontend contract" >&2
  exit 1
fi

echo "ok"
