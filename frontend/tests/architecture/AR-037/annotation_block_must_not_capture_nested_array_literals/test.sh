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
&!input() -> #i32;

&^main() -> #i32 {
  matrix = [[input(), 2], [3, 4]];
  matrix[0][0]
}
VXEOF

"$DRIVER" -b vexel -o "$OUT_BASE" "$SRC" >"$TMP_DIR/stdout.log" 2>"$TMP_DIR/stderr.log"

if ! rg -q 'matrix([[:space:]]*:[[:space:]]*#[^=]+)?[[:space:]]*=[[:space:]]*\[\[input\(\),[[:space:]]*2\],[[:space:]]*\[3,[[:space:]]*4\]\];' "$OUT_VX"; then
  echo "nested array literal with identifier-first element must be preserved as array syntax" >&2
  cat "$OUT_VX" >&2
  exit 1
fi

echo "ok"
