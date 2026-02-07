#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd ../../.. && pwd)"

cleanup() {
  rm -f out.c out.h out.lowered.vx out.analysis.txt
}
trap cleanup EXIT

if ! make -s -C "$ROOT" driver >/tmp/driver_build.out 2>/tmp/driver_build.err; then
  cat /tmp/driver_build.out /tmp/driver_build.err
  exit 1
fi

"$ROOT/build/vexel" --backend-opt foo=bar -b c -o out input.vx >/dev/null 2>/dev/null
if [[ ! -f out.c ]]; then
  echo "missing out.c"
  exit 1
fi

set +e
"$ROOT/build/vexel" --backend-opt foo -b c -o out input.vx >bad.out 2>bad.err
status=$?
set -e
if [[ $status -eq 0 ]]; then
  echo "expected backend-opt parse failure"
  exit 1
fi
if ! rg -q "backend-opt expects key=value" bad.err; then
  echo "missing backend-opt error"
  exit 1
fi

set +e
"$ROOT/build/vexel" -o out input.vx >missing_b.out 2>missing_b.err
status=$?
set -e
if [[ $status -eq 0 ]]; then
  echo "expected missing backend failure"
  exit 1
fi
if ! rg -q "Backend must be specified with -b/--backend" missing_b.err; then
  echo "missing required backend error"
  exit 1
fi

set +e
"$ROOT/build/vexel" -b c --unknown-backend-opt -o out input.vx >unknown.out 2>unknown.err
status=$?
set -e
if [[ $status -eq 0 ]]; then
  echo "expected unknown backend option failure"
  exit 1
fi
if ! rg -q "Unknown option: --unknown-backend-opt" unknown.err; then
  echo "missing unknown option error"
  exit 1
fi
if ! rg -q "Backend-specific options \\(c\\):" unknown.out; then
  echo "missing backend usage section"
  exit 1
fi
rm -f bad.out bad.err missing_b.out missing_b.err unknown.out unknown.err

echo "ok"
