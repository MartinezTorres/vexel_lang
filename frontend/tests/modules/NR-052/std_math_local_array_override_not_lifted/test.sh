#!/usr/bin/env bash
set -euo pipefail

# @rfc: docs/vexel-rfc.md#name-resolution--modules
# @desc: Project-local std::math array overrides are true overrides; bundled std::math array lifting/folding must not apply to local std/math.vx | local std::math array override is not treated as builtin array math

ROOT_DIR="$(cd ../../../../.. && pwd)"
trap 'rm -f out.vx' EXIT
VEXEL_ROOT_DIR="$ROOT_DIR" "$ROOT_DIR/build/vexel" -b vexel -o out input.vx >/dev/null
if ! rg -q 'ys = sqrt\(xs\);' out.vx; then
  echo "local std::math array override should remain a direct call (no bundled array lifting)" >&2
  cat out.vx >&2
  exit 1
fi
echo "ok"
