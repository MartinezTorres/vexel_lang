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
#Vec2(x:#i32, y:#i32);
&!input() -> #i32;

&^main() -> #i32 {
  v:#Vec2;
  v.x = input();
  v.y = 2;
  v.x + v.y
}
VXEOF

"$DRIVER" -b vexel -o "$OUT" "$SRC" >/dev/null

if ! grep -Eq '^[[:space:]]*#Vec2\(' "$OUT.vx"; then
  echo "missing #Vec2 declaration even though #Vec2 is used in reachable local code" >&2
  exit 1
fi

echo "ok"
