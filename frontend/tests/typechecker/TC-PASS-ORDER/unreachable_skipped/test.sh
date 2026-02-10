#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd ../../../../.. && pwd)"

cleanup() {
  rm -f out.c out.h out.analysis.txt
}
trap cleanup EXIT

if ! make -s -C "$ROOT" driver >/tmp/driver_build.out 2>/tmp/driver_build.err; then
  cat /tmp/driver_build.out /tmp/driver_build.err
  exit 1
fi

"$ROOT/build/vexel" -b c -o out input.vx >/dev/null 2>/dev/null

echo "ok"
