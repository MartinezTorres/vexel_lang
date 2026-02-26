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

cat >"$SRC" <<'EOF'
&!seedi() -> #i32;
&!seedu() -> #u32;
&!tick() -> #b;

&^main() -> #i32 {
  i:#i32 = seedi();
  u:#u32 = seedu();
  t:#b = tick();
  one:#b = 1 == 1;
  zero:#b = 0 == 1;

  ai:#i32[2] = [i, i + 1];
  bi:#i32[2] = [2, 3];
  au:#u32[2] = [u, u + 1];
  bu:#u32[2] = [2, 3];
  ab:#b[2] = [t, zero];
  bb:#b[2] = [one, t];

  radd:#i32[2] = ai .+ bi;
  rsub:#i32[2] = ai .- 1;
  rmul:#i32[2] = ai .* bi;
  rdiv:#i32[2] = ai ./ 2;

  rmod:#u32[2] = au .% bu;
  rand:#u32[2] = au .& 3;
  ror:#u32[2] = au .| bu;
  rxor:#u32[2] = au .^ bu;
  rshl:#u32[2] = au .<< 1;
  rshr:#u32[2] = au .>> 1;

  ceq:#b[2] = ai .== bi;
  cne:#b[2] = ai .!= bi;
  clt:#b[2] = ai .< bi;
  cle:#b[2] = ai .<= bi;
  cgt:#b[2] = ai .> bi;
  cge:#b[2] = ai .>= bi;

  land:#b[2] = ab .&& bb;
  lor:#b[2] = ab .|| one;

  ai .+= i;
  ai .-= 1;
  ai .*= 2;
  ai ./= 2;

  au .%= bu;
  au .&= 3;
  au .|= bu;
  au .^= 1;
  au .<<= 1;
  au .>>= 1;

  ab .&&= bb;
  ab .||= one;

  (((((((((((((((((((((((((((#i32)radd[0]) + ((#i32)rsub[0])) + ((#i32)rmul[0])) + ((#i32)rdiv[0])) + ((#i32)rmod[0])) + ((#i32)rand[0])) + ((#i32)ror[0])) + ((#i32)rxor[0])) + ((#i32)rshl[0])) + ((#i32)rshr[0])) + ((#i32)ceq[0])) + ((#i32)cne[0])) + ((#i32)clt[0])) + ((#i32)cle[0])) + ((#i32)cgt[0])) + ((#i32)cge[0])) + ((#i32)land[0])) + ((#i32)lor[0])) + ((#i32)ai[0])) + ((#i32)au[0])) + ((#i32)ab[0])) + ((#i32)radd[1])) + ((#i32)lor[1])) + ((#i32)ai[1])) + ((#i32)au[1])) + ((#i32)ab[1]))
}
EOF

"$DRIVER" -b vexel -o "$OUT_BASE" "$SRC" >"$TMP_DIR/stdout.log" 2>"$TMP_DIR/stderr.log"

if [[ ! -f "$OUT_VX" ]]; then
  echo "missing lowered Vexel output" >&2
  cat "$TMP_DIR/stderr.log" >&2 || true
  exit 1
fi

for tok in '.+' '.-' '.*' './' '.%' '.&' '.|' '.^' '.<<' '.>>' \
           '.==' '.!=' '.<' '.<=' '.>' '.>=' '.&&' '.||' \
           '.+=' '.-=' '.*=' './=' '.%=' '.&=' '.|=' '.^=' '.<<=' '.>>=' '.&&=' '.||='; do
  if rg -Fq "$tok" "$OUT_VX"; then
    echo "primitive per-element operator token leaked after lowering: $tok" >&2
    cat "$OUT_VX" >&2
    exit 1
  fi
done

echo "ok"
