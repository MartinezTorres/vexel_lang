#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
VEXEL="$ROOT/build/vexel"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

run_case() {
  local name="$1"
  local max_ms="$2"
  local src="$3"

  local src_file="$TMPDIR/$name.vx"
  local out_prefix="$TMPDIR/$name-out"
  local out_file="$out_prefix.vx"
  local start_ns end_ns elapsed_ms

  printf "%s" "$src" > "$src_file"

  start_ns="$(date +%s%N)"
  "$VEXEL" -b vexel -o "$out_prefix" "$src_file" >/dev/null 2>&1
  end_ns="$(date +%s%N)"
  elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))

  if [[ "$elapsed_ms" -gt "$max_ms" ]]; then
    echo "CTE workload '$name' exceeded budget: ${elapsed_ms}ms > ${max_ms}ms" >&2
    exit 1
  fi

  if [[ ! -f "$out_file" ]]; then
    echo "CTE workload '$name' produced no lowered output" >&2
    exit 1
  fi

  if grep -q "@{" "$out_file"; then
    echo "CTE workload '$name' did not collapse loops in lowered output" >&2
    exit 1
  fi

  if ! rg -q "^&\\^main\\(\\) -> #i32 \\{$" "$out_file"; then
    echo "CTE workload '$name' missing lowered main signature" >&2
    exit 1
  fi
}

run_case "sieve_1000" 12000 "&^main() -> #i32 {
  limit:#i32 = 1000;
  prime:#b[1000];
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
"

run_case "nested_loops" 12000 "&^main() -> #i32 {
  acc:#i32 = 0;
  i:#i32 = 0;
  (i < 220)@{
    j:#i32 = 0;
    (j < 220)@{
      acc = acc + (i * j);
      j = j + 1;
    };
    i = i + 1;
  };
  acc
}
"

run_case "array_pipeline" 12000 "&^main() -> #i32 {
  arr:#i32[256];
  i:#i32 = 0;
  (i < 256)@{
    arr[i] = i * 3 + 7;
    i = i + 1;
  };
  total:#i32 = 0;
  k:#i32 = 0;
  (k < 256)@{
    total = total + arr[k];
    k = k + 1;
  };
  total
}
"

echo "ok"
