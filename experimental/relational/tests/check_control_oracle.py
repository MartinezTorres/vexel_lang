import ctypes
import hashlib
import itertools
import json
from pathlib import Path
import subprocess
import tempfile

repo = Path.cwd()
machine = repo / "build/relational/vexel-source"
source = (repo / "examples/relational_control.vx").read_text()
prefix, body = source.split("&^control", 1)
declarations = [line for line in prefix.splitlines() if line and not line.startswith("//")]
orders = [declarations, declarations[::-1], declarations[2:] + declarations[:2]]
reports = []


def run(command):
    completed = subprocess.run([str(part) for part in command], text=True, capture_output=True, timeout=90)
    assert completed.returncode == 0, completed.stderr + completed.stdout
    return completed


with tempfile.TemporaryDirectory(prefix="vexel-control-orders-") as temporary:
    directory = Path(temporary)
    for order, schedule, branches in itertools.product(range(3), ("fifo", "lifo", "random"), ("forward", "reverse")):
        path = directory / f"case-{order}-{schedule}-{branches}.vx"
        path.write_text("\n".join(orders[order]) + "\n&^control" + body)
        generated = path.with_suffix("")
        result = run([machine, path, "--query", "value:base", "--source", "control", "--budget", "4",
                      "--schedule", schedule, "--branch-order", branches, "--emit-c", generated])
        report = json.loads(result.stdout)
        assert report["queries"][0]["answer"] == "6"
        assert report["sources"][0]["logical_type"] == "#u16[2]"
        library = path.with_suffix(".so")
        run(["cc", "-std=c11", "-O2", "-fPIC", "-shared", "-I" + str(generated),
             "-I" + str(repo / "backends/msx-neo8/runtime"), generated / "vx_bridges.c",
             repo / "backends/msx-neo8/runtime/vx_runtime.c", "-o", library])
        dll = ctypes.CDLL(str(library))
        dll.vx_control.argtypes = [ctypes.c_uint8]
        dll.vx_control_read.argtypes = [ctypes.c_uint16]
        dll.vx_control_read.restype = ctypes.c_uint32
        for value in range(256):
            dll.vx_control(value)
            assert [dll.vx_control_read(i) for i in range(2)] == [value, 2*value+16 if value < 128 else value+9]
        reports.append({"declaration_order": order, "schedule": schedule, "branches": branches,
                        "input_cases": 256, "c_sha256": hashlib.sha256((generated / "vx_bridges.c").read_bytes()).hexdigest()})

evidence = {"machine_sha256": hashlib.sha256(machine.read_bytes()).hexdigest(), "cases": reports}
Path("build/relational-control-order-oracle.json").write_text(json.dumps(evidence, indent=2) + "\n")
print("18 declaration/search permutations; 4608 host executions passed")
