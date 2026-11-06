#!/usr/bin/env python3
"""Check recursive/overloaded/stateful source on the host and NEO-8 Z80."""
import argparse
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("source_msx", REPO / "examples/relational_source_msx.py")
source_msx = importlib.util.module_from_spec(spec)
spec.loader.exec_module(source_msx)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("machine", type=Path)
    parser.add_argument("--output", type=Path, default=Path("build/relational-control-msx"))
    args = parser.parse_args()
    machine = args.machine.resolve()
    source = REPO / "examples/relational_control.vx"
    args.output.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="run-", dir=args.output.resolve()))
    generated, project = directory / "projection", directory / "project"
    source_msx.run([machine, source, "--query", "value:base", "--source", "control",
                    "--budget", "4", "--emit-c", generated])
    report = source_msx.backend.package(generated, project, source, machine)
    assert report["queries"][0]["answer"] == "6", report
    assert report["sources"][0]["logical_type"] == "#u16[2]", report
    library = directory / "control.so"
    source_msx.run(["cc", "-std=c11", "-O2", "-shared", "-fPIC", "-I" + str(project / "generated"),
                    "-I" + str(project / "runtime"), project / "generated/vx_bridges.c",
                    project / "runtime/vx_runtime.c", "-o", library])
    dll = ctypes.CDLL(str(library))
    dll.vx_control.argtypes = [ctypes.c_uint8]
    dll.vx_control_read.argtypes = [ctypes.c_uint16]
    dll.vx_control_read.restype = ctypes.c_uint32
    for value in range(256):
        dll.vx_control(value)
        assert [dll.vx_control_read(i) for i in range(2)] == [value, 2 * value + 16 if value < 128 else value + 9]
    (project / "user/main.c").write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    uint16_t input;
    uint32_t expected;
    vx_last_result=0;
    for(input=0;input<256;++input) {
        expected=input<128 ? 2UL*input+16UL : input+9UL;
        vx_control((uint8_t)input);
        if(vx_control_read(0)!=input || vx_control_read(1)!=expected) {
            vx_last_result=input+1UL;
            break;
        }
    }
    __asm halt __endasm;
}
''')
    execution = source_msx.run(["make", "-C", project, "-j2", "simulate"])
    (directory / "build.log").write_text(execution.stdout + execution.stderr)
    result = json.loads((project / "build/execution.json").read_text())
    assert result["observation"]["last_result"] == result["observation"]["fault"] == 0, result
    sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
    evidence = {"format": "vexel-relational-control-msx-1", "machine_sha256": sha(machine),
                "source_sha256": sha(source), "driver_sha256": sha(__file__), "host_cases": 256,
                "z80_cases": 256, "project": str(project), "report": report,
                "execution_sha256": sha(project / "build/execution.json")}
    (directory / "comparison.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print("256 host and 256 Z80 inputs passed; four payload bytes")
    print("Evidence:", directory / "comparison.json")


if __name__ == "__main__":
    main()
