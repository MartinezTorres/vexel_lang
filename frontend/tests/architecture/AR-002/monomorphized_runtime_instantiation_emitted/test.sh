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

&id(x) {
  x
}

&^main() -> #i32 {
  id(input())
}
VXEOF

"$DRIVER" -b vexel -o "$OUT" "$SRC" >/dev/null

if ! grep -Eq 'id_G_i32\(input\(\)\)' "$OUT.vx"; then
  echo "expected lowered call to specialized id_G_i32(input())" >&2
  exit 1
fi

if ! grep -Eq '^[[:space:]]*&id_G_i32\(' "$OUT.vx"; then
  echo "missing lowered declaration/definition for id_G_i32" >&2
  exit 1
fi

echo "ok"
