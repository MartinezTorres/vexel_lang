#!/usr/bin/env python3
"""One demand closure crosses calls, captures and loop-carried state."""
import ctypes
from pathlib import Path
import tempfile
import unittest
import test_result_demand as support


class InterfaceDemandTests(unittest.TestCase):
    project = support.ResultDemandTests.project
    execute = support.ResultDemandTests.execute
    simulate = support.ResultDemandTests.simulate

    def test_forwarding_loop_and_result_close_together(self):
        source = (support.support.ROOT / "examples/interface_demand.vx").read_text()
        for options in ((), ("--schedule", "lifo", "--branch-order", "reverse")):
            with self.subTest(options=options), tempfile.TemporaryDirectory() as temp:
                d = Path(temp)
                generated, report = self.project(d, source, *options)
                self.assertEqual(len(report["runtime_functions"]), 2)
                for fn in report["runtime_functions"]:
                    self.assertEqual(fn["logical_argument_leaves"], 3)
                    self.assertEqual(fn["transported_arguments"], [0, 2])
                    self.assertEqual(fn["transported_logical_leaves"], [0])
                    self.assertEqual(fn["transported_result_bytes"], 1)
                loops = report["interface_demand"]["loops"]
                self.assertEqual(len(loops), 1)
                self.assertLess(loops[0]["retained_carried_leaves"], loops[0]["logical_carried_leaves"])
                self.assertFalse(loops[0]["iteration_counter"])
                oracle = lambda n: (n+3*(n&3))&255
                self.execute(d, generated, report, oracle)
                self.execute(d, generated, report, oracle, split=True)
                if support.MSX and not options:
                    self.simulate(d, generated, expected="((n+3*(n&3))&255)")

    def test_sparse_capture_inputs(self):
        source = '&^entry(n:#u8){x:#u8=n;y:#u16=(#u16)n*257;'
        source += '&~step(){x+=1;y+=300;}step();x}'
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            generated, report = self.project(d, source)
            fn = report["runtime_functions"][0]
            self.assertEqual(fn["logical_capture_leaves"], 2)
            self.assertEqual(len(fn["transported_captures"]), 1)
            self.execute(d, generated, report, lambda n: (n+1)&255)

    def test_recursive_forwarding_cycle_has_no_own_demand(self):
        source = '&~walk(n:#u8,dead:#u16)->#u8{n==0?(#u8)7:walk(n-1,dead+300)+1}'
        source += '&^entry(n:#u8){walk(n,(#u16)n*257)}'
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            generated, report = self.project(d, source)
            self.assertEqual(report["runtime_functions"][0]["transported_arguments"], [0])
            self.execute(d, generated, report, lambda n: (n+7)&255)

    def test_call_and_loop_traps_remain_consumers(self):
        cases = [
            '&~ignore(a:#u8,b:#u8){a}&^entry(n:#u8){ignore(n,(#u8)(1/n))}',
            '&~loop(n:#u8)->#u8{i:#u8=0;x:#u8=n;(i<2)!@{x=(#u8)(1/n);i+=1;};n}'
            '&^entry(n:#u8){loop(n)}',
        ]
        for source in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory() as temp:
                d = Path(temp)
                generated, report = self.project(d, source)
                self.execute(d, generated, report, lambda n: n if n else (1,))

    def test_discarded_argument_keeps_foreign_call(self):
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            generated, report = self.project(d, '&!observe(n:#u8)->#u8;'
                '&~first(a:#u8,b:#u8){a}&^entry(n:#u8){first(n,observe(n))}')
            self.assertEqual(report["runtime_functions"][0]["transported_arguments"], [])
            dll = self.execute(d, generated, report, lambda n: n,
                'unsigned calls;\nvoid vx_external_observe(uint32_t *out,uint8_t n){++calls;out[0]=n;}\n')
            self.assertEqual(ctypes.c_uint.in_dll(dll, "calls").value, 256)

    def test_mixed_width_results_and_bank_continuations(self):
        source = '&~mixed(n:#u8)->(#i8,#u16,#u32){((#i8)(n-128),(#u16)n+500,(#u32)n*100000)}'
        source += '&^entry(n:#u8){a,b,c=mixed(n);(#u32)((#i32)a+128)+(#u32)b+c}'
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            generated, report = self.project(d, source, "--prefer", "bank_chunk_operations=1")
            fn = report["runtime_functions"][0]
            self.assertEqual(fn["transported_result_bytes"], 7)
            self.assertTrue(any(m["name"].startswith("vx_chunk_") for m in report["bank_modules"]))
            self.execute(d, generated, report, lambda n: n*100002+500)
            self.execute(d, generated, report, lambda n: n*100002+500, split=True)
            if support.MSX:
                self.simulate(d, generated, expected="expected", setup="uint32_t expected=500;", advance="expected+=100002UL;")

    def test_completion_only_cycle_keeps_calls(self):
        # The external pulse supplies a bounded test escape. The compiler has
        # no such contract and must retain the otherwise nonterminating cycle.
        source = '&!pulse();&~again(n:#u8)->#u8{pulse();again(n)}&^entry(n:#u8){again(n);n}'
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            generated, report = self.project(d, source)
            fn = report["runtime_functions"][0]
            self.assertEqual(fn["transported_arguments"], [])
            self.assertEqual(fn["transported_result_slots"], 0)
            dll = self.execute(d, generated, report, lambda n: (73,),
                'unsigned calls;\nvoid vx_external_pulse(uint32_t *out)'
                '{(void)out;++calls;if((calls&3)==0)vx_trap(73);}\n')
            self.assertEqual(ctypes.c_uint.in_dll(dll, "calls").value, 1024)

    def test_loop_parallel_copies_readiness_and_index(self):
        cases = [
            ('&^entry(n:#u8){x:#u8=n;y:#u8=n+1;i:#u8=0;'
             '(i<3)!@{x,y=(y,x);i+=1;};x}', lambda n: (n+1)&255),
            ('&^entry(n:#u8){x:#u8;i:#u8=0;(i<2)!@{n<128?{x=n+i;};i+=1;};'
             'n<128?x:(#u8)0}', lambda n: n+1 if n<128 else 0),
            ('&^entry(n:#u8){x:#u8=n;[1,2,3]!@{x+=_;};x}', lambda n: (n+6)&255),
        ]
        for source, oracle in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory() as temp:
                d = Path(temp)
                generated, report = self.project(d, source)
                self.execute(d, generated, report, oracle)


if __name__ == "__main__":
    unittest.main(verbosity=2)
