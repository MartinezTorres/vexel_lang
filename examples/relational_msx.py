#!/usr/bin/env python3
"""Realize relational type/storage plans as editable SDCC/megalinker projects.

The experimental kernel owns representation and storage choices. The current
Vexel implementation supplies source execution and its existing MSX projector.
This adapter is explicit bootstrap code, not a second Vexel parser.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


SOURCE = """// Boundary inputs are checked to be in 0..250; payloads are in 0..252.
&!check_input(x:#u8);
&!write_a(index:#u8,value:#u16);
&!write_b(index:#u8,value:#u16);
&!read_a(index:#u8)->#u16;
&!read_b(index:#u8)->#u16;
&!sink()->#u16;
&^a(x:#u8)->#u16 {
    check_input(x);
    write_a(0,(#u16)x); write_a(1,(#u16)x+1); write_a(2,(#u16)x+2);
    nested=sink();
    read_a(0)+read_a(1)+read_a(2)+nested
}
&^b(x:#u8)->#u16 {
    check_input(x);
    write_b(0,(#u16)x); write_b(1,(#u16)x+1); write_b(2,(#u16)x+2);
    read_b(0)+read_b(1)+read_b(2)
}
&^observe()->#u16 {read_a(0)+read_a(1)+read_a(2)}
"""


def run(command, **kwargs):
    env = os.environ.copy()
    for key in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES", "MAKELEVEL", "BUILD"):
        env.pop(key, None)
    result = subprocess.run([str(arg) for arg in command], env=env, text=True,
                            capture_output=True, timeout=180, **kwargs)
    if result.returncode:
        raise RuntimeError(f"Command failed: {command}\n{result.stdout}\n{result.stderr}")
    return result


def physical_adapter(plan, mode, force_alias=False):
    width = plan["element_bits"] // 8
    first, second = plan["offsets"]
    if force_alias:
        second = first
    text = f'''#include "vexel.h"
uint8_t second_value;
/* This array is the payload storage whose budget the relation machine solved.
   It excludes compiler temporaries, runtime code, and the machine stack. */
static uint8_t payload[{plan['extent']}];
void vx_ext_check_input(uint8_t x) VX_ABI {{ if (x > 250) vx_trap(16); }}
static void store_payload(uint8_t base,uint8_t index,uint16_t value) {{
    uint8_t at;
    if (index >= 3 || value > 252) vx_trap(16);
    at=base+index*{width};
    payload[at]=(uint8_t)value;
'''
    if width == 2:
        text += "    payload[at+1]=(uint8_t)(value>>8);\n"
    text += f'''}}
static uint16_t load_payload(uint8_t base,uint8_t index) {{
    uint8_t at;
    if (index >= 3) vx_trap(16);
    at=base+index*{width};
'''
    text += ("    return (uint16_t)payload[at]+((uint16_t)payload[at+1]<<8);\n" if width == 2
             else "    return payload[at];\n")
    text += f'''}}
void vx_ext_write_a(uint8_t i,uint16_t x) VX_ABI {{store_payload({first},i,x);}}
void vx_ext_write_b(uint8_t i,uint16_t x) VX_ABI {{store_payload({second},i,x);}}
uint16_t vx_ext_read_a(uint8_t i) VX_ABI {{return load_payload({first},i);}}
uint16_t vx_ext_read_b(uint8_t i) VX_ABI {{return load_payload({second},i);}}
uint16_t vx_ext_sink(void) VX_ABI {{return {'vx_b(second_value)' if mode == 'overlap' else '0'};}}
'''
    return text


def host_library(project, adapter, destination):
    implementation = destination.with_suffix(".c")
    implementation.write_text(adapter)
    run(["cc", "-std=c11", "-O2", "-shared", "-fPIC",
         "-I" + str(project / "generated"), "-I" + str(project / "runtime"),
         *sorted((project / "generated").glob("*.c")), project / "runtime/vx_runtime.c",
         implementation, "-o", destination])
    library = ctypes.CDLL(str(destination))
    for name in ("a", "b"):
        function = getattr(library, "vx_" + name)
        function.argtypes = [ctypes.c_uint8]
        function.restype = ctypes.c_uint16
    library.vx_observe.restype = ctypes.c_uint16
    return library


def observe(library, mode, first, second):
    ctypes.c_uint8.in_dll(library, "second_value").value = second
    answer = library.vx_a(first)
    if mode != "overlap":
        answer += library.vx_b(second)
    retained = library.vx_observe() if mode == "retained" else None
    return answer, retained


def machine_harness(mode):
    text = '''volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    uint16_t answer;
    vx_last_result=0;
'''
    for index, (first, second) in enumerate(((0, 0), (0, 250), (250, 0), (250, 250),
                                              (7, 19), (19, 7), (127, 128), (128, 127)), 1):
        text += f"    second_value={second};answer=vx_a({first});\n"
        if mode != "overlap":
            text += f"    answer+=vx_b({second});\n"
        text += f"    if(answer!={3 * first + 3 * second + 6}u) vx_last_result={index};\n"
        if mode == "retained":
            text += f"    if(vx_observe()!={3 * first + 3}u) vx_last_result={index + 100};\n"
    return text + "    __asm halt __endasm;\n}\n"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("machine", type=Path)
    parser.add_argument("vexel", type=Path)
    parser.add_argument("--output", type=Path, default=Path("build/relational-msx"))
    args = parser.parse_args()
    args.machine = args.machine.resolve()
    args.vexel = args.vexel.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    # Fresh runs preserve inspectable projects and previous measurements.
    directory = Path(tempfile.mkdtemp(prefix="run-", dir=args.output.resolve()))
    source = directory / "source.vx"
    source.write_text(SOURCE)
    evidence = {"format": "vexel-relational-msx-1", "input_range": [0, 250],
                "machine_sha256": digest(args.machine), "vexel_sha256": digest(args.vexel),
                "adapter_sha256": digest(Path(__file__)), "source_sha256": digest(source), "candidates": []}
    for mode in ("serial", "overlap", "retained"):
        report = json.loads(run([args.machine, "--mode", mode, "--budget", "6"]).stdout)
        assert report["status"] == "resolved", report
        plan = report["plan"]
        project = directory / mode
        run([args.vexel, source, "--emit-msx", project])
        (project / "user/activation-plan.json").write_text(json.dumps(report, indent=2) + "\n")
        implementation = physical_adapter(plan, mode)
        library = host_library(project, implementation, directory / (mode + ".so"))
        checked = 0
        for first in range(251):
            for second in (0, 7, 250):
                answer, retained = observe(library, mode, first, second)
                assert answer == 3 * first + 3 * second + 6, (mode, first, second, answer)
                if retained is not None:
                    assert retained == 3 * first + 3, (mode, first, second, retained)
                checked += 1
        counterexample = None
        if mode != "serial":
            bad = host_library(project, physical_adapter(plan, mode, force_alias=True), directory / (mode + "-aliased.so"))
            actual = observe(bad, mode, 7, 19)
            expected = (84, 24 if mode == "retained" else None)
            assert actual != expected, (mode, "invalid shared storage was not exposed")
            counterexample = {"inputs": [7, 19], "expected": expected, "aliased": actual}
        (project / "user/main.c").write_text(implementation + machine_harness(mode))
        build = run(["make", "-C", project, "-j2", "simulate"])
        (directory / (mode + "-build.log")).write_text(build.stdout + build.stderr)
        execution_path = project / "build/execution.json"
        execution = json.loads(execution_path.read_text())
        assert execution["observation"]["last_result"] == 0, execution
        assert execution["observation"]["fault"] == 0, execution
        evidence["candidates"].append({"mode": mode, "project": str(project), "plan": plan,
                                       "host_cases": checked, "z80_cases": 8,
                                       "wrong_sharing_counterexample": counterexample,
                                       "execution_sha256": digest(execution_path)})
        print(f"{mode}: {plan['buffer_type']}, offsets={plan['offsets']}, "
              f"extent={plan['extent']}; {checked} host cases and 8 Z80 cases passed", flush=True)
    output = directory / "comparison.json"
    output.write_text(json.dumps(evidence, indent=2) + "\n")
    print("Evidence:", output)


if __name__ == "__main__":
    main()
