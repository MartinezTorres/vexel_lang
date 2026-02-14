#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
VEXEL="$ROOT/build/vexel"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

run_case() {
  local limit="$1"
  local name="$2"
  local src_file="$TMPDIR/$name.vx"
  local out_prefix="$TMPDIR/$name-out"
  local out_file="$out_prefix.vx"

  cat >"$src_file" <<VX
&^main() -> #i32 {
  limit:#i32 = $limit;
  prime:#b[$limit];
  i:#i32 = 0;
  (i < limit)@{
    prime[i] = 1;
    i = i + 1;
  };
  prime[0] = 0;
  prime[1] = 0;
  p:#i32 = 2;
  (p * p < limit)@{
    prime[p] ? {
      k:#i32 = p * p;
      (k < limit)@{
        prime[k] = 0;
        k = k + p;
      };
    };
    p = p + 1;
  };
  sum:#i32 = 0;
  n:#i32 = 2;
  (n < limit)@{
    prime[n] ? { sum = sum + n; };
    n = n + 1;
  };
  sum
}
VX

  local elapsed_sum=0
  local runs=2
  local i start_ns end_ns elapsed_ms
  for ((i=0; i<runs; ++i)); do
    start_ns="$(date +%s%N)"
    "$VEXEL" -b vexel -o "$out_prefix" "$src_file" >/dev/null 2>&1
    end_ns="$(date +%s%N)"
    elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))
    elapsed_sum=$(( elapsed_sum + elapsed_ms ))
  done

  if [[ ! -f "$out_file" ]]; then
    echo "CTE workload '$name' produced no lowered output" >&2
    exit 1
  fi

  if grep -q "@{" "$out_file"; then
    echo "CTE workload '$name' did not collapse loops in lowered output" >&2
    exit 1
  fi

  echo $(( elapsed_sum / runs ))
}

small_ms="$(run_case 3000 sieve_small)"
large_ms="$(run_case 6000 sieve_large)"

if [[ "$small_ms" -le 0 ]]; then
  echo "invalid timing for small sieve case" >&2
  exit 1
fi

ratio_milli=$(( large_ms * 1000 / small_ms ))
if [[ "$ratio_milli" -gt 3400 ]]; then
  echo "sieve scaling too steep: ${small_ms}ms -> ${large_ms}ms (ratio ${ratio_milli}/1000)" >&2
  exit 1
fi

echo "ok"
