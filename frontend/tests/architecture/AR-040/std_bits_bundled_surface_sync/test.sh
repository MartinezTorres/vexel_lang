#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"

python3 - "$ROOT" <<'PY'
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])

std_bits = (root / "std" / "bits.vx").read_text()
evaluator = (root / "frontend" / "src" / "transform" / "evaluator_call.cpp").read_text()
c_codegen = (root / "backends" / "c" / "src" / "codegen_support.cpp").read_text()
ml_codegen_path = root / "backends" / "ext" / "megalinker" / "src" / "codegen_support.cpp"
ml_codegen = ml_codegen_path.read_text() if ml_codegen_path.exists() else None

std_funcs = set(re.findall(r"&!([A-Za-z_][A-Za-z0-9_]*)\s*\(", std_bits))
if not std_funcs:
    raise SystemExit("failed to parse bundled std/bits.vx function surface")

evaluator_funcs = set(re.findall(r'if \(name == "([A-Za-z0-9_]+)"\)', evaluator))
missing_eval = sorted(std_funcs - evaluator_funcs)
if missing_eval:
    raise SystemExit("frontend std::bits CTE surface drift:\n"
                     f"  missing in evaluator: {missing_eval}")


def mapped_source_names(code: str) -> set[str]:
    m = re.search(r"is_std_bits_builtin_name_impl\s*\([^\)]*\)\s*\{.*?kNames\s*=\s*\{(.*?)\};", code, re.S)
    if not m:
        raise SystemExit("failed to parse std::bits kNames table")
    return set(re.findall(r'"([A-Za-z0-9_]+)"', m.group(1)))

c_mapped = mapped_source_names(c_codegen)

missing_c = sorted(std_funcs - c_mapped)
if missing_c:
    raise SystemExit("C backend std::bits runtime mapping drift:\n"
                     f"  missing in backend map: {missing_c}")

if ml_codegen is not None:
    ml_mapped = mapped_source_names(ml_codegen)
    missing_ml = sorted(std_funcs - ml_mapped)
    if missing_ml:
        raise SystemExit("megalinker backend std::bits runtime mapping drift:\n"
                         f"  missing in backend map: {missing_ml}")

required_gates = [
    (evaluator, "std/bits.vx", "frontend evaluator must gate std::bits folding to bundled std"),
    (evaluator, "ModuleOrigin::BundledStd", "frontend evaluator must gate std::bits folding to bundled std"),
    (c_codegen, "std/bits.vx", "C backend std::bits mapping must gate to bundled std"),
    (c_codegen, "ModuleOrigin::BundledStd", "C backend std::bits mapping must gate to bundled std"),
]
if ml_codegen is not None:
    required_gates.extend([
        (ml_codegen, "std/bits.vx", "megalinker std::bits mapping must gate to bundled std"),
        (ml_codegen, "ModuleOrigin::BundledStd", "megalinker std::bits mapping must gate to bundled std"),
    ])
for text, needle, message in required_gates:
    if needle not in text:
        raise SystemExit(message)

print("ok")
PY
