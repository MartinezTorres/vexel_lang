#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd ../../../../.. && pwd)"

cleanup() {
  rm -f out.c out.h out.analysis.txt stdout.txt stderr.txt
}
trap cleanup EXIT

if ! make -s -C "$ROOT" driver >/tmp/driver_build.out 2>/tmp/driver_build.err; then
  cat /tmp/driver_build.out /tmp/driver_build.err
  exit 1
fi

set +e
"$ROOT/build/vexel" -b c -o out input.vx >stdout.txt 2>stderr.txt
status=$?
set -e

cat stdout.txt
cat stderr.txt >&2

if [[ $status -eq 0 ]]; then
  echo "expected failure" >&2
  exit 1
fi

echo "ok"
