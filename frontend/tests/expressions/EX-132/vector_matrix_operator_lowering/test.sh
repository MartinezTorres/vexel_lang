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
#Pixel(r:#u8, g:#u8, b:#u8);

&(lhs)#Pixel::+(rhs:#Pixel) -> #Pixel {
  #Pixel(lhs.r + rhs.r, lhs.g + rhs.g, lhs.b + rhs.b)
}

&^main() -> #i32 {
  base:#Pixel[2] = [#Pixel(1, 2, 3), #Pixel(4, 5, 6)];
  delta:#Pixel[2] = [#Pixel(10, 20, 30), #Pixel(1, 1, 1)];
  sum:#Pixel[2] = base .+ delta;
  (#i32)sum[0].r + (#i32)sum[1].g
}
VXEOF

"$DRIVER" -b vexel -o "$OUT_BASE" "$SRC" >"$TMP_DIR/stdout.log" 2>"$TMP_DIR/stderr.log"

if grep -Eq '#v\(|#m\(' "$OUT_VX"; then
  echo "legacy vector/matrix syntax should not survive lowering" >&2
  cat "$OUT_VX" >&2
  exit 1
fi

if ! grep -Fq 'sum = [base[0] .+ delta[0], base[1] .+ delta[1]];' "$OUT_VX"; then
  echo "per-element array operators must lower over non-primitive element types" >&2
  cat "$OUT_VX" >&2
  exit 1
fi

if ! grep -Fq '( #i32 ) sum[0].r + ( #i32 ) sum[1].g' "$OUT_VX"; then
  echo "array indexing into named-struct elements must type-check and lower" >&2
  cat "$OUT_VX" >&2
  exit 1
fi

echo "ok"
