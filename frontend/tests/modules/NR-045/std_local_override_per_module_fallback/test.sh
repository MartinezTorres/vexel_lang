#!/usr/bin/env bash
set -euo pipefail

# @rfc: docs/vexel-rfc.md#name-resolution--modules
# @desc: Project-local std/<module>.vx overrides bundled std per module, while missing local std modules fall back to bundled std | std override is per-module with fallback

ROOT_DIR="$(cd ../../../../.. && pwd)"
trap 'rm -f out.vx' EXIT
VEXEL_ROOT_DIR="$ROOT_DIR" "$ROOT_DIR/build/vexel" -b vexel -o out input.vx >/dev/null
cat out.vx
