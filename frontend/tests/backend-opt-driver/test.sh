#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd ../../.. && pwd)"

cleanup() {
  rm -f out out.c out.h out.analysis.txt
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
"$ROOT/build/vexel" --run -o out input.vx >missing_b_run.out 2>missing_b_run.err
status=$?
set -e
if [[ $status -eq 0 ]]; then
  echo "expected missing backend failure for --run"
  exit 1
fi
if ! rg -q "Backend must be specified with -b/--backend" missing_b_run.err; then
  echo "missing required backend error for --run"
  exit 1
fi

set +e
"$ROOT/build/vexel" --emit-exe -o out input.vx >missing_b_exe.out 2>missing_b_exe.err
status=$?
set -e
if [[ $status -eq 0 ]]; then
  echo "expected missing backend failure for --emit-exe"
  exit 1
fi
if ! rg -q "Backend must be specified with -b/--backend" missing_b_exe.err; then
  echo "missing required backend error for --emit-exe"
  exit 1
fi

set +e
"$ROOT/build/vexel" -b vexel --run -o out input.vx >wrong_backend_run.out 2>wrong_backend_run.err
status=$?
set -e
if [[ $status -eq 0 ]]; then
  echo "expected backend requirement failure for --run"
  exit 1
fi
if ! rg -q -- "--run/--emit-exe require backend 'c'" wrong_backend_run.err; then
  echo "missing backend-c requirement error for --run"
  exit 1
fi

set +e
"$ROOT/build/vexel" -b vexel --emit-exe -o out input.vx >wrong_backend_exe.out 2>wrong_backend_exe.err
status=$?
set -e
if [[ $status -eq 0 ]]; then
  echo "expected backend requirement failure for --emit-exe"
  exit 1
fi
if ! rg -q -- "--run/--emit-exe require backend 'c'" wrong_backend_exe.err; then
  echo "missing backend-c requirement error for --emit-exe"
  exit 1
fi

"$ROOT/build/vexel" --backend=c -o out input.vx >/dev/null 2>/dev/null
if [[ ! -f out.c ]]; then
  echo "missing out.c when backend passed as --backend=c"
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
rm -f \
  bad.out bad.err \
  missing_b.out missing_b.err \
  missing_b_run.out missing_b_run.err \
  missing_b_exe.out missing_b_exe.err \
  wrong_backend_run.out wrong_backend_run.err \
  wrong_backend_exe.out wrong_backend_exe.err \
  unknown.out unknown.err

help_out=$("$ROOT/build/vexel" --help 2>/dev/null || true)
if printf "%s" "$help_out" | rg -q -- "--run"; then
  "$ROOT/build/vexel" -b c --run -o out input.vx >/dev/null 2>/dev/null

  "$ROOT/build/vexel" -b c --emit-exe -o out input.vx >/dev/null 2>/dev/null
  if [[ ! -x out ]]; then
    echo "missing native executable output"
    exit 1
  fi
else
  set +e
  "$ROOT/build/vexel" -b c --run -o out input.vx >run.out 2>run.err
  status=$?
  set -e
  if [[ $status -eq 0 ]]; then
    echo "expected --run to fail without libtcc support"
    exit 1
  fi
  if ! rg -q "unavailable in this build" run.err; then
    echo "missing libtcc unavailable message"
    exit 1
  fi
  rm -f run.out run.err
fi

echo "ok"
