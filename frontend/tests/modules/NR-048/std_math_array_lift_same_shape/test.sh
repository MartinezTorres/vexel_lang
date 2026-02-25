#!/usr/bin/env bash
set -euo pipefail

# @rfc: docs/vexel-rfc.md#name-resolution--modules
# @desc: Bundled std::math calls lift over arrays using strict broadcasting by lowering to indexed scalar calls. | std::math array lifting lowers broadcasted calls to scalar calls

ROOT_DIR="$(cd ../../../../.. && pwd)"
trap 'rm -f out.vx' EXIT
VEXEL_ROOT_DIR="$ROOT_DIR" "$ROOT_DIR/build/vexel" -b vexel -o out input.vx >/dev/null

grep -Fq 'roots = [sqrt(xs[0]), sqrt(xs[1])];' out.vx
grep -Fq 'mods = [fmod(ys[0], 2), fmod(ys[1], 4)];' out.vx
grep -Fq 'powed = [pow(xs[0], 2.0), pow(xs[1], 2.0)];' out.vx
grep -Fq 'mods2 = [[fmod(m[0][0], row[0]), fmod(m[0][1], row[1])], [fmod(m[1][0], row[0]), fmod(m[1][1], row[1])]];' out.vx
grep -Fq 'flags = [isfinite(roots[0]), isfinite(roots[1])];' out.vx
! grep -Fq 'std::math::sqrt(xs)' out.vx
