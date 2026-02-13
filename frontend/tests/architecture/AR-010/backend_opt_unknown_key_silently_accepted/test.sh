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
&^main() -> #i32 {
  0
}
VXEOF

if "$DRIVER" -b c --backend-opt nonsense=1 -o "$OUT" "$SRC" >"$STDOUT_FILE" 2>"$STDERR_FILE"; then
  echo "backend must reject unknown --backend-opt key for selected backend" >&2
  exit 1
fi

echo "ok"
