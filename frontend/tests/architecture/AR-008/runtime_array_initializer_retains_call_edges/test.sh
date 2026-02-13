#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
DRIVER="$ROOT/build/vexel"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

SRC="$TMP_DIR/test.vx"
OUT="$TMP_DIR/out"
STDOUT_FILE="$TMP_DIR/stdout.log"
STDERR_FILE="$TMP_DIR/stderr.log"

cat >"$SRC" <<'VXEOF'
&!putchar(c:#u8);

&helper() -> #i32 {
  putchar(65);
  7
}

ARR:#i32[2] = [helper(), 2];

&^main() -> #i32 {
  ARR[0]
}
VXEOF

if ! "$DRIVER" -b c -o "$OUT" "$SRC" >"$STDOUT_FILE" 2>"$STDERR_FILE"; then
  cat "$STDERR_FILE" >&2
  echo "expected compile success for runtime-dependent global array initializer calls" >&2
  exit 1
fi

if ! rg -q "helper" "$OUT.c"; then
  echo "expected emitted C output to retain helper call edge" >&2
  exit 1
fi

echo "ok"
