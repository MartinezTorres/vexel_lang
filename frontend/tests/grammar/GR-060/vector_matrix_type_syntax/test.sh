#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
DRIVER="$ROOT/build/vexel"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

SRC="$TMP_DIR/test.vx"
OUT_BASE="$TMP_DIR/out"
OUT_VX="$TMP_DIR/out.vx"

cat >"$SRC" <<'VXEOF'
^palette:#v(#u8, 3) = [1, 2, 3];
^weights:#m(#i32, 2, 2) = [[1, 2], [3, 4]];

&^main() -> #i32 {
  (#i32)palette[0] + weights[1][1]
}
VXEOF

"$DRIVER" -b vexel -o "$OUT_BASE" "$SRC" >"$TMP_DIR/stdout.log" 2>"$TMP_DIR/stderr.log"

if ! grep -Eq '^\^palette:[[:space:]]*#u8\[3\][[:space:]]*=' "$OUT_VX"; then
  echo "vector type syntax must lower to a fixed-size array type" >&2
  cat "$OUT_VX" >&2
  exit 1
fi

if ! grep -Eq '^\^weights:[[:space:]]*#i32\[2\]\[2\][[:space:]]*=' "$OUT_VX"; then
  echo "matrix type syntax must lower to nested fixed-size array types" >&2
  cat "$OUT_VX" >&2
  exit 1
fi

echo "ok"
