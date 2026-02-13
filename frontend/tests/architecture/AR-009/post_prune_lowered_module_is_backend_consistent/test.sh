#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
DRIVER="$ROOT/build/vexel"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

SRC="$TMP_DIR/test.vx"
LOWERED="$TMP_DIR/lowered"
OUT_C="$TMP_DIR/out_c"
STDOUT_1="$TMP_DIR/stdout_1.log"
STDERR_1="$TMP_DIR/stderr_1.log"
STDOUT_2="$TMP_DIR/stdout_2.log"
STDERR_2="$TMP_DIR/stderr_2.log"

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

if ! "$DRIVER" -b vexel -o "$LOWERED" "$SRC" >"$STDOUT_1" 2>"$STDERR_1"; then
  cat "$STDERR_1" >&2
  echo "failed to produce lowered Vexel module" >&2
  exit 1
fi

if ! "$DRIVER" -b c -o "$OUT_C" "$LOWERED.vx" >"$STDOUT_2" 2>"$STDERR_2"; then
  cat "$STDERR_2" >&2
  echo "expected lowered module to remain backend-consistent after frontend prune" >&2
  exit 1
fi

echo "ok"
