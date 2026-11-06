#!/usr/bin/env python3
"""State/completion observations and independent generated-C execution oracles."""
import ctypes
import itertools
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

EXE = Path(sys.argv.pop(1)).resolve()
REPO = Path(__file__).resolve().parents[3]
ORDERS = list(itertools.product(("fifo", "lifo", "random"), ("forward", "reverse")))


def run(command):
    return subprocess.run([str(item) for item in command], text=True, capture_output=True, timeout=120)


class ExecutionTests(unittest.TestCase):
    def check(self, source, queries, expected=None, status="resolved"):
        with tempfile.TemporaryDirectory(prefix="vexel-execution-") as directory:
            path = Path(directory) / "source.vx"
            path.write_text(source)
            for schedule, branches in ORDERS:
                with self.subTest(schedule=schedule, branches=branches, source=source):
                    flags = [part for query in queries for part in ("--query", query)]
                    result = run([EXE, path, "--schedule", schedule, "--branch-order", branches, *flags])
                    self.assertIn(result.returncode, (0, 1), result.stderr)
                    report = json.loads(result.stdout)
                    self.assertEqual(report["status"], status, report)
                    if expected is not None:
                        self.assertEqual([item["answer"] for item in report["queries"]], expected)

    def test_ordered_calls_arguments_and_nested_blocks(self):
        self.check("a={x:#u8=1;&take(){v=x;x+=1;v}&pair(a,b){[a,b]}"
                   "p=pair(take(),take());[p[0],p[1],x]};", ["value:a"], ["Values(1,2,3)"])
        self.check("a={x:#u8=1;&sink(){x+=2;}sink();{x*=2;};x};", ["value:a"], ["6"])
        self.check("a={x:#u8=1;&f(){x+=1;x}&g(){f()+f()}g();x};", ["value:a"], ["3"])
        self.check("a={x:#u8=1;1 ? {x=9;};0 ? {x=10;};x};", ["value:a"], ["9"])
        self.check("a={x:#u8; &init(){x=9;}init();x};", ["value:a"], ["9"])

    def test_recursive_progress_in_captured_state(self):
        self.check("a={x:#u8=0;&~inc()->#u8 {x+=1;x<3 ? inc() : x}inc()};",
                   ["value:a"], ["3"])
        self.check("a={x:#u8=0;&~loop()->#u8 {x=0;loop()}loop()};", ["value:a"], status="unresolved")
        self.check("a={x:#u8=0;&~inc()->#u8 {x+=1;x<3 ? inc() : x}before=inc();inc();[before,x]};",
                   ["value:a"], ["Values(3,4)"])

    def test_state_dependent_types_and_independent_signatures(self):
        self.check("a={x:#u8=0;&set(){x=1;}set();x==0 ? (#u8)7 : (#u16)9};",
                   ["type:a", "value:a"], ["#u16", "9"])
        self.check("&~loop()->#u16 {loop()}a=loop();", ["type:a"], ["#u16"])
        self.check("&~loop()->#u16 {loop()}a=loop();", ["value:a"], status="unresolved")
        self.check("a={n:#u8=1;n+=1;#P(data:#u8[n]);#P([1,2])};", ["type:a"], ["#P(data:#u8[2]){Captures(n(2),type:n(UInt(8)))}"])

    def project(self, source, directory, schedule="fifo", branches="forward", instrument=False):
        path = directory / "source.vx"
        path.write_text(source)
        generated = directory / "generated"
        result = run([EXE, path, "--source", "entry", "--budget", "2", "--schedule", schedule,
                      "--branch-order", branches, "--emit-c", generated])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        report = json.loads(result.stdout)
        code = generated / "vx_bridges.c"
        if instrument:
            text = code.read_text().replace('#include "vexel.h"', '#include "vexel.h"\nuint32_t vx_test_calls;')
            code.write_text(re.sub(r"((?:static )?void vx_flow_[^\n]+ \{)", r"\1 ++vx_test_calls;", text))
        library = directory / "test.so"
        compiled = run(["cc", "-std=c11", "-O2", "-shared", "-fPIC", "-I" + str(generated),
                        "-I" + str(REPO / "backends/msx-neo8/runtime"), code,
                        REPO / "backends/msx-neo8/runtime/vx_runtime.c", "-o", library])
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        dll = ctypes.CDLL(str(library))
        dll.vx_entry.argtypes = [ctypes.c_uint8]
        dll.vx_entry_read.argtypes = [ctypes.c_uint16]
        dll.vx_entry_read.restype = ctypes.c_uint32
        return dll, report, code.read_text()

    def test_runtime_recursion_and_returned_capture_state(self):
        cases = [
            ("&~sum(n:#u8)->#u16 {n==0 ? (#u16)0 : (#u16)n+sum(n-1)}&^entry(n:#u8){sum(n)}", lambda n: n*(n+1)//2),
            ("&^entry(n:#u8){acc:#u16=0;&~sum(k:#u8)->#u16 {k==0 ? acc : {acc+=(#u16)k;sum(k-1)}}sum(n);acc}", lambda n: n*(n+1)//2),
            ("&~even(n:#u8)->#u8 {n==0 ? (#u8)1 : odd(n-1)}&~odd(n:#u8)->#u8 {n==0 ? (#u8)0 : even(n-1)}&^entry(n:#u8){even(n)}", lambda n: int(n % 2 == 0)),
            ("&^entry(n:#u8){acc:#u8=n;&~bump(){acc+=1;}bump();acc}", lambda n: (n+1) % 256),
            ("&^entry(n:#u8){acc:#u16=(#u16)n;&~bump()->#u16 {acc+=1;acc}before=bump();bump();before+acc}", lambda n: 2*n+3),
        ]
        for source, oracle in cases:
            for schedule, branches in ORDERS:
                with self.subTest(source=source, schedule=schedule, branches=branches):
                    with tempfile.TemporaryDirectory(prefix="vexel-runtime-call-") as directory:
                        dll, report, _ = self.project(source, Path(directory), schedule, branches)
                        self.assertTrue(report["runtime_functions"])
                        self.assertTrue(all(frame["automatic_frame"] for frame in report["runtime_functions"]))
                        for n in range(256):
                            dll.vx_entry(n)
                            self.assertEqual(dll.vx_entry_read(0), oracle(n), n)

    def test_runtime_branch_state_and_completion_even_with_constant_result(self):
        source = "&^entry(n:#u8){x:#u16=1;n<128 ? {x=7;} : {x=9;};x}"
        with tempfile.TemporaryDirectory(prefix="vexel-branch-state-") as directory:
            dll, _, _ = self.project(source, Path(directory))
            for n in range(256):
                dll.vx_entry(n)
                self.assertEqual(dll.vx_entry_read(0), 7 if n < 128 else 9)
        source = "&^entry(n:#u8){x:#u16=(#u16)n;&~bump(){x+=1;}n<128 ? {bump();};x}"
        with tempfile.TemporaryDirectory(prefix="vexel-branch-call-") as directory:
            dll, _, _ = self.project(source, Path(directory))
            for n in range(256):
                dll.vx_entry(n)
                self.assertEqual(dll.vx_entry_read(0), n+1 if n < 128 else n)
        source = "&~visit(n:#u8)->#u8 {n==0 ? (#u8)0 : visit(n-1)}&^entry(n:#u8){visit(n);7}"
        with tempfile.TemporaryDirectory(prefix="vexel-completion-") as directory:
            dll, report, _ = self.project(source, Path(directory), instrument=True)
            calls = ctypes.c_uint32.in_dll(dll, "vx_test_calls")
            self.assertEqual(len(report["runtime_functions"]), 1)
            for n in range(32):
                calls.value = 0
                dll.vx_entry(n)
                self.assertEqual(dll.vx_entry_read(0), 7)
                self.assertEqual(calls.value, n+1)

    def test_unselected_runtime_body_is_not_an_artifact(self):
        source = "&~bad(n:#u8)->#u16 {missing()}&^entry(n:#u8){0 ? bad(n) : (#u16)n}"
        with tempfile.TemporaryDirectory(prefix="vexel-unselected-frame-") as directory:
            dll, report, code = self.project(source, Path(directory))
            self.assertEqual(report["runtime_functions"], [])
            self.assertNotIn("void vx_flow_", code)
            dll.vx_entry(17)
            self.assertEqual(dll.vx_entry_read(0), 17)

    def test_open_interfaces_cannot_emit(self):
        cases = [
            # An output-only interface cannot justify a read before its write.
            ("&^entry(n:#u8){x:#u8;&~init(k:#u8){x=x+k;}init(n);x}", "unresolved"),
            # A constant initializer defines an immutable equation.
            ("x:#u8=0;&^entry(n:#u8){x=n;x}", "contradictory"),
        ]
        for source,expected in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory(prefix="vexel-open-state-") as directory:
                directory = Path(directory)
                path, generated = directory / "source.vx", directory / "generated"
                path.write_text(source)
                result = run([EXE, path, "--source", "entry", "--budget", "2", "--emit-c", generated])
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertEqual(json.loads(result.stdout)["status"], expected)
                self.assertFalse(generated.exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
