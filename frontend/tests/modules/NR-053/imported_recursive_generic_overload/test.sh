#!/usr/bin/env bash
set -euo pipefail

# @rfc: docs/vexel-rfc.md#declarations
# @desc: Imported generic overloads instantiate in their defining module context, so recursive generic calls keep seeing the original overload set. | imported recursive generic overloads instantiate cleanly

ROOT_DIR="$(cd ../../../../.. && pwd)"
trap 'rm -f out.vx' EXIT
VEXEL_ROOT_DIR="$ROOT_DIR" "$ROOT_DIR/build/vexel" -b vexel -o out main.vx >/dev/null
if ! rg -q 'show__ov1_G_array_i32_n2' out.vx; then
  echo "missing nested imported generic instantiation" >&2
  cat out.vx >&2
  exit 1
fi
if ! rg -q 'show__ov1_G_array_array_i32_n2_n2' out.vx; then
  echo "missing outer imported generic instantiation" >&2
  cat out.vx >&2
  exit 1
fi
echo "ok"
