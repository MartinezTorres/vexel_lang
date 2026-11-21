#!/usr/bin/env python3
"""Consumer demand crosses calls; effects, traps and completion stay mandatory."""
import ctypes
import importlib.util
import json
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import test_tuples as support

MSX = "--msx" in sys.argv
if MSX:
    sys.argv.remove("--msx")


class ResultDemandTests(unittest.TestCase):
    def project(self, directory, source, *options):
        path = directory / "source.vx"
        path.write_text(source)
        generated = directory / "generated"
        run = support.run([support.EXE, path, "--source", "entry", "--budget", 8,
                           "--emit-c", generated, *options])
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        return generated, json.loads(run.stdout)

    def execute(self, directory, generated, report, oracle, bindings="", split=False):
        harness = directory / "harness.c"
        harness.write_text('#include "vexel.h"\n#include <setjmp.h>\n'
            'static jmp_buf trapped;\nvoid vx_trap(uint8_t reason){longjmp(trapped,reason);}\n'
            'unsigned checked(uint8_t n){int reason=setjmp(trapped);if(reason)return reason;vx_entry(n);return 0;}\n'
            + bindings)
        rt = support.ROOT / "backends/msx-neo8/runtime"
        runtime = directory / "runtime.c"
        runtime.write_text((rt / "vx_runtime.c").read_text().replace(
            'void vx_trap(uint8_t reason)', 'void vx_unused_trap(uint8_t reason)', 1))
        files = [generated / "vx_bridges.c"] if not split else [generated / "vx_banked.c", *[
            generated / (module["module"] + ".c") for module in report["bank_modules"]]]
        library = directory / ("split.so" if split else "whole.so")
        run = support.run(["cc", "-std=c11", "-O2", "-shared", "-fPIC", "-fsanitize=undefined",
            "-fno-sanitize-recover=all", "-I"+str(generated), "-I"+str(rt), *files, runtime, harness, "-o", library])
        self.assertEqual(run.returncode, 0, run.stderr)
        dll = ctypes.CDLL(str(library))
        dll.checked.argtypes = [ctypes.c_uint8]
        dll.checked.restype = ctypes.c_uint
        dll.vx_entry_read.argtypes = [ctypes.c_uint16]
        dll.vx_entry_read.restype = ctypes.c_uint32
        for n in range(256):
            expected = oracle(n)
            reason = dll.checked(n)
            if isinstance(expected, tuple):
                self.assertEqual(reason, expected[0], n)
            else:
                self.assertEqual(reason, 0, n)
                self.assertEqual(dll.vx_entry_read(0), expected, n)
        return dll

    def test_transitive_sparse_demand_and_order(self):
        declarations = ['&~pair(n:#u8)->(#u8,#u16){(n,(#u16)n+300)}',
                        '&~relay(n:#u8)->(#u8,#u16){pair(n)}',
                        '&^entry(n:#u8){relay(n).__1}']
        for schedule, branch, source in (("fifo", "forward", declarations),
                                         ("lifo", "reverse", declarations[::-1])):
            with self.subTest(schedule=schedule), tempfile.TemporaryDirectory() as temp:
                d = Path(temp)
                generated, report = self.project(d, '\n'.join(source), "--schedule", schedule, "--branch-order", branch)
                functions = report["runtime_functions"]
                self.assertEqual(len(functions), 2)
                self.assertEqual([f["transported_logical_leaves"] for f in functions], [[1], [1]])
                self.assertEqual([f["logical_result_slots"] for f in functions], [2, 2])
                code = (generated / "vx_bridges.c").read_text()
                self.assertNotIn("vxo->v1 =", code)
                self.execute(d, generated, report, lambda n: n+300)
                self.execute(d, generated, report, lambda n: n+300, split=True)
                if MSX and schedule == "fifo":
                    self.simulate(d, generated)

    def test_union_of_callers(self):
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            generated, report = self.project(d, '&~pair(n:#u8){(n,(#u16)n+300)}'
                '&^entry(n:#u8){pair(n).__0+pair(n+1).__1}')
            self.assertEqual(len(report["runtime_functions"]), 1)
            self.assertEqual(report["runtime_functions"][0]["transported_logical_leaves"], [1])
            self.assertEqual(report["interface_demand"]["discarded_calls"], 1)
            self.assertEqual(len(re.findall(r'^\s+vx_flow_\d+\(', (generated / "vx_bridges.c").read_text(), re.M)), 1)
            self.execute(d, generated, report, lambda n: n+((n+1)&255)+300)
            self.execute(d, generated, report, lambda n: n+((n+1)&255)+300, split=True)
            if MSX:
                self.simulate(d, generated, expected="n+((n+1)&255)+300")
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            generated, report = self.project(d, '&~pair(n:#u8){(n,(#u16)n+300)}'
                '&^entry(n:#u8){pair(n).__0}&^other(n:#u8){pair(n).__1}', "--source", "other")
            dll = self.execute(d, generated, report, lambda n: n)
            dll.vx_other.argtypes = [ctypes.c_uint8]
            dll.vx_other_read.argtypes = [ctypes.c_uint16]
            dll.vx_other_read.restype = ctypes.c_uint32
            for n in (0, 19, 255):
                dll.vx_other(n)
                self.assertEqual(dll.vx_other_read(0), n+300)

    def test_captures_and_recursive_fixed_point(self):
        cases = [
            ('&^entry(n:#u8){x:#u8=n;y:#u8=n;&~bump(){x+=1;y+=2;}bump();y}', [2], lambda n: (n+2)&255),
            ('&~walk(n:#u8)->(#u8,#u8){n==0?((#u8)0,(#u8)7):'
             '{p=walk(n-1);(p.__0+1,p.__1+2)}}&^entry(n:#u8){walk(n).__1}', [1], lambda n: (7+2*n)&255),
        ]
        for source, leaves, oracle in cases:
            with self.subTest(leaves=leaves), tempfile.TemporaryDirectory() as temp:
                d = Path(temp)
                generated, report = self.project(d, source)
                self.assertEqual(report["runtime_functions"][0]["transported_logical_leaves"], leaves)
                self.execute(d, generated, report, oracle)

    def test_unused_value_keeps_traps_and_completion(self):
        for result, expected_leaves in (("checked(n).__0", []), ("checked(n);n", [])):
            with self.subTest(result=result), tempfile.TemporaryDirectory() as temp:
                d = Path(temp)
                generated, report = self.project(d, '&~checked(n:#u8){(n,(#u8)(1/n))}'
                    '&^entry(n:#u8){'+result+'}')
                self.assertEqual(report["runtime_functions"][0]["transported_logical_leaves"], expected_leaves)
                self.execute(d, generated, report, lambda n: n if n else (1,))
                if not expected_leaves:
                    # A completion-only native call now has no result pointer.
                    self.assertRegex((generated / "vx_bridges.c").read_text(), r'vx_flow_\d+\(vxp_')
                    if MSX:
                        self.simulate(d, generated, expected="n", first=1)

    def test_foreign_boundary_retains_validation_and_effects(self):
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            generated, report = self.project(d, '&!observe(n:#u8)->(#u8,#u8);'
                '&~forward(n:#u8){observe(n)}&^entry(n:#u8){forward(n).__0}')
            self.assertEqual(report["runtime_functions"][0]["transported_logical_leaves"], [0])
            dll = self.execute(d, generated, report, lambda n: n if n != 13 else (16,),
                'unsigned calls;\nvoid vx_external_observe(uint32_t *out,uint8_t n)'
                '{++calls;out[0]=n;out[1]=n==13?999:7;}\n')
            self.assertEqual(ctypes.c_uint.in_dll(dll, "calls").value, 256)

    def test_lazy_initializer_is_a_consumer(self):
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            generated, report = self.project(d, '&!seed()->#u8;'
                '&~pair(n:#u8){(n,n+1)}state:#u8=pair(seed()).__1;&^entry(n:#u8){state}')
            self.assertEqual(report["runtime_functions"][0]["transported_logical_leaves"], [1])
            dll = self.execute(d, generated, report, lambda n: 38,
                'unsigned calls;\nvoid vx_external_seed(uint32_t *out){++calls;out[0]=37;}\n')
            self.assertEqual(ctypes.c_uint.in_dll(dll, "calls").value, 1)

    def test_total_execution_closes_across_native_boundaries(self):
        source='&~zero(n:#u8){n-n}&~relay(n:#u8){zero(n)+n}&^entry(n:#u8){relay(n)}'
        for schedule,branch in (("fifo","forward"),("lifo","reverse")):
            with self.subTest(schedule=schedule), tempfile.TemporaryDirectory() as temp:
                d=Path(temp)
                generated,report=self.project(d,source,"--schedule",schedule,"--branch-order",branch)
                self.assertTrue(all(f["discardable_execution"] for f in report["runtime_functions"]))
                self.assertEqual(report["interface_demand"]["discarded_calls"],2)
                self.assertFalse(re.search(r'^\s+vx_flow_\d+\(', (generated/"vx_bridges.c").read_text(),re.M))
                self.assertTrue(report["sources"][0]["value_projection"]["available"])
                dll=self.execute(d,generated,report,lambda n:n,
                    '#include "vx_value_entry.h"\nunsigned projected(uint8_t n){vx_entry_result r;vx_entry_value(&r,n);return r.v0;}')
                dll.projected.argtypes=[ctypes.c_uint8];dll.projected.restype=ctypes.c_uint
                for n in range(256):self.assertEqual(dll.projected(n),n)
                self.execute(d,generated,report,lambda n:n,split=True)
                if MSX and schedule=="fifo":self.simulate(d,generated,expected="n",
                    includes='#include "vx_value_entry.h"\n',setup='vx_entry_result direct;',
                    advance='vx_entry_value(&direct,(uint8_t)n);if(direct.v0!=n){vx_last_result=2;break;}')

    def test_unused_total_call_preserves_argument_traps_and_capture_observations(self):
        cases=[
            ('&~ignore(n:#u8){n-n}&^entry(n:#u8){ignore((#u8)(1/n));n}',0,lambda n:n if n else (1,)),
            ('&^entry(n:#u8){x=n;&~bump(){x+=1;}bump();n}',1,lambda n:n),
            ('&^entry(n:#u8){x=n;&~bump(){x+=1;}bump();x}',0,lambda n:(n+1)&255),
        ]
        for source,removed,oracle in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory() as temp:
                d=Path(temp);generated,report=self.project(d,source)
                self.assertTrue(all(f["discardable_execution"] for f in report["runtime_functions"]))
                self.assertEqual(report["interface_demand"]["discarded_calls"],removed)
                self.execute(d,generated,report,oracle)

    def test_unknown_completion_and_foreign_effects_are_not_total(self):
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp)
            _,report=self.project(d,'&~spin(n:#u8)->#u8{spin(n)}&^entry(n:#u8){spin(n);n}')
            self.assertFalse(report["runtime_functions"][0]["discardable_execution"])
            self.assertEqual(report["interface_demand"]["discarded_calls"],0)
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp)
            generated,report=self.project(d,'&!observe(n:#u8);&~touch(n:#u8){observe(n);n}'
                '&^entry(n:#u8){touch(n);n}')
            self.assertFalse(report["runtime_functions"][0]["discardable_execution"])
            self.assertEqual(report["interface_demand"]["discarded_calls"],0)
            dll=self.execute(d,generated,report,lambda n:n,
                'unsigned calls;void vx_external_observe(uint32_t *out,uint8_t n){(void)out;(void)n;++calls;}')
            self.assertEqual(ctypes.c_uint.in_dll(dll,"calls").value,256)

    def test_arithmetic_domains_certify_completion(self):
        expressions = ('n/3', 'n%3', 'n/(n|1)', 'n%(n|1)', 'n<<(n&7)',
                       'n>>(n&7)', '(#i8)n/(-1)', '(#i8)n%(-1)', '(#i8)n>>(n&7)')
        source = '&~total(n:#u8){'+''.join(e+';' for e in expressions)+'n}'
        source += '&~relay(n:#u8){total(n)}&^entry(n:#u8){relay(n);n}'
        for schedule, branch in (("fifo", "forward"), ("lifo", "reverse")):
            with self.subTest(schedule=schedule), tempfile.TemporaryDirectory() as temp:
                d=Path(temp)
                generated,report=self.project(d,source,"--schedule",schedule,"--branch-order",branch)
                self.assertTrue(all(f['discardable_execution'] for f in report['runtime_functions']))
                self.assertEqual(report['interface_demand']['discarded_calls'],2)
                code=(generated/'vx_bridges.c').read_text()
                self.assertNotIn('(void)vx_divmod(',code)
                self.assertNotIn('(void)vx_shift(',code)
                self.execute(d,generated,report,lambda n:n)
                self.execute(d,generated,report,lambda n:n,split=True)
                if MSX and schedule=='fifo':self.simulate(d,generated,expected='n')

    def test_total_outer_arithmetic_retains_operand_traps(self):
        cases = [('(1/n)/3',lambda n:n if n else (1,)),
                 ('n/(n&7)',lambda n:n if n&7 else (1,)),
                 ('n<<(n&15)',lambda n:n if (n&15)<8 else (2,)),
                 ('(n<<n)>>1',lambda n:n if n<8 else (2,)),
                 ('n/(#u8)((#u16)n+1)',lambda n:n if n!=255 else (1,))]
        for expression,oracle in cases:
            with self.subTest(expression=expression), tempfile.TemporaryDirectory() as temp:
                d=Path(temp)
                generated,report=self.project(d,'&~checked(n:#u8){'+expression+';n}'
                    '&^entry(n:#u8){checked(n);n}')
                self.assertFalse(report['runtime_functions'][0]['discardable_execution'])
                self.assertEqual(report['interface_demand']['discarded_calls'],0)
                self.execute(d,generated,report,oracle)
                self.execute(d,generated,report,oracle,split=True)

    def test_narrow_results_preserve_wide_trap_operands(self):
        cases = [
            ('&~checked(count:#u32)->#u8{(#u8)((#u32)1<<count)}'
             '&^entry(n:#u8){checked((#u32)n*256);n}', lambda n: 0 if n==0 else (2,), True),
            ('&~divide(d:#u32)->#u32{(#u32)1000/d}'
             '&^entry(n:#u8){(#u8)divide(((#u32)n+1)*256)}', lambda n: 1000//((n+1)*256), False),
        ]
        for source,oracle,trapping in cases:
            with self.subTest(trapping=trapping), tempfile.TemporaryDirectory() as temp:
                d=Path(temp);generated,report=self.project(d,source)
                self.assertEqual(report['runtime_functions'][0]['demanded_bits']['arguments'],[32])
                self.execute(d,generated,report,oracle)
                self.execute(d,generated,report,oracle,split=True)
                if MSX:
                    if trapping:
                        self.simulate(d,generated,expected='n',setup='vx_last_result=0;vx_entry(1);',expected_fault=2)
                    else:self.simulate(d,generated,expected='(n==0?3:n<3?1:0)')

    def test_caller_envelopes_certify_private_arithmetic(self):
        cases = [
            ('&~checked(d:#u16){(#u16)1000/d}'
             '&~relay(d:#u16){checked(d)}'
             '&^entry(n:#u8){relay((#u16)n+1);n}', 2),
            ('&~shift(k:#u16){(#u8)1<<k}'
             '&^entry(n:#u8){shift((#u16)(n&7));n}', 1),
            ('&^entry(n:#u8){d:#u16=(#u16)n+1;'
             '&~checked(){(#u16)1000/d}checked();n}', 1),
        ]
        for source, removed in cases:
            for schedule, branch in (("fifo", "forward"), ("lifo", "reverse")):
                with self.subTest(source=source, schedule=schedule), tempfile.TemporaryDirectory() as temp:
                    d=Path(temp);generated,report=self.project(d,source,"--schedule",schedule,"--branch-order",branch)
                    self.assertTrue(all(f['discardable_execution'] for f in report['runtime_functions']))
                    self.assertEqual(report['interface_demand']['discarded_calls'],removed)
                    self.execute(d,generated,report,lambda n:n)
                    self.execute(d,generated,report,lambda n:n,split=True)
                    if MSX and schedule=='fifo':self.simulate(d,generated,expected='n')

    def test_guarded_arithmetic_completion_keeps_conditions_and_effects(self):
        for expression in ('n!=0?(#u8)(100/n):(#u8)0',
                           'n<8?((#u8)1<<n):(#u8)0',
                           'n!=0&&((100/n)>0)'):
            with self.subTest(expression=expression), tempfile.TemporaryDirectory() as temp:
                d=Path(temp);generated,report=self.project(d,
                    '&~checked(n:#u8){'+expression+'}&^entry(n:#u8){checked(n);n}')
                self.assertTrue(report['runtime_functions'][0]['discardable_execution'])
                self.assertEqual(report['interface_demand']['discarded_calls'],1)
                self.execute(d,generated,report,lambda n:n)
                self.execute(d,generated,report,lambda n:n,split=True)
                if MSX:self.simulate(d,generated,expected='n')
        for expression,oracle in (
                ('n!=0?(#u8)(100/(n-n)):(#u8)0',lambda n:n if not n else (1,)),
                ('(100/n)!=0?(#u8)1:(#u8)0',lambda n:n if n else (1,))):
            with self.subTest(expression=expression), tempfile.TemporaryDirectory() as temp:
                d=Path(temp);generated,report=self.project(d,
                    '&~checked(n:#u8){'+expression+'}&^entry(n:#u8){checked(n);n}')
                self.assertFalse(report['runtime_functions'][0]['discardable_execution'])
                self.assertEqual(report['interface_demand']['discarded_calls'],0)
                self.execute(d,generated,report,oracle)
                self.execute(d,generated,report,oracle,split=True)
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp);generated,report=self.project(d,
                '&!observe(n:#u8);&~checked(n:#u8){n!=0?{observe(n);(#u8)(100/n)}:(#u8)0}'
                '&^entry(n:#u8){checked(n);n}')
            self.assertFalse(report['runtime_functions'][0]['discardable_execution'])
            self.assertEqual(report['interface_demand']['discarded_calls'],0)
            dll=self.execute(d,generated,report,lambda n:n,
                'unsigned calls;void vx_external_observe(uint32_t *out,uint8_t n){(void)out;(void)n;++calls;}')
            self.assertEqual(ctypes.c_uint.in_dll(dll,'calls').value,255)

    def test_unused_safe_result_drops_its_argument_and_arithmetic(self):
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp);generated,report=self.project(d,
                '&~pair(a:#u8,d:#u16){(a+1,(#u16)1000/d)}'
                '&^entry(n:#u8){pair(n,(#u16)n+1).__0}')
            fn=report['runtime_functions'][0]
            self.assertEqual(fn['transported_arguments'],[0])
            self.assertEqual(fn['transported_logical_leaves'],[0])
            self.assertNotIn('vx_divmod(', (generated/'vx_bridges.c').read_text())
            self.execute(d,generated,report,lambda n:(n+1)&255)
            self.execute(d,generated,report,lambda n:(n+1)&255,split=True)
            if MSX:self.simulate(d,generated,expected='((n+1)&255)')

    def test_completion_envelopes_include_every_caller_and_argument(self):
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp);generated,report=self.project(d,
                '&~checked(d:#u16){(#u16)1000/d}'
                '&^entry(n:#u8){checked((#u16)n);n}'
                '&^safe(n:#u8){checked((#u16)n+1);n}', '--source', 'safe')
            self.assertFalse(report['runtime_functions'][0]['discardable_execution'])
            self.assertEqual(report['interface_demand']['discarded_calls'],0)
            self.execute(d,generated,report,lambda n:n if n else (1,))
            self.execute(d,generated,report,lambda n:n if n else (1,),split=True)
            if MSX:self.simulate(d,generated,expected='n',setup='vx_last_result=0;vx_entry(0);',expected_fault=1)
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp);generated,report=self.project(d,
                '&~checked(d:#u16){(#u16)1000/d}'
                '&^entry(n:#u8){checked(((#u16)100/n)|1);n}')
            self.assertTrue(report['runtime_functions'][0]['discardable_execution'])
            # The callee is harmless; evaluating its argument still can trap.
            self.assertEqual(report['interface_demand']['discarded_calls'],0)
            self.execute(d,generated,report,lambda n:n if n else (1,))
            self.execute(d,generated,report,lambda n:n if n else (1,),split=True)
            if MSX:self.simulate(d,generated,expected='n',setup='vx_last_result=0;vx_entry(0);',expected_fault=1)

    def test_record_metadata_is_not_execution(self):
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            generated, report = self.project(d, '#Invoke(CallValue:#u8,After:#u8);'
                '&~pair(n:#u8){#Invoke((#u8)n,(#u8)(n+1))}&^entry(n:#u8){'
                'p=#Invoke((#u8)n,(#u8)n);i:#u8=0;(i<2)!@{p=pair(p.After);i+=1;};p.After}')
            self.execute(d, generated, report, lambda n: (n+2)&255)

    def simulate(self, directory, generated, expected="n+300", first=0, setup="", advance="", includes="", expected_fault=0):
        spec = importlib.util.spec_from_file_location("backend", support.ROOT / "experimental/relational/project_msx.py")
        backend = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(backend)
        project = directory / "msx"
        backend.package(generated, project, directory / "source.vx", support.EXE)
        (project / "user/main.c").write_text('''#include "vexel.h"
INCLUDES
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    uint16_t n;
    SETUP
    for(n=FIRST;n<256;++n){vx_entry((uint8_t)n);if(vx_entry_read(0)!=EXPECTED){vx_last_result=1;break;}ADVANCE}
    __asm halt __endasm;
}
'''.replace("FIRST", str(first)).replace("EXPECTED", expected).replace("SETUP", setup).replace("ADVANCE", advance).replace("INCLUDES",includes))
        targets = ['all', 'build/neo8-runner'] if expected_fault else ['simulate', 'SIMFLAGS=--max-ticks=400000000']
        run = subprocess.run(["make", "-C", str(project), "-j2", *targets],
                             text=True, capture_output=True, timeout=240)
        self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
        if expected_fault:
            # The production simulate command rejects every trap. Exercise the
            # same machine directly for this deliberately faulting workload.
            spec=importlib.util.spec_from_file_location('simulator',project/'tools/simulate.py')
            simulator=importlib.util.module_from_spec(spec);spec.loader.exec_module(simulator)
            symbols=simulator.symbols(project/'build/game.rom.symbols.map')
            plan=json.loads((project/'generated/plan.json').read_text())
            run=support.run([project/'build/neo8-runner','--rom',project/'build/game.rom',
                '--max-ticks','400000000','--stack-floor',str(plan['resident_limit']),
                '--shadow',str(symbols['_vx_segment_b']),'--fault',str(symbols['_vx_fault'])])
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            observation=json.loads(run.stdout)
        else:
            observation = json.loads((project / "build/execution.json").read_text())["observation"]
            self.assertEqual(observation["last_result"], 0)
        self.assertEqual(observation["fault"], expected_fault)


if __name__ == "__main__":
    unittest.main(verbosity=2)
