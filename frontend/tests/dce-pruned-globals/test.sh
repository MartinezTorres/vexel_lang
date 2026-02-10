#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd ../../.. && pwd)"

cleanup() {
  rm -f out out.vx
}
trap cleanup EXIT

if ! make -s -C "$ROOT" driver >/tmp/driver_build.out 2>/tmp/driver_build.err; then
  cat /tmp/driver_build.out /tmp/driver_build.err
  exit 1
fi

"$ROOT/build/vexel" -b vexel -o out input.vx >/dev/null 2>/dev/null

if [[ ! -f out.vx ]]; then
  echo "missing lowered vexel output"
  exit 1
fi

if rg -q "\\bG_UNUSED\\b|\\bdead\\(" out.vx; then
  echo "frontend DCE did not prune dead declarations before backend handoff"
  exit 1
fi

if ! rg -q "\\bG_USED\\b|\\bkeep\\(" out.vx; then
  echo "missing live declarations in lowered output"
  exit 1
fi

echo "ok"
