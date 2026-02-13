#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
COMPILER_CPP="$ROOT/frontend/src/cli/compiler.cpp"

count="$(rg -n "run_frontend_pipeline\\(" "$COMPILER_CPP" | wc -l | tr -d '[:space:]')"
if [ "$count" -ne 1 ]; then
  echo "compiler orchestration must have a single owner path (found $count run_frontend_pipeline calls)" >&2
  exit 1
fi

echo "ok"
