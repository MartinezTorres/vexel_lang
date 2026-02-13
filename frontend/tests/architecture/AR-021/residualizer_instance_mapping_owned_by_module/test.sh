#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
RESIDUALIZER_H="$ROOT/frontend/src/transform/residualizer.h"
RESIDUALIZER_CPP="$ROOT/frontend/src/transform/residualizer.cpp"
PIPELINE_CPP="$ROOT/frontend/src/pipeline/frontend_pipeline.cpp"
AST_H="$ROOT/frontend/src/core/ast.h"

if rg -q "run\\(Module& mod, const Program\\*" "$RESIDUALIZER_H"; then
  echo "residualizer API must not depend on Program merge order" >&2
  exit 1
fi

if ! rg -q "std::vector<int> top_level_instance_ids;" "$AST_H"; then
  echo "Module must carry top-level instance IDs for instance-aware passes" >&2
  exit 1
fi

if ! rg -q "residualizer\\.run\\(merged\\)" "$PIPELINE_CPP"; then
  echo "frontend pipeline must call residualizer with merged module only" >&2
  exit 1
fi

if rg -q "checker\\.get_program\\(\\)" "$PIPELINE_CPP"; then
  echo "frontend pipeline must not thread Program through residualizer" >&2
  exit 1
fi

if rg -q "program->instances" "$RESIDUALIZER_CPP"; then
  echo "residualizer implementation must not consume Program instance ordering" >&2
  exit 1
fi

echo "ok"
