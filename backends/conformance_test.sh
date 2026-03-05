#!/usr/bin/env bash
set -euo pipefail

# Backend conformance source of truth:
# validates minimum backend build/registration contract for every discovered backend directory.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
if [[ "$BUILD_DIR" != /* ]]; then
  BUILD_DIR="$ROOT/$BUILD_DIR"
fi
DRIVER="$BUILD_DIR/vexel"

has_rg() {
  command -v rg >/dev/null 2>&1
}

search_quiet() {
  local pattern="$1"
  shift
  if has_rg; then
    rg -n --no-heading "$pattern" "$@" >/dev/null 2>&1
  else
    grep -R -nHE "$pattern" "$@" >/dev/null 2>&1
  fi
}

search_lines() {
  local pattern="$1"
  shift
  if has_rg; then
    rg -n --no-heading "$pattern" "$@" 2>/dev/null || true
  else
    grep -R -nHE "$pattern" "$@" 2>/dev/null || true
  fi
}

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
  if [[ "${#dirs[@]}" -eq 0 ]]; then
    return
  fi
  printf '%s\n' "${dirs[@]}" | sort
}

assert_has_target() {
  local makefile="$1"
  local target="$2"
  if ! search_quiet "^[[:space:]]*${target}[[:space:]]*:" "$makefile"; then
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

  if ! search_quiet "register_backend_${name}\\s*\\(" "$backend_cpp"; then
    echo "FAIL: missing register_backend_${name} in $backend_cpp" >&2
    exit 1
  fi
  if ! search_quiet "backend\\.info\\.name\\s*=\\s*\"${name}\"" "$backend_cpp"; then
    echo "FAIL: backend name mismatch in $backend_cpp (expected '${name}')" >&2
    exit 1
  fi

  # Backend isolation contract: no direct references to another backend's source tree.
  mapfile -t refs < <(search_lines "backends/(common|c/|vexel/|ext/[^/]+/)" "$mk" "$dir/src")
  local ref
  for ref in "${refs[@]}"; do
    local payload="${ref#*:}"
    payload="${payload#*:}"
    if [[ "$payload" == *"backends/common/"* ]]; then
      echo "FAIL: backend '$name' references removed shared backend path: $ref" >&2
      exit 1
    fi
    if [[ "$payload" == *"backends/${name}/"* || "$payload" == *"backends/ext/${name}/"* ]]; then
      continue
    fi
    if [[ "$payload" == *"backends/"* ]]; then
      echo "FAIL: backend '$name' references another backend path: $ref" >&2
      exit 1
    fi
  done

  if search_quiet "BackendContext" "$mk" "$dir/src"; then
    echo "FAIL: backend '$name' references legacy BackendContext instead of AnalyzedProgram contract" >&2
    exit 1
  fi

  # Backend-owned suite is part of conformance.
  BUILD_DIR="$BUILD_DIR" make -s -C "$dir" test

  # Backend must still build in isolation.
  BUILD_DIR="$BUILD_DIR" make -s -C "$dir" all
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

  declare -A seen_backend_names=()
  for dir in "${backends[@]}"; do
    name="$(basename "$dir")"
    if [[ ! "$name" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
      echo "FAIL: backend name '$name' is not a valid C++ identifier suffix for auto-registration symbols" >&2
      exit 1
    fi
    if [[ -n "${seen_backend_names[$name]:-}" ]]; then
      echo "FAIL: duplicate backend name '$name' discovered:" >&2
      echo "  - ${seen_backend_names[$name]}" >&2
      echo "  - $dir" >&2
      exit 1
    fi
    seen_backend_names[$name]="$dir"
  done

  if [[ -d "$ROOT/backends/common" ]]; then
    echo "FAIL: backends/common is not allowed; keep codegen backend-owned" >&2
    exit 1
  fi

  BUILD_DIR="$BUILD_DIR" make -s -C "$ROOT" driver
  [[ -x "$DRIVER" ]] || { echo "FAIL: expected unified driver $DRIVER not found/executable" >&2; exit 1; }

  for dir in "${backends[@]}"; do
    check_backend_dir "$dir"
  done

  echo "Backend conformance checks passed (${#backends[@]} backends)."
}

main "$@"
