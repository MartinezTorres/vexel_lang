#!/usr/bin/env python3
"""Exercise recursive aggregate state interfaces on the host and NEO-8 Z80."""
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
    parser.add_argument("--output", type=Path, default=Path("build/relational-aggregate-msx"))
    args = parser.parse_args()
    machine = args.machine.resolve()
    source = REPO / "examples/relational_aggregate.vx"
    args.output.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="run-", dir=args.output.resolve()))
    generated, project = directory / "projection", directory / "project"
    source_msx.run([machine, source, "--source", "accumulate", "--budget", "6", "--emit-c", generated])
    report = source_msx.backend.package(generated, project, source, machine)
    assert report["sources"][0]["logical_type"] == "#Packet(total:#u16,data:#u8[2])", report
    assert len(report["runtime_functions"]) == 1, report
    assert report["runtime_functions"][0]["automatic_frame"], report
    library = directory / "execution.so"
    source_msx.run(["cc", "-std=c11", "-O2", "-shared", "-fPIC", "-I" + str(project / "generated"),
                    "-I" + str(project / "runtime"), project / "generated/vx_bridges.c",
                    project / "runtime/vx_runtime.c", "-o", library])
    dll = ctypes.CDLL(str(library))
    dll.vx_accumulate.argtypes = [ctypes.c_uint8, ctypes.c_uint8]
    dll.vx_accumulate_read.argtypes = [ctypes.c_uint16]
    dll.vx_accumulate_read.restype = ctypes.c_uint32
    for n in range(9):
        for seed in range(256):
            dll.vx_accumulate(n, seed)
            assert [dll.vx_accumulate_read(i) for i in range(3)] == [seed+n*(n+1)//2, (seed+n)%256, (seed+1)%256], (n, seed)
    # Sample every admitted recursion depth on Z80, including carry boundaries.
    seeds = (0, 1, 127, 128, 250, 255)
    (project / "user/main.c").write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
static const uint8_t seeds[] = {0,1,127,128,250,255};
static const uint8_t triangles[] = {0,1,3,6,10,15,21,28,36};
void main(void) VX_ABI VX_RESIDENT {
    uint8_t n, s;
    uint16_t expected;
    vx_last_result=0;
    for(n=0;n<=8;++n) for(s=0;s<6;++s) {
        expected=(uint16_t)seeds[s]+triangles[n];
        vx_accumulate(n,seeds[s]);
        if(vx_accumulate_read(0)!=expected || vx_accumulate_read(1)!=(uint8_t)(seeds[s]+n) || vx_accumulate_read(2)!=(uint8_t)(seeds[s]+1)) {
            vx_last_result=1u+(uint16_t)n*6u+s;
            __asm halt __endasm;
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
    evidence = {"format": "vexel-relational-aggregate-msx-1", "machine_sha256": sha(machine),
                "source_sha256": sha(source), "driver_sha256": sha(__file__), "host_cases": 9 * 256,
                "z80_cases": 9 * len(seeds), "z80_seeds": seeds, "maximum_tested_call_depth": 9,
                "project": str(project), "report": report,
                "execution_sha256": sha(project / "build/execution.json")}
    (directory / "comparison.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print("2,304 host and 54 Z80 inputs passed; runtime recursion and returned record/array state")
    print("Evidence:", directory / "comparison.json")


if __name__ == "__main__":
    main()
