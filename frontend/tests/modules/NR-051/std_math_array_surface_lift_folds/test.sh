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

python3 - "$ROOT" "$SRC" <<'PY'
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
dst = pathlib.Path(sys.argv[2])
std_math = (root / "std" / "math.vx").read_text()

decl_re = re.compile(r"&!([A-Za-z_][A-Za-z0-9_]*)\((.*?)\)\s*->\s*(#[A-Za-z0-9_.]+)\s*;")
decls = []
for m in decl_re.finditer(std_math):
    name = m.group(1)
    params_src = m.group(2).strip()
    ret = m.group(3)
    params = []
    if params_src:
        for part in [p.strip() for p in params_src.split(",")]:
            pm = re.match(r"[A-Za-z_][A-Za-z0-9_]*\s*:\s*(#[A-Za-z0-9_.]+)$", part)
            if not pm:
                raise SystemExit(f"Could not parse std::math parameter: {part!r}")
            params.append(pm.group(1))
    decls.append((name, params, ret))

def scalar_arg(ty: str, name: str, arg_idx: int) -> str:
    is_f32 = ty == "#f32"
    cast = "(#f32)" if is_f32 else ""
    base = "0.5"
    if name.startswith(("log", "log2", "log10")):
        base = "2.0"
    elif name.startswith("sqrt"):
        base = "4.0"
    elif name.startswith(("asin", "acos")):
        base = "0.5"
    elif name.startswith("atan2"):
        base = "0.5" if arg_idx == 0 else "1.0"
    elif name.startswith("fmod"):
        base = "5.5" if arg_idx == 0 else "2.0"
    elif name.startswith(("isnan", "isinf", "isfinite")):
        base = "1.0"
    elif name.startswith(("floor", "ceil", "trunc", "round")):
        base = "1.75"
    elif name.startswith("pow"):
        base = "2.0" if arg_idx == 0 else "3.0"
    return f"{cast}{base}" if cast else base

def array_pair_expr(ty: str, name: str) -> str:
    a0 = scalar_arg(ty, name, 0)
    if name.startswith("log"):
        a1 = f"{'(#f32)' if ty == '#f32' else ''}8.0" if ty == "#f64" else "(#f32)8.0"
    elif name.startswith("sqrt"):
        a1 = f"{'(#f32)' if ty == '#f32' else ''}9.0" if ty == "#f64" else "(#f32)9.0"
    elif name.startswith(("asin", "acos")):
        a1 = f"{'(#f32)' if ty == '#f32' else ''}-0.5" if ty == "#f64" else "(#f32)-0.5"
    elif name.startswith("pow"):
        a1 = f"{'(#f32)' if ty == '#f32' else ''}4.0" if ty == "#f64" else "(#f32)4.0"
    elif name.startswith("fmod"):
        a1 = f"{'(#f32)' if ty == '#f32' else ''}7.5" if ty == "#f64" else "(#f32)7.5"
    else:
        a1 = f"{'(#f32)' if ty == '#f32' else ''}1.0" if ty == "#f64" else "(#f32)1.0"
    return f"[{a0}, {a1}]"

lines = ["::std::math;", "", "&^main() -> #i32 {",
         "  acc64:#f64 = 0.0;",
         "  acc32:#f32 = (#f32)0.0;",
         "  flags:#i32 = 0;",
         ""]

tmp_idx = 0
for name, params, ret in decls:
    if len(params) not in (1, 2):
        continue
    if any(p not in ("#f32", "#f64") for p in params):
        continue
    # Array lifting is for unary/binary math; binary uses scalar broadcasting on the RHS in this smoke test.
    if len(params) == 1:
        arr_expr = array_pair_expr(params[0], name)
        if ret == "#f64":
            var = f"u64_{tmp_idx}"
            lines.append(f"  {var}:#f64[2] = std::math::{name}({arr_expr});")
            lines.append(f"  acc64 = acc64 + {var}[0] + {var}[1];")
        elif ret == "#f32":
            var = f"u32_{tmp_idx}"
            lines.append(f"  {var}:#f32[2] = std::math::{name}({arr_expr});")
            lines.append(f"  acc32 = acc32 + {var}[0] + {var}[1];")
        elif ret == "#b":
            var = f"ub_{tmp_idx}"
            lines.append(f"  {var}:#b[2] = std::math::{name}({arr_expr});")
            lines.append(f"  flags = flags + ((#i32){var}[0]) + ((#i32){var}[1]);")
        else:
            raise SystemExit(f"Unexpected unary return type: {ret}")
    else:
        lhs_arr = array_pair_expr(params[0], name)
        rhs_scalar = scalar_arg(params[1], name, 1)
        if ret == "#f64":
            var = f"b64_{tmp_idx}"
            lines.append(f"  {var}:#f64[2] = std::math::{name}({lhs_arr}, {rhs_scalar});")
            lines.append(f"  acc64 = acc64 + {var}[0] + {var}[1];")
        elif ret == "#f32":
            var = f"b32_{tmp_idx}"
            lines.append(f"  {var}:#f32[2] = std::math::{name}({lhs_arr}, {rhs_scalar});")
            lines.append(f"  acc32 = acc32 + {var}[0] + {var}[1];")
        elif ret == "#b":
            var = f"bb_{tmp_idx}"
            lines.append(f"  {var}:#b[2] = std::math::{name}({lhs_arr}, {rhs_scalar});")
            lines.append(f"  flags = flags + ((#i32){var}[0]) + ((#i32){var}[1]);")
        else:
            raise SystemExit(f"Unexpected binary return type: {ret}")
    tmp_idx += 1

lines.extend(["", "  ((#i32)acc64) + ((#i32)acc32) + flags", "}"])
dst.write_text("\n".join(lines) + "\n")
PY

"$DRIVER" -b vexel -o "$OUT_BASE" "$SRC" >"$TMP_DIR/stdout.log" 2>"$TMP_DIR/stderr.log"

if [[ ! -f "$OUT_VX" ]]; then
  echo "missing lowered Vexel output" >&2
  cat "$TMP_DIR/stderr.log" >&2 || true
  exit 1
fi

if grep -q 'std::math::' "$OUT_VX"; then
  echo "bundled std::math array-lifted calls with constexpr inputs must fold before backend handoff" >&2
  cat "$OUT_VX" >&2
  exit 1
fi

echo "ok"
