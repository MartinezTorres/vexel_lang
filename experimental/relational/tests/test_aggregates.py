#!/usr/bin/env python3
"""Structural state interfaces, copies, recursive C frames and checked places."""
import ctypes
import itertools
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXE = Path(sys.argv.pop(1)).resolve()
REPO = Path(__file__).resolve().parents[3]
ORDERS = tuple(itertools.product(("fifo", "lifo", "random"), ("forward", "reverse")))


def run(command):
    return subprocess.run([str(x) for x in command], capture_output=True, text=True, timeout=120)


class AggregateTests(unittest.TestCase):
    def query(self, source, expected, query="value:a"):
        with tempfile.TemporaryDirectory(prefix="vexel-aggregate-") as directory:
            path = Path(directory) / "source.vx"
            path.write_text(source)
            for schedule, branches in ORDERS:
                result = run([EXE, path, "--query", query, "--schedule", schedule, "--branch-order", branches])
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(json.loads(result.stdout)["queries"][0]["answer"], expected)

    def test_copies_nested_places_and_captured_updates(self):
        self.query("a={data:#u8[2][2]=[[1,2],[3,4]];before=data;data[1][0]+=5;[before[1][0],data[1][0]]};", "Values(3,8)")
        self.query("#P(x:#u8,data:#u8[2]);a={p=#P(1,[2,3]);before=p;&f(){p.data[1]+=5;}f();[before.data[1],p.data[1]]};", "Values(3,8)")
        self.query("a={data:#u8[2]=[1,2];i:#u8=0;&index(){before=i;i+=1;before}data[index()]+=3;[data[0],data[1],i]};", "Values(4,2,1)")

    def test_compound_snapshot_and_rhs_updates(self):
        self.query("a={data:#u8[2]=[1,2];&change(){data[0]=10;data[1]=20;3}data[0]+=change();data};", "Values(4,20)")
        self.query("a={x:#u8=1;&change(){x=10;2}x+=change();x};", "3")
        self.query("a={data:#u8[2]=[1,2];i:#u8=0;&change(){i=1;data[1]=20;3}data[i]+=change();[data[0],data[1],i]};", "Values(4,20,1)")

    def project(self, source, directory, budget=8, schedule="fifo", branches="forward"):
        path, generated = directory / "source.vx", directory / "generated"
        path.write_text(source)
        result = run([EXE, path, "--source", "entry", "--budget", budget,
                      "--schedule", schedule, "--branch-order", branches, "--emit-c", generated])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return generated, json.loads(result.stdout)

    def test_runtime_aggregate_interfaces_and_recursion(self):
        cases = [
            ("&~f(a:#u8[2])->#u8[2]{a[1]+=1;a}&^entry(n:#u8){f([n,n+1])}", lambda n: [n, (n+2)%256]),
            ("&^entry(n:#u8){a:#u8[2]=[n,n+1];&~f(){a[0]+=1;}f();a}", lambda n: [(n+1)%256, (n+1)%256]),
            ("#P(x:#u8,y:#u8[2]);&^entry(n:#u8){p=#P(n,[n+1,n+2]);&~f(){p.y[0]+=1;}f();p}", lambda n: [n, (n+2)%256, (n+2)%256]),
            ("&~f(a:#u16[2],n:#u8)->#u16[2]{n==0 ? a : {a[0]+=(#u16)n;f(a,n-1)}}&^entry(n:#u8){f([(#u16)0,(#u16)n],n)}", lambda n: [n*(n+1)//2, n]),
            ("&^entry(n:#u8){x:#u8;&~init(k:#u8){x=k;}init(n);x}", lambda n: [n]),
            ("&^entry(n:#u8){x:#u8[2];&~init(k:#u8){x=[k,k+1];}init(n);x}", lambda n: [n,(n+1)%256]),
            ("&^entry(n:#u8){x:#u8=n;x+=0;n<128 ? [x] : [x+1]}", lambda n: [n if n<128 else (n+1)%256]),
        ]
        for source, oracle in cases:
            for schedule, branches in ORDERS:
                with self.subTest(source=source, schedule=schedule, branches=branches), tempfile.TemporaryDirectory(prefix="vexel-aggregate-c-") as temporary:
                    directory = Path(temporary)
                    generated, _ = self.project(source, directory, schedule=schedule, branches=branches)
                    library = directory / "test.so"
                    result = run(["cc", "-std=c11", "-O2", "-shared", "-fPIC", "-I"+str(generated),
                                  "-I"+str(REPO / "backends/msx-neo8/runtime"), generated / "vx_bridges.c",
                                  REPO / "backends/msx-neo8/runtime/vx_runtime.c", "-o", library])
                    self.assertEqual(result.returncode, 0, result.stderr)
                    dll = ctypes.CDLL(str(library))
                    dll.vx_entry.argtypes = [ctypes.c_uint8]
                    dll.vx_entry_read.argtypes = [ctypes.c_uint16]
                    dll.vx_entry_read.restype = ctypes.c_uint32
                    for n in range(256):
                        dll.vx_entry(n)
                        expected = oracle(n)
                        self.assertEqual([dll.vx_entry_read(i) for i in range(len(expected))], expected, n)

    def test_runtime_bounds_survive_discarded_values_and_writes(self):
        for expression in ("a[n];", "a[n]=7;", "a[n]+=1;"):
            with self.subTest(expression=expression), tempfile.TemporaryDirectory(prefix="vexel-index-trap-") as temporary:
                directory = Path(temporary)
                generated, _ = self.project("&^entry(n:#u8){a:#u8[2]=[1,2];"+expression+"7}", directory)
                harness = directory / "main.c"
                harness.write_text('#include "vexel.h"\n#include <stdlib.h>\n'
                                   'void vx_trap(uint8_t reason){exit(reason);}\n'
                                   'int main(void){vx_entry(255);return 0;}\n')
                binary = directory / "test"
                result = run(["cc", "-std=c11", "-O2", "-I"+str(generated), "-I"+str(REPO / "backends/msx-neo8/runtime"),
                              generated / "vx_bridges.c", harness, "-o", binary])
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(run([binary]).returncode, 16)


if __name__ == "__main__":
    unittest.main(verbosity=2)
