#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd ../../../../.. && pwd)"

cleanup() {
  rm -f out.vx out.c out.h out.analysis.txt
}
trap cleanup EXIT

if ! make -s -C "$ROOT" driver >/tmp/driver_build.out 2>/tmp/driver_build.err; then
  cat /tmp/driver_build.out /tmp/driver_build.err
  exit 1
fi

"$ROOT/build/vexel" -b vexel -o out input.vx >/dev/null 2>/dev/null
if [[ ! -f out.vx ]]; then
  echo "missing out.vx"
  exit 1
fi

if rg -q "@\\{" out.vx; then
  echo "pure loop should not remain in lowered output"
  exit 1
fi

echo "ok"
