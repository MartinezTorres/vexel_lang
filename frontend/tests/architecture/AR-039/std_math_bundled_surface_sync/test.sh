#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"

python3 - "$ROOT" <<'PY'
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])

std_math = (root / "std" / "math.vx").read_text()
evaluator = (root / "frontend" / "src" / "transform" / "evaluator_call.cpp").read_text()
typechecker = (root / "frontend" / "src" / "type" / "typechecker_expr.cpp").read_text()
c_codegen = (root / "backends" / "c" / "src" / "codegen_support.cpp").read_text()
ml_codegen = (root / "backends" / "ext" / "megalinker" / "src" / "codegen_support.cpp").read_text()

std_funcs = set(re.findall(r"&!([A-Za-z_][A-Za-z0-9_]*)\s*\(", std_math))
if not std_funcs:
    raise SystemExit("failed to parse bundled std/math.vx function surface")

evaluator_funcs = set(re.findall(r'if \(name == "([A-Za-z0-9_]+)"\)', evaluator))
missing = sorted(std_funcs - evaluator_funcs)
if missing:
    extra = sorted((evaluator_funcs - std_funcs) & std_funcs)
    raise SystemExit(
        "frontend std::math CTE surface drift:\n"
        f"  missing in evaluator: {missing}\n"
        f"  extra in evaluator: {extra}"
    )

def mapped_source_names(code: str) -> set[str]:
    m = re.search(r"kNames\s*=\s*\{(.*?)\};", code, re.S)
    if not m:
        raise SystemExit("failed to parse std::math kNames table")
    direct = set(re.findall(r'"([A-Za-z0-9_]+)"', m.group(1)))
    alias_pairs = set(re.findall(r'name == "([A-Za-z0-9_]+)"\)\s*return\s*"([A-Za-z0-9_]+)"', code))
    return direct | {src for src, _dst in alias_pairs}

c_mapped = mapped_source_names(c_codegen)
if std_funcs != c_mapped:
    missing = sorted(std_funcs - c_mapped)
    extra = sorted(c_mapped - std_funcs)
    raise SystemExit(
        "C backend std::math runtime mapping drift:\n"
        f"  missing in backend map: {missing}\n"
        f"  extra in backend map: {extra}"
    )

ml_mapped = mapped_source_names(ml_codegen)
if std_funcs != ml_mapped:
    missing = sorted(std_funcs - ml_mapped)
    extra = sorted(ml_mapped - std_funcs)
    raise SystemExit(
        "megalinker backend std::math runtime mapping drift:\n"
        f"  missing in backend map: {missing}\n"
        f"  extra in backend map: {extra}"
    )

required_gates = [
    (evaluator, "ModuleOrigin::BundledStd", "frontend evaluator must gate std::math folding to bundled std"),
    (typechecker, "mod->origin != ModuleOrigin::BundledStd", "typechecker std::math array lifting must gate to bundled std"),
    (c_codegen, "mod->origin != ModuleOrigin::BundledStd", "C backend std::math mapping must gate to bundled std"),
    (ml_codegen, "mod->origin != ModuleOrigin::BundledStd", "megalinker std::math mapping must gate to bundled std"),
]
for text, needle, message in required_gates:
    if needle not in text:
        raise SystemExit(message)

print("ok")
PY
