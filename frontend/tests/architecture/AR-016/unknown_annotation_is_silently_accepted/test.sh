#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
DRIVER="$ROOT/build/vexel"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

SRC="$TMP_DIR/test.vx"
OUT="$TMP_DIR/out"
STDOUT_FILE="$TMP_DIR/stdout.log"
STDERR_FILE="$TMP_DIR/stderr.log"

cat >"$SRC" <<'VXEOF'
[[definitely_not_a_known_annotation]]
&^main() -> #i32 {
  0
}
VXEOF

if ! "$DRIVER" -b vexel -o "$OUT" "$SRC" >"$STDOUT_FILE" 2>"$STDERR_FILE"; then
  echo "frontend must pass unknown annotations through to the selected backend" >&2
  cat "$STDERR_FILE" >&2
  exit 1
fi

echo "ok"
