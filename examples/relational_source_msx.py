#!/usr/bin/env python3
"""Exercise real Vexel source -> relational model -> C -> SDCC/megalinker -> Z80."""
import argparse
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("relational_project", REPO / "experimental/relational/project_msx.py")
backend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backend)


def run(command):
    env = os.environ.copy()
    for key in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES", "MAKELEVEL", "BUILD"):
        env.pop(key, None)
    result = subprocess.run([str(x) for x in command], env=env, text=True, capture_output=True, timeout=180)
    if result.returncode:
        raise RuntimeError(f"{command}\n{result.stdout}\n{result.stderr}")
    return result


def host(project, destination, override=None):
    code = project / "generated/vx_bridges.c"
    if override is not None:
        code = destination.with_suffix(".c")
        code.write_text(override)
    run(["cc", "-std=c11", "-O2", "-shared", "-fPIC", "-I" + str(project / "generated"),
         "-I" + str(project / "runtime"), code, project / "runtime/vx_runtime.c", "-o", destination])
    library = ctypes.CDLL(str(destination))
    for name in ("first", "second"):
        getattr(library, "vx_" + name).argtypes = [ctypes.c_uint8]
        reader = getattr(library, "vx_" + name + "_read")
        reader.argtypes = [ctypes.c_uint16]
        reader.restype = ctypes.c_uint32
    return library


def observe(lib, mode, x, y):
    lib.vx_first(x)
    if mode == "serial":
        first = [lib.vx_first_read(i) for i in range(4)]
    lib.vx_second(y)
    if mode != "serial":
        first = [lib.vx_first_read(i) for i in range(4)]
    second = [lib.vx_second_read(i) for i in range(4)]
    return first, second


def oracle(x, y):
    return [2, x, x + 1, x + 2], [1, y, y + 1, y + 2]


def harness(mode):
    cases = ((0, 0), (0, 250), (250, 0), (250, 250), (7, 19), (19, 7), (127, 128), (128, 127))
    text = '#include "vexel.h"\nvolatile uint32_t vx_last_result;\nvoid main(void) VX_ABI VX_RESIDENT {\n    vx_last_result=0;\n'
    for case, (x, y) in enumerate(cases, 1):
        first, second = oracle(x, y)
        text += f"    vx_first({x});\n"
        if mode != "serial":
            text += f"    vx_second({y});\n"
        for i, expected in enumerate(first):
            text += f"    if(vx_first_read({i})!={expected}UL) vx_last_result={case};\n"
        if mode == "serial":
            text += f"    vx_second({y});\n"
        for i, expected in enumerate(second):
            text += f"    if(vx_second_read({i})!={expected}UL) vx_last_result={case + 100};\n"
    return text + "    __asm halt __endasm;\n}\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("machine", type=Path)
    parser.add_argument("--output", type=Path, default=Path("build/relational-source-msx"))
    args = parser.parse_args()
    machine = args.machine.resolve()
    source = REPO / "examples/relational_source.vx"
    args.output.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="run-", dir=args.output.resolve()))
    sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
    evidence = {"format": "vexel-relational-source-msx-1", "machine_sha256": sha(machine),
                "source_sha256": sha(source), "driver_sha256": sha(__file__), "candidates": []}
    for mode in ("serial", "overlap", "retained"):
        generated = directory / (mode + "-projection")
        run([machine, source, "--query", "value:left_length", "--query", "value:right_length", "--query", "value:field_type_example",
             "--source", "first", "--source", "second", "--mode", mode, "--budget", "6", "--emit-c", generated])
        project = directory / mode
        report = backend.package(generated, project, source, machine)
        assert [q["answer"] for q in report["queries"]] == ["4", "3", "42"], report
        lib = host(project, directory / (mode + ".so"))
        checked = 0
        for x in range(251):
            for y in (0, 7, 250):
                assert observe(lib, mode, x, y) == oracle(x, y), (mode, x, y)
                checked += 1
        counterexample = None
        if mode != "serial":
            code = (generated / "vx_bridges.c").read_text()
            # A deliberate mutation of this example's three-byte second output
            # supplies a falsifiable witness that the separation is necessary.
            for at in range(3, 6):
                code = code.replace(f"vx_payload[{at}]", f"vx_payload[{at - 3}]")
            wrong = host(project, directory / (mode + "-aliased.so"), code)
            actual = observe(wrong, mode, 7, 19)
            assert actual != oracle(7, 19)
            counterexample = {"inputs": [7, 19], "expected": oracle(7, 19), "aliased": actual}
        (project / "user/main.c").write_text(harness(mode))
        result = run(["make", "-C", project, "-j2", "simulate"])
        (directory / (mode + "-build.log")).write_text(result.stdout + result.stderr)
        execution = json.loads((project / "build/execution.json").read_text())
        assert execution["observation"]["last_result"] == execution["observation"]["fault"] == 0, execution
        evidence["candidates"].append({"mode": mode, "project": str(project), "report": report, "host_cases": checked,
                                       "z80_cases": 8, "wrong_sharing_counterexample": counterexample,
                                       "execution_sha256": sha(project / "build/execution.json")})
        print(f"{mode}: source types unchanged; storage widths {[s['element_bits'] for s in report['sources']]}; "
              f"{checked} host and 8 Z80 cases passed", flush=True)
    (directory / "comparison.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print("Evidence:", directory / "comparison.json")


if __name__ == "__main__":
    main()
