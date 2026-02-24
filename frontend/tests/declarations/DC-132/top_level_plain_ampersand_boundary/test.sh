#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd ../../../../.. && pwd)"

cleanup() {
  rm -f out.vx stdout.txt stderr.txt
}
trap cleanup EXIT

if ! make -s -C "$ROOT" driver >/tmp/driver_build.out 2>/tmp/driver_build.err; then
  cat /tmp/driver_build.out /tmp/driver_build.err
  exit 1
fi

set +e
"$ROOT/build/vexel" -b vexel -o out input.vx >stdout.txt 2>stderr.txt
status=$?
set -e

cat stdout.txt
cat stderr.txt >&2

if [[ $status -ne 0 ]]; then
  echo "expected success" >&2
  exit 1
fi

if [[ ! -f out.vx ]]; then
  echo "missing lowered vexel output" >&2
  exit 1
fi

if ! grep -Fq "&^entry() -> #i32" out.vx; then
  echo "missing exported entry declaration in lowered output" >&2
  cat out.vx >&2
  exit 1
fi

# The internal `&main(){}` declaration may be removed by frontend CTE/DCE.
# This test specifically guards the parser boundary: compile success proves the
# semicolon-optional global initializer was split from the following plain `&`
# function declaration instead of being parsed as bitwise `&`.

echo "ok"
