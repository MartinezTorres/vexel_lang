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
&^main() -> #i32 {
  (1 + 1 == 2) ? 1 : "x"
}
VXEOF

"$DRIVER" -b vexel -o "$OUT" "$SRC" >/dev/null

if grep -q '"x"' "$OUT.vx"; then
  echo "dead compile-time branch with mismatched type should be pruned from lowered output" >&2
  exit 1
fi

echo "ok"
