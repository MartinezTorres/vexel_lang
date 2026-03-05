#!/usr/bin/env bash
set -euo pipefail

# @rfc: docs/vexel-rfc.md#name-resolution--modules
# @desc: Project-local std::bits overrides are true overrides; compiler builtin std::bits folding must not apply to local std/bits.vx | local std::bits override is not treated as builtin bits

ROOT_DIR="$(cd ../../../../.. && pwd)"
trap 'rm -f out.vx' EXIT
VEXEL_ROOT_DIR="$ROOT_DIR" "$ROOT_DIR/build/vexel" -b vexel -o out input.vx >/dev/null
cat out.vx
