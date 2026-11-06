#!/usr/bin/env python3
"""Observable source semantics and generated C; no old inference reports."""
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
EXAMPLE = REPO / "examples/relational_source.vx"
ORDERS = list(itertools.product(("fifo", "lifo", "random"), ("forward", "reverse")))


def run(args, timeout=90):
    return subprocess.run([str(x) for x in args], text=True, capture_output=True, timeout=timeout)


class SourceTests(unittest.TestCase):
    def solve(self, source, *flags, schedule="fifo", branch="forward"):
        result = run([EXE, source, "--schedule", schedule, "--branch-order", branch, *flags])
        self.assertIn(result.returncode, (0, 1), result.stderr)
        return [json.loads(line) for line in result.stdout.splitlines()]

    def check(self, text, queries, expected=None, status="resolved"):
        with tempfile.TemporaryDirectory(prefix="vexel-source-test-") as directory:
            source = Path(directory) / "main.vx"
            source.write_text(text)
            for schedule, branch in ORDERS:
                with self.subTest(schedule=schedule, branch=branch, source=text):
                    flags = [part for q in queries for part in ("--query", q)]
                    report, = self.solve(source, *flags, schedule=schedule, branch=branch)
                    self.assertEqual(report["status"], status, report)
                    if expected is not None:
                        self.assertEqual([q["answer"] for q in report["queries"]], expected)
                    if status != "resolved":
                        self.assertNotIn("queries", report)
                        self.assertNotIn("sources", report)

    def test_seedless_dimension_equations_and_source_order(self):
        declarations = ["a:#u8[|b|+1];", "b:#u8[7-|a|];"]
        for order in (declarations, declarations[::-1]):
            self.check("".join(order) + "^answer=|a|*10+|b|;", ["value:answer", "type:a", "type:b"],
                       ["43", "#u8[4]", "#u8[3]"])
        self.check("a:#u8[|b|+1];b:#u8[|a|+1];", ["length:a"], status="contradictory")
        self.check("a:#u8[|b|+1];b:#u8[6-|a|];", ["length:a"], status="contradictory")
        self.check("a:#u8[|b|];b:#u8[|a|];", ["length:a"], status="unresolved")

    def test_partial_schema_and_later_complete_demand(self):
        for schedule, branch in ORDERS:
            reports = self.solve(EXAMPLE, "--query", "type:partial.count", "--then", "--query", "type:partial",
                                 schedule=schedule, branch=branch)
            self.assertEqual(reports[0]["queries"][0]["answer"], "#u8")
            self.assertEqual(reports[1]["status"], "unresolved", reports)
            self.assertNotIn("queries", reports[1])
        self.check(EXAMPLE.read_text(), ["value:field_type_example"], ["42"])
        self.check(EXAMPLE.read_text(), ["value:partial.count"], status="unresolved")
        self.check(EXAMPLE.read_text().replace("missing:#u8=missing;", "missing:#u8=1;"), ["type:partial"],
                   ["#Packet(count:#u8,data:#u16[3]){Captures(offset(1),type:offset(UInt(8)))}"])
        self.check(EXAMPLE.read_text().replace("missing:#u8=missing;", "missing:#u8=2;"), ["type:partial"], status="contradictory")

    def test_generic_instances_captures_and_nominal_identity(self):
        source = """&make(offset:#u8,n:#u8,x){#P(n:#u8,data:#[x][n+offset]);#P(n,[x,x,x])}
                    first=make(1,2,(#u8)7); second=make(2,1,(#u16)9);"""
        self.check(source, ["type:first", "type:second"],
                   ["#P(n:#u8,data:#u8[3]){Captures(offset(1),type:offset(UInt(8)),type:x(UInt(8)))}",
                    "#P(n:#u8,data:#u16[3]){Captures(offset(2),type:offset(UInt(8)),type:x(UInt(16)))}"])
        self.check("#A(x:#u8);#B(x:#u8);a:#A=#B(1);", ["type:a"], status="contradictory")
        self.check("#Box(x);#Pair(a,b);p=#Pair(#Box((#u8)3),#Box((#u16)4));",
                   ["type:p.a.x", "type:p.b.x"], ["#u8", "#u16"])
        self.check("unknown:#u8;&make(x){#P(data:#[x][2]);#P([1,2])}p=make(unknown);", ["type:p"],
                   ["#P(data:#u8[2]){Captures(type:x(UInt(8)))}"])
        self.check("unknown:#u8[2];#P(data:#u8[|unknown|]);p=#P([1,2]);", ["type:p"],
                   ["#P(data:#u8[2]){Captures(length:unknown(2))}"])

    def test_partial_element_queries_do_not_execute_values(self):
        self.check("a:#u8[|b|]=[missing(),other()];b:#u8[|a|]=[1,2];^answer:#u8={v:#[a[0]]=7;v};",
                   ["value:answer"], ["7"])
        self.check("#P(n:#u8,data:#Missing);p=#P(2,missing());", ["type:p.n"], ["#u8"])
        self.check("#P(n:#u8,data:#Missing);p=#P(2,missing());", ["type:p"], status="unresolved")
        self.check("a:#u8[2]=[1,2,3];", ["length:a"], status="contradictory")

    def test_unsigned_arithmetic_and_conversion_oracle(self):
        # Expected values are calculated independently using Python's integers.
        expressions = [("(#u8)255+1", 0), ("(#u8)0-1", 255), ("(#u8)200*2", 144),
                       ("(#u16)255+1", 256), ("(#u8)(-1)", 255), ("(#u8)257", 1),
                       ("((#u8)255)+((#u16)1)", 256), ("7-10", -3), ("300*300", 90000)]
        for expression, expected in expressions:
            self.check("^answer=" + expression + ";", ["value:answer"], [str(expected)])
        self.check("^answer:#u8=256;", ["value:answer"], status="contradictory")
        self.check("^answer:#u8=(#u16)7;", ["value:answer"], status="contradictory")
        # Exact operands are concretized before the operation. Arithmetic
        # wrapping does not excuse a literal that cannot enter that type.
        for expression in ("(#u8)1+300", "(#u8)1+(-1)", "(#u16)2*65536"):
            self.check("^answer=" + expression + ";", ["value:answer"], status="contradictory")
        self.check("a:#u16[2]=[1,300];^answer=a[1];", ["value:answer"], ["300"])
        self.check("a:#u16[2]=[1,300];^answer=a[2];", ["value:answer"], status="contradictory")

    def test_unmarked_recursion_stays_open(self):
        self.check("&loop(x){loop(x)}^answer=loop(1);", ["value:answer"], status="unresolved")
        self.check("&a(x){b(x)}&b(x){a(x)}^answer=a(1);", ["value:answer"], status="unresolved")
    def test_overload_selection_is_closed_and_independent_of_consumers(self):
        declarations = ["&f(x:#u8){10}", "&f(x:#u16){20}", "&f(x){30}"]
        for order in (declarations, declarations[::-1]):
            source = "".join(order)
            self.check(source + "a=f((#u8)1);b=f((#u16)1);c=f(300);d=f((#u32)1);",
                       ["value:a", "value:b", "value:c", "value:d"], ["10", "20", "20", "30"])
            self.check(source + "a=f(1);", ["value:a"], status="ambiguous")
        self.check("&f(x:#u8){10}&f(x:#u8){10}a=f((#u8)1);", ["value:a"], status="ambiguous")
        self.check("&f(x:#u8){(#u8)1}&f(x:#u16){(#u16)2}a:#u8=f(1);", ["value:a"], status="ambiguous")
        self.check("&f(x:#u8){bad:#u8=300;bad}&f(x){7}a=f((#u8)1);", ["value:a"], status="contradictory")
        self.check("&f(x:#u8){missing()}&f(x){7}a=f((#u8)1);", ["value:a"], status="unresolved")
        self.check("&f(x:#u8){missing()}&f(x:#u16){20}a=f(300);", ["value:a"], ["20"])
        self.check("&f(x:#u8){0}a=f(300);", ["value:a"], status="contradictory")
        self.check("&~f(x:#u8){0}a=f(300);", ["value:a"], status="contradictory")

    def test_guarded_recursion_and_signature_queries(self):
        factorial = "&~fact(n:#u16){n==0 ? (#u16)1 : n*fact(n-1)}"
        self.check(factorial + "a=fact(6);", ["type:a", "value:a"], ["#u16", "720"])
        mutual = "&~even(n:#u8){n==0 ? (#u8)1 : odd(n-1)}&~odd(n:#u8){n==0 ? (#u8)0 : even(n-1)}"
        self.check(mutual + "a=even(8);b=odd(8);", ["value:a", "value:b"], ["1", "0"])
        self.check("&~f(n:#u8){n==0 ? (#u16)0 : f(n-1)+1}&^entry(n:#u8){f(n)}", ["type:entry"], ["#u16"])
        self.check("&~f(n:#u8)->#u16 {f(n)}a=f(1);", ["type:a"], ["#u16"])
        self.check("&~f(n:#u8)->#u16 {f(n)}a=f(1);", ["value:a"], status="unresolved")
        self.check("&~f(){f()*0}a=f();", ["value:a"], status="unresolved")
        self.check("&~f(n:#u8){n==0 ? (#u8)4 : missing()}a=f(0);", ["value:a"], ["4"])
        self.check("&~f(n:#u8){n==0 ? (#u8)4 : missing()}a=f(1);", ["value:a"], status="unresolved")
        with tempfile.TemporaryDirectory(prefix="vexel-recursive-budget-") as directory:
            source = Path(directory) / "main.vx"
            source.write_text("&~grow(n){grow(n+1)}a=grow(0);")
            report, = self.solve(source, "--query", "value:a", "--max-work", "5000")
            self.assertEqual(report["status"], "exhausted", report)
            self.assertNotIn("queries", report)

    def test_deferred_schema_captures_require_closure(self):
        # The dimension can be known before the dynamically discovered capture
        # support is closed. Omitting that support would forge a schema identity.
        self.check("offset:#u8=1;&size(x:#u8){x+offset}&size(x:#u16){x}"
                   "#P(data:#u8[size((#u8)1)]);p=#P([1,2]);", ["type:p"],
                   ["#P(data:#u8[2]){Captures(offset(1),type:offset(UInt(8)))}"])
        # A pure unknown must remain open; a declaration without an initializer
        # is persistent storage and is forbidden in a frozen schema parameter.
        self.check("offset:#u8=offset;&size(x:#u8){x+offset}&size(x:#u16){x}"
                   "#P(data:#u8[size((#u8)1)]);p=#P([1,2]);", ["type:p"], status="unresolved")
        self.check("offset:#u8;&size(x:#u8){x+offset}&size(x:#u16){x}"
                   "#P(data:#u8[size((#u8)1)]);p=#P([1,2]);", ["type:p"], status="contradictory")
        self.check("offset:#u8=1;&size(x:#u8){x+offset}&size(x:#u8){x}"
                   "#P(data:#u8[size((#u8)1)]);p=#P([1,2]);", ["type:p"], status="ambiguous")

    def test_local_state_versions_and_capture_reads(self):
        self.check("a={x:#u8=3;before=x+1;x+=4;after=x;x*=2;[before,after,x]};",
                   ["value:a"], ["Values(4,7,14)"])
        self.check("a={x:#u8;x=4;x+=2;x};", ["value:a"], ["6"])
        self.check("a={x:#u8=1;x=300;x};", ["value:a"], status="contradictory")
        self.check("a={x:#u8=1;&read(){x}before=read();x=9;after=read();[before,after]};",
                   ["value:a"], ["Values(1,9)"])
        self.check("a={x:#u8=1;&write(){x=9;x}write();x};", ["value:a"], ["9"])
        self.check("a=1 ? 7 : missing();b=0 ? missing() : 9;", ["value:a", "value:b"], ["7", "9"])
        self.check("&f(x:#u8){x+=1;x}a=f(255);", ["value:a"], ["0"])
        # Execution now establishes that this block has no value to bind.
        self.check("a={x=1;x=2};", ["value:a"], status="contradictory")
        self.check("a=(#b)0;b=(#b)9;c=(#b)(-1);", ["value:a", "value:b", "value:c"], ["0", "1", "1"])

    def test_structural_and_dependent_overloads(self):
        self.check("&f(x:#u8[2]){1}&f(x:#u16[2]){2}a=f([(#u8)1,(#u8)2]);", ["value:a"], ["1"])
        self.check("&f(x:#u8[2]){1}&f(x:#u16[2]){2}a=f([1,2]);", ["value:a"], status="ambiguous")
        self.check("&f(n:#u8,x:#u8[n]){7}&f(n:#u16,x:#u8[2]){9}a=f((#u8)2,[(#u8)1,(#u8)2]);", ["value:a"], ["7"])
        self.check("&f(n:#u8,x:#u8[n]){7}&f(n:#u16,x:#u8[2]){9}a=f((#u16)2,[(#u8)1,(#u8)2]);", ["value:a"], ["9"])
        self.check("#P(x:#u8);#Q(x:#u8);&f(p:#P){p.x}&f(p:#Q){p.x+1}a=f(#P(7));", ["value:a"], ["7"])

    def test_runtime_control_overloads_and_state_c_projection(self):
        with tempfile.TemporaryDirectory(prefix="vexel-source-state-") as directory:
            directory = Path(directory)
            source = directory / "source.vx"
            source.write_text("&bump(x:#u8){x+1}&bump(x:#u16){x+2}"
                              "&^answer(x:#u8){v:#u16=(#u16)x;old=v;v+=bump((#u16)3);"
                              "v=x<128 ? v*2 : v+1;[old,v]}")
            generated = directory / "generated"
            result = run([EXE, source, "--source", "answer", "--budget", "4", "--emit-c", generated])
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            library = directory / "check.so"
            compiled = run(["cc", "-std=c11", "-O2", "-shared", "-fPIC", "-I" + str(generated),
                            "-I" + str(REPO / "backends/msx-neo8/runtime"), generated / "vx_bridges.c",
                            REPO / "backends/msx-neo8/runtime/vx_runtime.c", "-o", library])
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            dll = ctypes.CDLL(str(library))
            dll.vx_answer.argtypes = [ctypes.c_uint8]
            dll.vx_answer_read.argtypes = [ctypes.c_uint16]
            dll.vx_answer_read.restype = ctypes.c_uint32
            for x in range(256):
                dll.vx_answer(x)
                self.assertEqual([dll.vx_answer_read(i) for i in range(2)],
                                 [x, (x + 5) * 2 if x < 128 else x + 6])

    def test_large_serial_and_simultaneous_output_arenas(self):
        with tempfile.TemporaryDirectory(prefix='vexel-output-packing-') as temporary:
            source = Path(temporary)/'source.vx'
            source.write_text(''.join('&^'+name+'(n:#u32){n}' for name in ('a','b','c','d')))
            for mode in ('serial','retained','overlap'):
                for schedule, branch in ORDERS:
                    with self.subTest(mode=mode,schedule=schedule,branch=branch):
                        flags = [part for name in ('a','b','c','d') for part in ('--source',name)]
                        report, = self.solve(source,*flags,'--mode',mode,'--budget','16',
                                             '--max-decisions','1000',schedule=schedule,branch=branch)
                        self.assertEqual(report['status'],'resolved',report)
                        self.assertEqual(report['extent'],4 if mode=='serial' else 16)
                        self.assertEqual([item['element_bits'] for item in report['sources']],[32]*4)
                        self.assertEqual([item['offset'] for item in report['sources']],
                                         [0]*4 if mode=='serial' else [0,4,8,12])
            source.write_text('&^a(n:#u32){n}&^b(n:#u32){}&^c(n:#u32){n}')
            report, = self.solve(source,'--source','a','--source','b','--source','c',
                                 '--mode','retained','--budget','8')
            self.assertEqual(report['status'],'resolved',report)
            self.assertEqual([item['offset'] for item in report['sources']],[0,0,4])

    def test_source_representation_and_schedule_independence(self):
        logical = None
        for mode, (schedule, branch) in itertools.product(("serial", "overlap", "retained"), ORDERS):
            report, = self.solve(EXAMPLE, "--source", "first", "--source", "second", "--mode", mode, "--budget", "6",
                                 schedule=schedule, branch=branch)
            self.assertEqual(report["status"], "resolved", report)
            sources = report["sources"]
            self.assertEqual([s["element_bits"] for s in sources], [16, 16] if mode == "serial" else [8, 8])
            self.assertEqual([s["offset"] for s in sources], [0, 0] if mode == "serial" else [0, 3])
            self.assertEqual(report["extent"], 6)
            types = [s["logical_type"] for s in sources]
            logical = types if logical is None else logical
            self.assertEqual(types, logical)
            self.assertTrue(all("data:#u16[3]" in t for t in types))

    def test_late_target_contract_rebuild_and_closed_emission(self):
        reports = self.solve(EXAMPLE, "--query", "type:first.data", "--then", "--source", "first", "--source", "second",
                             "--budget", "6", "--mode", "serial", "--then", "--mode", "overlap")
        self.assertEqual([r["status"] for r in reports], ["resolved"] * 3)
        self.assertEqual([r["queries"][0]["answer"] for r in reports], ["#u16[3]"] * 3)
        self.assertEqual([r["sources"][0]["element_bits"] for r in reports[1:]], [16, 8])
        for flags, expected in ((["--mode", "open"], "unresolved"), (["--mode", "overlap", "--budget", "5"], "contradictory"),
                                (["--max-work", "1"], "exhausted")):
            report, = self.solve(EXAMPLE, "--source", "first", "--source", "second", "--budget", "6", *flags)
            self.assertEqual(report["status"], expected, report)
            self.assertNotIn("sources", report)

    def test_embedded_constant_does_not_widen_stored_payload(self):
        with tempfile.TemporaryDirectory(prefix="vexel-constant-payload-") as directory:
            source = Path(directory) / "source.vx"
            source.write_text("#P(tag:#u16,data:#u16[3]); &^a(x:#u8){#P(1000,[(#u16)x,(#u16)x,(#u16)x])}"
                              "&^b(x:#u8){#P(2000,[(#u16)x,(#u16)x,(#u16)x])}")
            report, = self.solve(source, "--source", "a", "--source", "b", "--mode", "overlap", "--budget", "6")
            self.assertEqual(report["status"], "resolved", report)
            self.assertEqual([s["element_bits"] for s in report["sources"]], [8, 8])
            self.assertEqual([s["leaves"][0]["value"] for s in report["sources"]], ["1000", "2000"])
            self.assertTrue(all(not s["leaves"][0]["stored"] for s in report["sources"]))
            source.write_text("&^constant(){4294967295}")
            generated = Path(directory) / "constant"
            result = run([EXE, source, "--source", "constant", "--budget", "0", "--emit-c", generated])
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)["extent"], 0)
            self.assertNotIn("vx_payload", (generated / "vx_bridges.c").read_text())

    def test_generated_boundary_checks_and_no_unclosed_emission(self):
        with tempfile.TemporaryDirectory(prefix="vexel-boundary-contract-") as directory:
            directory = Path(directory)
            source = directory / "source.vx"
            source.write_text("&^answer([[range(10,20)]] int:#u8){[(#u16)int+1]}")
            generated = directory / "generated"
            result = run([EXE, source, "--source", "answer", "--budget", "1", "--emit-c", generated])
            self.assertEqual(result.returncode, 0, result.stderr)
            harness = directory / "check.c"
            harness.write_text('''#include "vexel.h"
#include <stdlib.h>
void vx_trap(uint8_t reason) {exit(reason);}
int main(int argc,char** argv) {
    (void)argc; vx_answer((uint8_t)atoi(argv[1]));
    return vx_answer_read((uint16_t)atoi(argv[2])) == 16 ? 0 : 1;
}
''')
            executable = directory / "check"
            compiled = run(["cc", "-std=c11", "-O2", "-I" + str(generated), "-I" + str(REPO / "backends/msx-neo8/runtime"),
                            generated / "vx_bridges.c", harness, "-o", executable])
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            for value, index, expected in ((15, 0, 0), (9, 0, 16), (21, 0, 16), (15, 1, 16)):
                self.assertEqual(run([executable, value, index]).returncode, expected)
            blocked = directory / "blocked"
            result = run([EXE, source, "--source", "answer", "--mode", "open", "--emit-c", blocked])
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(blocked.exists())
            original = (generated / "vx_bridges.c").read_bytes()
            result = run([EXE, source, "--source", "answer", "--emit-c", generated])
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual((generated / "vx_bridges.c").read_bytes(), original)

    def test_runtime_projection_preserves_wrapping_and_logical_width(self):
        with tempfile.TemporaryDirectory(prefix="vexel-source-c-") as directory:
            directory = Path(directory)
            source = directory / "source.vx"
            source.write_text("&^answer(int:#u8){[(#u16)int+1,(#u16)int*2]} &^wrap(x:#u8){[x+1,x-1,x*2]}")
            generated = directory / "generated"
            result = run([EXE, source, "--source", "answer", "--source", "wrap", "--mode", "serial", "--budget", "6", "--emit-c", generated])
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(result.stdout)
            self.assertEqual(report["sources"][0]["element_bits"], 16)
            library = directory / "check.so"
            compiled = run(["cc", "-std=c11", "-O2", "-shared", "-fPIC", "-I" + str(generated),
                            "-I" + str(REPO / "backends/msx-neo8/runtime"), generated / "vx_bridges.c",
                            REPO / "backends/msx-neo8/runtime/vx_runtime.c", "-o", library])
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            dll = ctypes.CDLL(str(library))
            for name in ("answer", "wrap"):
                getattr(dll, "vx_" + name).argtypes = [ctypes.c_uint8]
                getattr(dll, "vx_" + name + "_read").argtypes = [ctypes.c_uint16]
                getattr(dll, "vx_" + name + "_read").restype = ctypes.c_uint32
            for x in range(256):
                dll.vx_answer(x)
                self.assertEqual([dll.vx_answer_read(i) for i in range(2)], [x + 1, x * 2])
                dll.vx_wrap(x)
                self.assertEqual([dll.vx_wrap_read(i) for i in range(3)], [(x + 1) % 256, (x - 1) % 256, (x * 2) % 256])


if __name__ == "__main__":
    unittest.main(verbosity=2)
