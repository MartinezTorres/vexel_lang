#!/usr/bin/env python3
"""Immutable facts remove transport; mutable snapshots and execution survive."""
import ctypes
from pathlib import Path
import tempfile
import unittest
import test_result_demand as support


class ValueFactsTests(unittest.TestCase):
    project = support.ResultDemandTests.project
    execute = support.ResultDemandTests.execute
    simulate = support.ResultDemandTests.simulate

    def test_alias_chain_keeps_effects_and_materialized_calls(self):
        declarations = ['&!observe(n:#u8);',
                        '&~identity(n:#u8)->#u8{observe(n);n}',
                        '&~forward(n:#u8)->#u8{identity(n)}',
                        '&^entry(n:#u8){forward(n)}']
        for options in ((), ('--schedule', 'lifo', '--branch-order', 'reverse')):
            with self.subTest(options=options), tempfile.TemporaryDirectory() as temp:
                d = Path(temp)
                generated, report = self.project(d, ''.join(declarations), *options)
                self.assertTrue(all(f['transported_result_slots'] == 0 for f in report['runtime_functions']))
                dll = self.execute(d, generated, report, lambda n: n,
                    'unsigned calls;\nvoid vx_external_observe(uint32_t *out,uint8_t n)'
                    '{(void)out;(void)n;++calls;}\n', split=True)
                self.assertEqual(ctypes.c_uint.in_dll(dll, 'calls').value, 256)

    def test_constant_result_preserves_prefix_trap(self):
        source = '&~constant(n:#u8)->#u8{1/n;(#u8)7}&^entry(n:#u8){constant(n)}'
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            generated, report = self.project(d, source)
            self.assertEqual(report['runtime_functions'][0]['transported_result_slots'], 0)
            self.execute(d, generated, report, lambda n: 7 if n else (1,))
            if support.MSX:
                self.simulate(d, generated, expected='7', first=1)

    def test_keywordless_names_are_metadata(self):
        for name in ('Runtime', 'After', 'CallValue', 'LoopReady', 'Select'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                d = Path(temp)
                generated, report = self.project(d, '&!'+name+'(n:#u8)->#u8;'
                    '&~identity(n:#u8)->#u8{'+name+'(n);n}&^entry(n:#u8){identity(n)}')
                self.assertEqual(report['runtime_functions'][0]['transported_result_slots'], 0)
                self.execute(d, generated, report, lambda n: n,
                    'void vx_external_'+name+'(uint32_t *out,uint8_t n){out[0]=n;}\n')

    def test_loop_invariants_cross_identity_call(self):
        source = ('&~identity(n:#u8)->#u8{n}'
                  '&~walk(n:#u8)->#u8{x:#u8=n;i:#u8=0;'
                  '(i<3)!@{x=identity(x);i+=1;};x}'
                  '&^entry(n:#u8){walk(n)}')
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            generated, report = self.project(d, source)
            self.assertTrue(all(f['transported_result_slots'] == 0 for f in report['runtime_functions']))
            self.assertEqual(report['interface_demand']['loops'][0]['retained_carried_leaves'], 1)
            code = (generated / 'vx_bridges.c').read_text()
            self.assertNotIn('vxlq_', code)
            self.execute(d, generated, report, lambda n: n)
            if support.MSX:
                self.simulate(d, generated, expected='n')

    def test_known_result_does_not_prove_recursive_completion(self):
        source = ('&!pulse();&~again(n:#u8)->#u8{pulse();again(n);(#u8)7}'
                  '&^entry(n:#u8){again(n)}')
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            generated, report = self.project(d, source)
            self.assertEqual(report['runtime_functions'][0]['transported_result_slots'], 0)
            # A bounded test escape, absent from the compiler's contracts.
            dll = self.execute(d, generated, report, lambda n: (73,),
                'unsigned calls;\nvoid vx_external_pulse(uint32_t *out)'
                '{(void)out;++calls;if((calls&3)==0)vx_trap(73);}\n')
            self.assertEqual(ctypes.c_uint.in_dll(dll, 'calls').value, 1024)

    def test_mutable_call_actual_is_a_snapshot(self):
        # last must keep the pre-increment value of x, even after the loop has
        # updated x and the call site has executed again on another iteration.
        source = ('&~identity(n:#u8)->#u8{n}&^entry(n:#u8){'
                  'x:#u8=n;last:#u8=0;i:#u8=0;'
                  '(i<3)!@{last=identity(x);x+=1;i+=1;};last}')
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            generated, report = self.project(d, source)
            self.assertEqual(report['runtime_functions'][0]['transported_result_slots'], 1)
            self.execute(d, generated, report, lambda n: (n+2)&255)
            self.execute(d, generated, report, lambda n: (n+2)&255, split=True)
            if support.MSX:
                self.simulate(d, generated, expected='((n+2)&255)')

    def test_induction_rejects_dependent_candidates(self):
        cases = [
            ('x:#u8=n;y:#u8=n;i:#u8=0;(i<3)!@{x=y;y+=1;i+=1;};x', lambda n: (n+2)&255),
            ('x:#u8=n;y:#u8=n;i:#u8=0;(i<3)!@{x,y=(y,x);i+=1;};x', lambda n: n),
            ('x:#u8=n;i:#u8=0;(i<3)!@{j:#u8=0;(j<2)!@{x+=1;j+=1;};i+=1;};x', lambda n: (n+6)&255),
            ('x:#u8;i:#u8=0;(i<2)!@{n<128?{x=n+i;};i+=1;};n<128?x:(#u8)0', lambda n: n+1 if n<128 else 0),
        ]
        for body, oracle in cases:
            with self.subTest(body=body), tempfile.TemporaryDirectory() as temp:
                d = Path(temp)
                generated, report = self.project(d, '&^entry(n:#u8){'+body+'}')
                self.execute(d, generated, report, oracle)

    def test_signed_narrowing_and_mutable_capture(self):
        cases = [
            ('&~narrow(n:#u8)->#i8{(#i8)n}&^entry(n:#u8){(#i32)narrow(n)+128}', lambda n: n+128 if n<128 else n-128),
            ('&^entry(n:#u8){x:#u8=n;&~read()->#u8{x}i:#u8=0;last:#u8=0;'
             '(i<3)!@{last=read();x+=1;i+=1;};last}', lambda n: (n+2)&255),
        ]
        for source, oracle in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory() as temp:
                d = Path(temp)
                generated, report = self.project(d, source)
                self.assertGreaterEqual(report['runtime_functions'][0]['transported_result_slots'], 1)
                self.execute(d, generated, report, oracle)


if __name__ == '__main__':
    unittest.main(verbosity=2)
