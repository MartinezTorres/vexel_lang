#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd ../../../../.. && pwd)"

cleanup() {
  rm -f out out.c out.h out.analysis.txt stdout.txt stderr.txt
}
trap cleanup EXIT

if ! make -s -C "$ROOT" driver >/tmp/driver_build.out 2>/tmp/driver_build.err; then
  cat /tmp/driver_build.out /tmp/driver_build.err
  exit 1
fi

"$ROOT/build/vexel" -b c -o out good.vx >/dev/null 2>/dev/null

set +e
"$ROOT/build/vexel" -b c -o out bad_runtime.vx >stdout.txt 2>stderr.txt
status=$?
set -e
if [[ $status -eq 0 ]]; then
  echo "expected bad_runtime.vx to fail" >&2
  exit 1
fi
if ! rg -q "must be immutable and compile-time constant" stderr.txt; then
  echo "missing exported-global constexpr diagnostic" >&2
  cat stderr.txt >&2
  exit 1
fi

set +e
"$ROOT/build/vexel" -b c -o out bad_missing_init.vx >stdout.txt 2>stderr.txt
status=$?
set -e
if [[ $status -eq 0 ]]; then
  echo "expected bad_missing_init.vx to fail" >&2
  exit 1
fi
if ! rg -q "must have a compile-time initializer" stderr.txt; then
  echo "missing exported-global initializer diagnostic" >&2
  cat stderr.txt >&2
  exit 1
fi

echo "ok"
