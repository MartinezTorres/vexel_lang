#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd ../../.. && pwd)"

cleanup() {
  rm -f out out.c out.h ok.vx bad_tuple.vx bad_array.vx tuple.err array.err
}
trap cleanup EXIT

if ! make -s -C "$ROOT" driver >/tmp/driver_build.out 2>/tmp/driver_build.err; then
  cat /tmp/driver_build.out /tmp/driver_build.err
  exit 1
fi

cat > ok.vx <<'EOF'
#Pixel(rgb:#u8[3], tag:#i32);
#Sprite(px:#Pixel, xy:#i16[2]);

&!load(id:#i32) -> #Sprite;
&!store(s:#Sprite) -> #i32;

&^main() -> #i32 {
  s:#Sprite = load(1);
  store(s)
}
EOF

"$ROOT/build/vexel" -b c -o out ok.vx >/dev/null 2>/dev/null
if ! rg -q "vx_Sprite vx_load\\(" out.h; then
  echo "missing extern prototype for struct return"
  exit 1
fi
if ! rg -q "int32_t vx_store\\(" out.h; then
  echo "missing extern prototype for struct parameter function"
  exit 1
fi

cat > bad_tuple.vx <<'EOF'
&!bad() -> (#i32, #i32);
&^main() -> #i32 { 0 }
EOF

set +e
"$ROOT/build/vexel" -b c -o out bad_tuple.vx >/dev/null 2>tuple.err
status=$?
set -e
if [[ $status -eq 0 ]]; then
  echo "expected tuple return ABI rejection"
  exit 1
fi
if ! rg -q "cannot use tuple return types at ABI boundaries" tuple.err; then
  echo "missing tuple ABI rejection message"
  exit 1
fi

cat > bad_array.vx <<'EOF'
&!bad(data:#u8[8]) -> #i32;
&^main() -> #i32 { 0 }
EOF

set +e
"$ROOT/build/vexel" -b c -o out bad_array.vx >/dev/null 2>array.err
status=$?
set -e
if [[ $status -eq 0 ]]; then
  echo "expected top-level array ABI rejection"
  exit 1
fi
if ! rg -q "top-level arrays are not allowed at function ABI boundaries" array.err; then
  echo "missing top-level array ABI rejection message"
  exit 1
fi

echo "ok"
