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
&^main() -> #i32 {
  a:#v(#i32, 3) = [1, 2, 3];
  b:#v(#i32, 3) = [4, 5, 6];
  c:#v(#i32, 3) = a + b;
  want:#v(#i32, 3) = [5, 7, 9];
  same:#b = c == want;
  ordered:#b = a < b;

  left:#m(#i32, 2, 2) = [[1, 2], [3, 4]];
  right:#m(#i32, 2, 2) = [[5, 6], [7, 8]];
  prod:#m(#i32, 2, 2) = left * right;
  v:#v(#i32, 2) = [2, 3];
  mv:#v(#i32, 2) = left * v;
  vm:#v(#i32, 2) = v * left;
  dot:#i32 = a * b;

  dot + prod[0][0] + prod[1][1] + mv[0] + vm[1] + (#i32)same + (#i32)ordered
}
VXEOF

"$DRIVER" -b vexel -o "$OUT_BASE" "$SRC" >"$TMP_DIR/stdout.log" 2>"$TMP_DIR/stderr.log"

if grep -Eq '#v\(|#m\(' "$OUT_VX"; then
  echo "vector/matrix types must be fully lowered before backend handoff" >&2
  cat "$OUT_VX" >&2
  exit 1
fi

if ! grep -Fq 'c = [a[0] + b[0], a[1] + b[1], a[2] + b[2]];' "$OUT_VX"; then
  echo "vector addition must lower to element-wise scalar additions" >&2
  cat "$OUT_VX" >&2
  exit 1
fi

if ! grep -Fq 'same = c[0] == want[0] && c[1] == want[1] && c[2] == want[2];' "$OUT_VX"; then
  echo "vector equality must lower to element-wise comparisons" >&2
  cat "$OUT_VX" >&2
  exit 1
fi

if ! grep -Fq 'ordered = a[0] < b[0] || a[0] == b[0] && (a[1] < b[1] || a[1] == b[1] && a[2] < b[2]);' "$OUT_VX"; then
  echo "vector ordering must lower to lexicographic scalar comparisons" >&2
  cat "$OUT_VX" >&2
  exit 1
fi

if ! grep -Fq 'dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];' "$OUT_VX"; then
  echo "vector dot product must lower to a scalar reduction" >&2
  cat "$OUT_VX" >&2
  exit 1
fi

if ! grep -Fq 'prod = [[left[0][0] * right[0][0] + left[0][1] * right[1][0], left[0][0] * right[0][1] + left[0][1] * right[1][1]], [left[1][0] * right[0][0] + left[1][1] * right[1][0], left[1][0] * right[0][1] + left[1][1] * right[1][1]]];' "$OUT_VX"; then
  echo "matrix multiplication must lower to scalar multiply-accumulate expressions" >&2
  cat "$OUT_VX" >&2
  exit 1
fi

if ! grep -Fq 'mv = [left[0][0] * v[0] + left[0][1] * v[1], left[1][0] * v[0] + left[1][1] * v[1]];' "$OUT_VX"; then
  echo "matrix-vector multiplication must lower to row dot products" >&2
  cat "$OUT_VX" >&2
  exit 1
fi

if ! grep -Fq 'vm = [v[0] * left[0][0] + v[1] * left[1][0], v[0] * left[0][1] + v[1] * left[1][1]];' "$OUT_VX"; then
  echo "vector-matrix multiplication must lower to column dot products" >&2
  cat "$OUT_VX" >&2
  exit 1
fi

echo "ok"
