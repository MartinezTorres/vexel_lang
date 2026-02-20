#!/usr/bin/env bash
set -euo pipefail

# Backend conformance source of truth:
# validates minimum backend build/registration contract for every discovered backend directory.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DRIVER="$ROOT/build/vexel"

discover_backends() {
  local dirs=()
  local d base
  for d in "$ROOT"/backends/*/ "$ROOT"/backends/ext/*/; do
    [[ -d "$d" ]] || continue
    base="$(basename "$d")"
    if [[ "$base" == "common" || "$base" == "ext" || "$base" == "tests" ]]; then
      continue
    fi
    dirs+=("${d%/}")
  done
  printf '%s\n' "${dirs[@]}" | sort
}

assert_has_target() {
  local makefile="$1"
  local target="$2"
  if ! rg -n "^[[:space:]]*${target}[[:space:]]*:" "$makefile" >/dev/null 2>&1; then
    echo "FAIL: missing target '${target}' in ${makefile}" >&2
    exit 1
  fi
}

check_backend_dir() {
  local dir="$1"
  local name
  name="$(basename "$dir")"
  local rel
  rel="${dir#$ROOT/}"
  local mk="$dir/Makefile"
  local backend_cpp="$dir/src/${name}_backend.cpp"

  [[ -f "$mk" ]] || { echo "FAIL: missing Makefile in $dir" >&2; exit 1; }
  [[ -f "$backend_cpp" ]] || { echo "FAIL: missing $backend_cpp" >&2; exit 1; }

  assert_has_target "$mk" "all"
  assert_has_target "$mk" "test"
  assert_has_target "$mk" "clean"

  if ! rg -n "register_backend_${name}\\s*\\(" "$backend_cpp" >/dev/null 2>&1; then
    echo "FAIL: missing register_backend_${name} in $backend_cpp" >&2
    exit 1
  fi
  if ! rg -n "backend\\.info\\.name\\s*=\\s*\"${name}\"" "$backend_cpp" >/dev/null 2>&1; then
    echo "FAIL: backend name mismatch in $backend_cpp (expected '${name}')" >&2
    exit 1
  fi

  # Backend isolation contract: no direct references to another backend's source tree.
  mapfile -t refs < <(rg -n "backends/(common|c/|ext/[^/]+/)" "$mk" "$dir/src" 2>/dev/null || true)
  local ref
  for ref in "${refs[@]}"; do
    if [[ "$ref" == *"backends/common/"* ]]; then
      echo "FAIL: backend '$name' references removed shared backend path: $ref" >&2
      exit 1
    fi
    if [[ "$ref" == *"$rel/"* ]]; then
      continue
    fi
    echo "FAIL: backend '$name' references another backend path: $ref" >&2
    exit 1
  done

  if rg -n "BackendContext" "$mk" "$dir/src" >/dev/null 2>&1; then
    echo "FAIL: backend '$name' references legacy BackendContext instead of AnalyzedProgram contract" >&2
    exit 1
  fi

  # Backend-owned suite is part of conformance.
  make -s -C "$dir" test

  # Backend must still build in isolation.
  make -s -C "$dir" all
  [[ -x "$DRIVER" ]] || { echo "FAIL: expected unified driver $DRIVER not found/executable" >&2; exit 1; }

  "$DRIVER" -b "$name" -h >/dev/null 2>&1 || {
    echo "FAIL: $DRIVER -b $name -h failed" >&2
    exit 1
  }

  local tmp
  tmp="$(mktemp -d)"
  cat >"$tmp/test.vx" <<'EOF'
&^main() -> #i32 {
    0
}
EOF
  "$DRIVER" -b "$name" -o "$tmp/out" "$tmp/test.vx" >/dev/null 2>&1 || {
    echo "FAIL: $DRIVER failed to compile minimal program with backend '$name'" >&2
    rm -rf "$tmp"
    exit 1
  }
  rm -rf "$tmp"
}

main() {
  mapfile -t backends < <(discover_backends)
  if [[ "${#backends[@]}" -eq 0 ]]; then
    echo "No discovered backends for conformance checks."
    exit 0
  fi

  if [[ -d "$ROOT/backends/common" ]]; then
    echo "FAIL: backends/common is not allowed; keep codegen backend-owned" >&2
    exit 1
  fi

  make -s -C "$ROOT" driver
  [[ -x "$DRIVER" ]] || { echo "FAIL: expected unified driver $DRIVER not found/executable" >&2; exit 1; }

  for dir in "${backends[@]}"; do
    check_backend_dir "$dir"
  done

  echo "Backend conformance checks passed (${#backends[@]} backends)."
}

main "$@"
