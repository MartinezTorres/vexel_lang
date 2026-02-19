#!/usr/bin/env bash
set -euo pipefail

# Cross-backend ABI contract checks for exported globals.
# Verifies that both C and megalinker backends keep exported global symbols,
# preserve array shape, and elide non-exported dead globals.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DRIVER="$ROOT/build/vexel"

tmp=""
cleanup() {
  if [[ -n "$tmp" && -d "$tmp" ]]; then
    rm -rf "$tmp"
  fi
}
trap cleanup EXIT

tmp="$(mktemp -d)"

cat > "$tmp/abi_globals.vx" <<'EOF'
#Pixel(r:#u8, g:#u8, b:#u8);

^palette:#Pixel[2] = [Pixel(1, 2, 3), Pixel(4, 5, 6)];
^bw = [ [2, 3, 4], [5, 6, 7] ];
drop:#i32 = 99;

&^main() -> #i32 {
  0
}
EOF

if ! "$DRIVER" -b c -o "$tmp/c_out" "$tmp/abi_globals.vx" >/dev/null 2>&1; then
  echo "FAIL: C backend failed exported-global ABI fixture" >&2
  exit 1
fi

if ! rg -q "extern [^;]*vx_palette\\[2\\];" "$tmp/c_out.h"; then
  echo "FAIL: C backend missing exported named-struct global declaration" >&2
  exit 1
fi
if ! rg -q "extern [^;]*vx_bw\\[2\\]\\[3\\];" "$tmp/c_out.h"; then
  echo "FAIL: C backend missing exported nested-array declaration" >&2
  exit 1
fi
if ! rg -q "vx_palette\\[2\\]" "$tmp/c_out.c"; then
  echo "FAIL: C backend missing exported named-struct global definition" >&2
  exit 1
fi
if ! rg -q "vx_bw\\[2\\]\\[3\\]" "$tmp/c_out.c"; then
  echo "FAIL: C backend missing exported nested-array definition" >&2
  exit 1
fi
if rg -q "\\bvx_drop\\b" "$tmp/c_out.h" "$tmp/c_out.c"; then
  echo "FAIL: C backend leaked dead non-exported global into ABI/output" >&2
  exit 1
fi

if ! "$DRIVER" -b megalinker -o "$tmp/ml_out" "$tmp/abi_globals.vx" >/dev/null 2>&1; then
  echo "FAIL: megalinker backend failed exported-global ABI fixture" >&2
  exit 1
fi

if ! rg -q "extern [^;]*vx_palette\\[2\\];" "$tmp/ml_out.h"; then
  echo "FAIL: megalinker backend missing exported named-struct global declaration" >&2
  exit 1
fi
if ! rg -q "extern [^;]*vx_bw\\[2\\]\\[3\\];" "$tmp/ml_out.h"; then
  echo "FAIL: megalinker backend missing exported nested-array declaration" >&2
  exit 1
fi
if [[ ! -f "$tmp/megalinker/rom_vx_palette.c" ]]; then
  echo "FAIL: megalinker backend missing exported named-struct global file" >&2
  exit 1
fi
if [[ ! -f "$tmp/megalinker/rom_vx_bw.c" ]]; then
  echo "FAIL: megalinker backend missing exported nested-array global file" >&2
  exit 1
fi
if ! rg -q "vx_palette\\[2\\]" "$tmp/megalinker/rom_vx_palette.c"; then
  echo "FAIL: megalinker backend missing exported named-struct global definition" >&2
  exit 1
fi
if ! rg -q "vx_bw\\[2\\]\\[3\\]" "$tmp/megalinker/rom_vx_bw.c"; then
  echo "FAIL: megalinker backend missing exported nested-array global definition" >&2
  exit 1
fi
if rg -q "\\bvx_drop\\b" "$tmp/ml_out.h" "$tmp/ml_out__runtime.c" "$tmp/megalinker"/*.c; then
  echo "FAIL: megalinker backend leaked dead non-exported global into ABI/output" >&2
  exit 1
fi

echo "Backend ABI contract checks passed (c, megalinker)."
