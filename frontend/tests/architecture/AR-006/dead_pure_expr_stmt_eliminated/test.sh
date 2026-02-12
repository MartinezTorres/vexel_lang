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
  1;
  x
}
VXEOF

"$DRIVER" -b vexel -o "$OUT" "$SRC" >/dev/null

if grep -Eq '^[[:space:]]*1;[[:space:]]*$' "$OUT.vx"; then
  echo "dead pure expression statement '1;' should be removed" >&2
  exit 1
fi

echo "ok"
