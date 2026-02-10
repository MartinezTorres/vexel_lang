#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
CLI="$ROOT/build/vexel"

if [[ ! -x "$CLI" ]]; then
  if ! VEXEL_ROOT_DIR="$ROOT" make -s -C "$ROOT" driver >/tmp/vexel_driver_build.out 2>/tmp/vexel_driver_build.err; then
    cat /tmp/vexel_driver_build.out /tmp/vexel_driver_build.err >&2
    echo "missing CLI: $CLI" >&2
    exit 1
  fi
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cat >"$tmp/test.vx" <<'VX'
&^main() -> #i32 {
    x = 41;
    x + 1
}
VX

if ! "$CLI" -b vexel -o "$tmp/out" "$tmp/test.vx" >"$tmp/stdout" 2>"$tmp/stderr"; then
  echo "backend vexel failed to compile valid input" >&2
  cat "$tmp/stderr" >&2
  exit 1
fi

if [[ -s "$tmp/stderr" ]]; then
  echo "expected empty stderr for valid program" >&2
  cat "$tmp/stderr" >&2
  exit 1
fi

if [[ ! -f "$tmp/out.vx" ]]; then
  echo "missing lowered output file: $tmp/out.vx" >&2
  exit 1
fi

if ! diff -q "$tmp/stdout" "$tmp/out.vx" >/dev/null 2>&1; then
  echo "stdout and written output diverge" >&2
  exit 1
fi

if ! rg -q "Lowered Vexel module" "$tmp/out.vx"; then
  echo "missing lowered module header" >&2
  exit 1
fi

if ! rg -q "&\^main\(\) -> #i32" "$tmp/out.vx"; then
  echo "missing lowered main signature" >&2
  exit 1
fi

echo "ok"
