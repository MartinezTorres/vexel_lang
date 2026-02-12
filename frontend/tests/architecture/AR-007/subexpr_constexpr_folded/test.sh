#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
DRIVER="$ROOT/build/vexel"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

SRC="$TMP_DIR/test.vx"
OUT="$TMP_DIR/out"

cat >"$SRC" <<'VXEOF'
&!input() -> #i32;

&^main() -> #i32 {
  x = input();
  y = 1 + 2;
  x + y
}
VXEOF

"$DRIVER" -b vexel -o "$OUT" "$SRC" >/dev/null

if grep -Eq '1[[:space:]]*\+[[:space:]]*2' "$OUT.vx"; then
  echo "sub-expression constant '1 + 2' should be folded before backend emission" >&2
  exit 1
fi

echo "ok"
