#!/usr/bin/env bash
set -euo pipefail

# @rfc: docs/vexel-rfc.md#name-resolution--modules
# @desc: Bundled std::math calls lift over exact-shape arrays in phase 1 by lowering to indexed scalar calls. | std::math phase-1 array lifting lowers to scalar calls

ROOT_DIR="$(cd ../../../../.. && pwd)"
trap 'rm -f out.vx' EXIT
VEXEL_ROOT_DIR="$ROOT_DIR" "$ROOT_DIR/build/vexel" -b vexel -o out input.vx >/dev/null

grep -Fq 'roots = [sqrt(xs[0]), sqrt(xs[1])];' out.vx
grep -Fq 'mods = [fmod(ys[0], 2), fmod(ys[1], 4)];' out.vx
grep -Fq 'flags = [isfinite(roots[0]), isfinite(roots[1])];' out.vx
! grep -Fq 'std::math::sqrt(xs)' out.vx
