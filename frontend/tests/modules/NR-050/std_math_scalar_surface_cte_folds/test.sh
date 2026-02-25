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
            if not part:
                continue
            pm = re.match(r"[A-Za-z_][A-Za-z0-9_]*\s*:\s*(#[A-Za-z0-9_.]+)$", part)
            if not pm:
                raise SystemExit(f"Could not parse std::math parameter: {part!r}")
            params.append(pm.group(1))
    decls.append((name, params, ret))

if not decls:
    raise SystemExit("No std::math extern declarations parsed")

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
    elif name.startswith(("sin", "cos", "tan", "atan", "exp", "fabs")):
        base = "0.5"
    return f"{cast}{base}" if cast else base

lines = []
lines.append("::std::math;")
lines.append("")
lines.append("&^main() -> #i32 {")
lines.append("  acc64:#f64 = 0.0;")
lines.append("  acc32:#f32 = (#f32)0.0;")
lines.append("  flags:#i32 = 0;")
lines.append("")

for name, params, ret in decls:
    args = [scalar_arg(pty, name, i) for i, pty in enumerate(params)]
    call = f"std::math::{name}(" + ", ".join(args) + ")"
    if ret == "#f64":
        lines.append(f"  acc64 = acc64 + {call};")
    elif ret == "#f32":
        lines.append(f"  acc32 = acc32 + {call};")
    elif ret == "#b":
        lines.append(f"  flags = flags + ((#i32){call});")
    else:
        raise SystemExit(f"Unexpected std::math return type: {ret}")

lines.append("")
lines.append("  ((#i32)acc64) + ((#i32)acc32) + flags")
lines.append("}")
lines.append("")

dst.write_text("\n".join(lines))
PY

"$DRIVER" -b vexel -o "$OUT_BASE" "$SRC" >"$TMP_DIR/stdout.log" 2>"$TMP_DIR/stderr.log"

if [[ ! -f "$OUT_VX" ]]; then
  echo "missing lowered Vexel output" >&2
  cat "$TMP_DIR/stderr.log" >&2 || true
  exit 1
fi

if grep -q 'std::math::' "$OUT_VX"; then
  echo "bundled std::math scalar calls with constexpr inputs must fold before backend handoff" >&2
  cat "$OUT_VX" >&2
  exit 1
fi

echo "ok"
