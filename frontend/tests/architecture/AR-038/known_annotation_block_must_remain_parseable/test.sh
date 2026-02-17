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
[[nonreentrant]]
&helper() -> #i32 {
  7
}

&^main() -> #i32 {
  helper()
}
VXEOF

"$DRIVER" -b vexel -o "$OUT_BASE" "$SRC" >"$TMP_DIR/stdout.log" 2>"$TMP_DIR/stderr.log"

if ! rg -q '&\^main\(\)[[:space:]]*->[[:space:]]*#i32' "$OUT_VX"; then
  echo "known annotation blocks must parse cleanly and lower successfully" >&2
  cat "$OUT_VX" >&2
  exit 1
fi

echo "ok"
