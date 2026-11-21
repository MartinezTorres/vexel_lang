#!/usr/bin/env python3
"""Equal changing values share transport only within a proved observation time."""
import ctypes
from pathlib import Path
import tempfile
import unittest
import test_result_demand as support


class EqualityTests(unittest.TestCase):
    project = support.ResultDemandTests.project
    execute = support.ResultDemandTests.execute
    simulate = support.ResultDemandTests.simulate

    def test_loop_equality_crosses_materialized_returns(self):
        source = (support.support.ROOT / 'examples/equality_transport.vx').read_text()
        for options in ((), ('--schedule', 'lifo', '--branch-order', 'reverse')):
            with self.subTest(options=options), tempfile.TemporaryDirectory() as temp:
                d = Path(temp)
                generated, report = self.project(d, source, *options)
                for fn in report['runtime_functions']:
                    self.assertEqual(fn['logical_result_slots'], 2)
                    self.assertEqual(fn['transported_logical_leaves'], [0])
                    self.assertEqual(fn['transported_result_bytes'], 1)
                    self.assertEqual(fn['result_aliases'], [{'leaf': 1, 'representative': 0}])
                loop = report['interface_demand']['loops'][0]
                self.assertEqual(loop['coalesced_carried_leaves'], 1)
                self.assertEqual(loop['retained_carried_leaves'], 2)
                self.execute(d, generated, report, lambda n: ((n+3*(n&3))&255)*257)
                self.execute(d, generated, report, lambda n: ((n+3*(n&3))&255)*257, split=True)
                if support.MSX and not options:
                    self.simulate(d, generated, expected='(((n+3*(n&3))&255)*257U)')

    def test_returned_capture_and_result_share_one_snapshot(self):
        source = ('&^entry(n:#u8){x:#u8=n;last:#u8=n;'
                  '&~step()->#u8{x+=3;x}i:#u8=0;'
                  '(i<3)!@{last=step();i+=1;};(#u16)last|((#u16)x<<8)}')
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            generated, report = self.project(d, source, '--prefer', 'bank_chunk_operations=1')
            fn = report['runtime_functions'][0]
            self.assertEqual(fn['logical_result_slots'], 2)
            self.assertEqual(fn['transported_result_bytes'], 1)
            self.assertEqual(report['interface_demand']['loops'][0]['coalesced_carried_leaves'], 1)
            self.assertTrue(any(m['name'].startswith('vx_chunk_') for m in report['bank_modules']))
            self.execute(d, generated, report, lambda n: ((n+9)&255)*257, split=True)
            if support.MSX:
                self.simulate(d, generated, expected='(((n+9)&255)*257U)')

    def test_sparse_consumer_selects_an_unobserved_representative(self):
        source = ('&~many(n:#u8){a=n+1;b=(#u16)n+500;(a,b,a,b)}'
                  '&^entry(n:#u8){many(n).__3}')
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            generated, report = self.project(d, source)
            fn = report['runtime_functions'][0]
            self.assertEqual(fn['transported_logical_leaves'], [1])
            self.assertEqual(fn['transported_result_bytes'], 2)
            self.execute(d, generated, report, lambda n: n+500, split=True)
            if support.MSX:
                self.simulate(d, generated, expected='n+500')

    def test_refinement_rejects_dependent_and_conditional_equalities(self):
        # The first pair appears preserved if the second pair is assumed
        # equal. Rejecting the second pair must invalidate the first as well.
        cases = [
            ('x:#u8=n;y:#u8=n;a:#u8=n+1;b:#u8=n+1;i:#u8=0;'
             '(i<3)!@{x=a;y=b;a+=1;b+=2;i+=1;};', lambda n: ((n+3)&255) | (((n+5)&255)<<8)),
            ('x:#u8=n;y:#u8=n;i:#u8=0;'
             '(i<3)!@{x+=1;n<128?{y+=1;}:{y+=2;};i+=1;};',
             lambda n: ((n+3)&255) | (((n+(3 if n<128 else 6))&255)<<8)),
            ('x:#u8=n;y:#u8=n+1;i:#u8=0;(i<3)!@{x,y=(y,x);i+=1;};',
             lambda n: ((n+1)&255) | (n<<8)),
        ]
        for body, oracle in cases:
            with self.subTest(body=body), tempfile.TemporaryDirectory() as temp:
                d = Path(temp)
                generated, report = self.project(d, '&^entry(n:#u8){'+body+'(#u16)x|((#u16)y<<8)}')
                self.assertEqual(report['interface_demand']['loops'][0]['coalesced_carried_leaves'], 0)
                self.execute(d, generated, report, oracle)

    def test_readiness_types_and_trapping_results_remain_distinct(self):
        cases = [
            ('&^entry(n:#u8){x:#u8;y:#u8;i:#u8=0;'
             '(i<2)!@{n<128?{x=n+i;y=n+i;};i+=1;};'
             'n<128?((#u16)x|((#u16)y<<8)):(#u16)0}', lambda n: (n+1)*257 if n<128 else 0),
            ('&~pair(n:#u8)->(#u8,#i8){(n&127,(#i8)(n&127))}'
             '&^entry(n:#u8){a,b=pair(n);(#u16)a+(#u16)b}', lambda n: 2*(n&127)),
            ('&~pair(n:#u8)->(#u8,#u8){((#u8)(1/n),(#u8)(1/n))}'
             '&^entry(n:#u8){a,b=pair(n);(#u16)a+(#u16)b}', lambda n: 2*(1//n) if n else (1,)),
        ]
        for source, oracle in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory() as temp:
                d = Path(temp)
                generated, report = self.project(d, source)
                for loop in report['interface_demand']['loops']:
                    self.assertEqual(loop['coalesced_carried_leaves'], 0)
                for fn in report['runtime_functions']:
                    self.assertEqual(fn['result_aliases'], [])
                    self.assertEqual(fn['transported_result_slots'], 2)
                self.execute(d, generated, report, oracle)

    def test_distinct_calls_and_foreign_validation(self):
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            generated, report = self.project(d, '&!observe(n:#u8)->#u8;'
                '&~pair(n:#u8){a=observe(n);b=observe(n);(a,b)}'
                '&^entry(n:#u8){a,b=pair(n);(#u16)a|((#u16)b<<8)}')
            self.assertEqual(report['runtime_functions'][0]['result_aliases'], [])
            dll = self.execute(d, generated, report, lambda n: n | (((n+1)&255)<<8),
                'unsigned calls;void vx_external_observe(uint32_t *out,uint8_t n)'
                '{out[0]=(uint8_t)(n+(calls++&1));}\n')
            self.assertEqual(ctypes.c_uint.in_dll(dll, 'calls').value, 512)
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            generated, report = self.project(d, '&!observe(n:#u8)->(#u8,#u8);'
                '&~pair(n:#u8){p=observe(n);(p.__0,p.__0)}'
                '&^entry(n:#u8){a,b=pair(n);(#u16)a|((#u16)b<<8)}')
            self.assertEqual(report['runtime_functions'][0]['transported_result_bytes'], 1)
            dll = self.execute(d, generated, report, lambda n: n*257 if n!=13 else (16,),
                'unsigned calls;void vx_external_observe(uint32_t *out,uint8_t n)'
                '{++calls;out[0]=n;out[1]=n==13?999:7;}\n')
            self.assertEqual(ctypes.c_uint.in_dll(dll, 'calls').value, 256)

    def test_equal_results_do_not_prove_completion(self):
        source = ('&!pulse();&~again(n:#u8)->(#u8,#u8){pulse();again(n);(n+1,n+1)}'
                  '&^entry(n:#u8){a,b=again(n);(#u16)a+(#u16)b}')
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            generated, report = self.project(d, source)
            self.assertEqual(report['runtime_functions'][0]['transported_result_bytes'], 1)
            dll = self.execute(d, generated, report, lambda n: (73,),
                'unsigned calls;void vx_external_pulse(uint32_t *out)'
                '{(void)out;++calls;if((calls&3)==0)vx_trap(73);}\n')
            self.assertEqual(ctypes.c_uint.in_dll(dll, 'calls').value, 1024)


if __name__ == '__main__':
    unittest.main(verbosity=2)
