#!/usr/bin/env python3
"""Native value envelopes cross calls and loops without erasing evaluation."""
import ctypes
import json
from pathlib import Path
import tempfile
import unittest
import test_result_demand as support


class RangeTests(unittest.TestCase):
    project = support.ResultDemandTests.project
    execute = support.ResultDemandTests.execute
    simulate = support.ResultDemandTests.simulate

    def test_call_chain_shares_range_and_removes_predicate_transport(self):
        source = (support.support.ROOT/'examples/range_transport.vx').read_text()
        for options in ((), ('--schedule','lifo','--branch-order','reverse')):
            with self.subTest(options=options), tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp); generated, report=self.project(d,source,*options)
                self.assertTrue(report['value_ranges']['converged'])
                self.assertTrue(any(f['arguments']==[{'leaf':0,'min':0,'max':7}]
                                    for f in report['value_ranges']['functions']))
                self.assertTrue(all(f['transported_result_slots']==1 for f in report['runtime_functions']))
                self.execute(d,generated,report,lambda n:(n&7)+1)
                self.execute(d,generated,report,lambda n:(n&7)+1,split=True)
                if support.MSX and not options:
                    self.simulate(d,generated,expected='((n&7)+1)')
                    target=json.loads((d/'msx/generated/plan.json').read_text())
                    self.assertEqual(target['value_ranges'],report['value_ranges'])

    def test_all_callers_participate_in_formal_ranges(self):
        source=('&~bounded(n:#u8)->(#u8,#b){(n+1,n<8)}'
                '&^entry(n:#u8){a,b=bounded(n&7);c,d=bounded(n);'
                '(#u16)(b?a:(#u8)99)+(#u16)(d?c:(#u8)99)}')
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);generated,report=self.project(d,source,'--program')
            self.assertEqual(len(report['runtime_functions']),1)
            self.assertEqual(report['value_ranges']['functions'][0]['arguments'],[])
            self.assertEqual(report['runtime_functions'][0]['transported_result_slots'],2)
            self.execute(d,generated,report,lambda n:(n&7)+1+(n+1 if n<8 else 99))

    def test_capture_domains_cross_a_materialized_boundary(self):
        source=('&^entry(n:#u8){x:#i8=(#i8)(n&7)-4;'
                '&~read()->#b{x<4}read()?(#u8)1:(#u8)0}')
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);generated,report=self.project(d,source)
            self.assertEqual(report['value_ranges']['functions'][0]['captures'],[{'leaf':0,'min':-4,'max':3}])
            self.assertEqual(report['runtime_functions'][0]['transported_result_slots'],0)
            self.execute(d,generated,report,lambda n:1,split=True)

    def test_loop_envelope_has_entry_anchor_and_preserves_snapshots(self):
        source=('&~bounded(n:#u8)->(#u8,#b){(n+1,n<8)}'
                '&^entry(n:#u8){x:#u8=n&7;last:#u8=0;valid:#b=0;i:#u8=0;'
                '(i<3)!@{last,valid=bounded(x);x=(x+3)&7;i+=1;};valid?last:(#u8)99}')
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);generated,report=self.project(d,source)
            self.assertTrue(any(b['min']==0 and b['max']==7 for b in report['value_ranges']['carried']))
            self.assertTrue(any(f['arguments']==[{'leaf':0,'min':0,'max':7}]
                                for f in report['value_ranges']['functions']))
            self.execute(d,generated,report,lambda n:((n+6)&7)+1,split=True)
            if support.MSX:
                self.simulate(d,generated,expected='(((n+6)&7)+1)')

    def test_wraparound_signedness_and_formal_conversions(self):
        cases=[
            ('&~small(n:#u8)->#u8{n<8?(#u8)1:(#u8)0}'
             '&^entry(n:#u8){small((n&7)+250)}',lambda n:int((n&7)>=6)),
            ('&~negative(n:#i8)->#u8{n<0?(#u8)1:(#u8)0}'
             '&^entry(n:#u8){negative((#i8)n)}',lambda n:int(n>=128)),
            ('&~small(n:#i8)->#u8{n<4?(#u8)1:(#u8)0}'
             '&^entry(n:#u8){small((#i8)(n&7)-4)}',lambda n:1),
            ('&~small(n:#u8)->#u8{n<8?(#u8)1:(#u8)0}'
             '&^entry(n:#u8){small((#u8)((#u16)(n&7)+256))}',lambda n:1),
        ]
        for source,oracle in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);generated,report=self.project(d,source)
                self.execute(d,generated,report,oracle,split=True)

    def test_proved_results_preserve_traps_and_foreign_validation(self):
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);generated,report=self.project(d,
                '&^entry([[range(2,7)]] n:#u8)->#u8{n<8?(#u8)1:(#u8)0}')
            self.execute(d,generated,report,lambda n:1 if 2<=n<=7 else (16,))
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);generated,report=self.project(d,
                '&~zero(n:#u8)->#u8{(#u8)(1/n)&0}&^entry(n:#u8){zero(n)}')
            self.execute(d,generated,report,lambda n:0 if n else (1,),split=True)
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);generated,report=self.project(d,
                '&!observe(n:#u8)->#u8;&~small(n:#u8)->#u8{n<8?(#u8)1:(#u8)0}'
                '&^entry(n:#u8){small(observe(n)&7)}')
            dll=self.execute(d,generated,report,lambda n:1 if n!=13 else (16,),
                'unsigned calls;void vx_external_observe(uint32_t *out,uint8_t n)'
                '{++calls;out[0]=n==13?999:n;}\n',split=True)
            self.assertEqual(ctypes.c_uint.in_dll(dll,'calls').value,256)

    def test_division_and_shift_facts_cross_private_boundaries(self):
        cases=[
            ('&~small(x:#u32){x<256?(#u8)1:(#u8)0}'
             '&^entry(n:#u8){small((#u32)n/3)}',lambda n:1,'1'),
            ('&~small(x:#i32){x>=-64 && x<=63?(#u8)1:(#u8)0}'
             '&^entry(n:#u8){small(((#i32)(#i8)n)>>1)}',lambda n:1,'1'),
            ('&~small(x:#i32){x>-7 && x<7?(#u8)1:(#u8)0}'
             '&^entry(n:#u8){small(((#i32)(#i8)n)%(-7))}',lambda n:1,'1'),
            ('&~small(x:#u32){x<128?(#u8)1:(#u8)0}'
             '&^entry(n:#u8){small((#u32)(n&7)<<(n&3))}',lambda n:1,'1'),
        ]
        for source,oracle,expected in cases:
            with self.subTest(source=source),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);generated,report=self.project(d,source)
                self.assertTrue(all(f['transported_result_slots']==0 for f in report['runtime_functions']))
                self.execute(d,generated,report,oracle)
                self.execute(d,generated,report,oracle,split=True)
                if support.MSX:self.simulate(d,generated,expected=expected)
        # A precise successful-value envelope must not hide a trapping operand.
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);generated,report=self.project(d,
                '&~small(x:#u32){x<256?(#u8)1:(#u8)0}'
                '&^entry(n:#u8){small(((#u32)1/n)/3)}')
            self.execute(d,generated,report,lambda n:1 if n else (1,))
            self.execute(d,generated,report,lambda n:1 if n else (1,),split=True)

    def test_analysis_budget_retains_a_correct_general_implementation(self):
        source='&~f0(n:#u8)->#u8{n<8?(#u8)1:(#u8)0}'
        source+=''.join(f'&~f{i}(n:#u8)->#u8{{f{i-1}(n)}}' for i in range(1,40))
        source+='&^entry(n:#u8){f39(n&7)}'
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);generated,report=self.project(d,source)
            self.assertEqual(report['value_ranges']['rounds'],32)
            self.assertFalse(report['value_ranges']['converged'])
            self.assertTrue(report['value_ranges']['contexts']['budget_hit'])
            self.execute(d,generated,report,lambda n:1)

    def test_pure_binding_consumes_the_same_facts(self):
        source=('[[c_binding(check,full,1,exclusive)]] &^entry(n:#u8)->#u8'
                '{(n&7)<8?(#u8)7:(#u8)9}')
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);generated,report=self.project(d,source)
            # Full C bindings retain their declared client interface. Their
            # implementation still consumes the common range proof.
            self.assertNotIn('?', (generated/'vx_bind_check.h').read_text())
            (d/'binding.c').write_text('#include "vx_bind_check.h"\n'
                'unsigned check(void){VX_BIND_check_LOCAL;vx_bind_check_frame.a0=255;'
                'vx_bind_check_run();return vx_bind_check_frame.result.v0;}\n')
            runtime=support.support.ROOT/'backends/msx-neo8/runtime';library=d/'binding.so'
            result=support.support.run(['cc','-std=c11','-shared','-fPIC','-I'+str(generated),'-I'+str(runtime),
                                       d/'binding.c',generated/'vx_binding_frames.c','-o',library])
            self.assertEqual(result.returncode,0,result.stderr)
            dll=ctypes.CDLL(str(library));self.assertEqual(dll.check(),7)


if __name__=='__main__':
    unittest.main(verbosity=2)
