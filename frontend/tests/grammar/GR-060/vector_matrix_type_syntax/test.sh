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

"$DRIVER" -b vexel -o "$OUT_BASE" "$SRC" >"$TMP_DIR/stdout.log" 2>"$TMP_DIR/stderr.log" && {
  echo "legacy #v/#m syntax must be rejected" >&2
  exit 1
}

if ! grep -Fq "Vector/matrix shorthand '#v(...)' is no longer supported" "$TMP_DIR/stderr.log"; then
  echo "expected explicit #v/#m deprecation diagnostic" >&2
  cat "$TMP_DIR/stderr.log" >&2
  exit 1
fi

echo "ok"
