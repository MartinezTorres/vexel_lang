#!/usr/bin/env bash
set -euo pipefail

# @rfc: docs/vexel-rfc.md#name-resolution--modules
# @desc: Project-local std::math overrides are true overrides; compiler builtin std::math folding must not apply to local std/math.vx | local std::math override is not treated as builtin math

ROOT_DIR="$(cd ../../../../.. && pwd)"
trap 'rm -f out.vx' EXIT
VEXEL_ROOT_DIR="$ROOT_DIR" "$ROOT_DIR/build/vexel" -b vexel -o out input.vx >/dev/null
cat out.vx
