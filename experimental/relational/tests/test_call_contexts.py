#!/usr/bin/env python3
"""Function value graphs answer bounded abstract queries for each invocation."""
import ctypes
from pathlib import Path
import tempfile
import unittest
import test_result_demand as support


class ContextTests(unittest.TestCase):
    project=support.ResultDemandTests.project
    execute=support.ResultDemandTests.execute
    simulate=support.ResultDemandTests.simulate

    def test_narrow_call_keeps_facts_when_another_call_is_wide(self):
        declarations=['&~bounded(n:#u8)->(#u8,#b){(n+1,n<8)}',
                      '&~forward(n:#u8)->(#u8,#b){bounded(n)}',
                      '&^entry(n:#u8){a,b=forward(n&7);c,d=forward(n);'
                      '(#u16)(b?a:(#u8)99)+(#u16)(d?c:(#u8)99)}']
        for options,source in (((),declarations),(('--schedule','lifo','--branch-order','reverse'),declarations[::-1])):
            with self.subTest(options=options),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);g,r=self.project(d,'\n'.join(source),*options)
                self.assertEqual(len(r['runtime_functions']),2)
                self.assertTrue(all(not f['arguments'] for f in r['value_ranges']['functions']))
                self.assertTrue(all(f['transported_result_slots']==2 for f in r['runtime_functions']))
                self.assertTrue(any(x['leaf']==1 and x['min']==x['max']==1 for x in r['value_ranges']['call_results']))
                oracle=lambda n:(n&7)+1+(n+1 if n<8 else 99)
                self.execute(d,g,r,oracle);self.execute(d,g,r,oracle,split=True)
                if support.MSX and not options:self.simulate(d,g,expected='((n&7)+1+(n<8?n+1:99))')

    def test_sequential_captures_keep_each_invocations_snapshot(self):
        source=(support.support.ROOT/'examples/call_contexts.vx').read_text()
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,source)
            self.assertEqual(len(r['runtime_functions']),1)
            fn=r['runtime_functions'][0]
            self.assertEqual(fn['transported_capture_bytes'],1)
            self.assertEqual(fn['transported_result_bytes'],1)
            ranges={(x['min'],x['max']) for x in r['value_ranges']['call_results']}
            self.assertTrue({(3,10),(6,13)}<=ranges)
            self.execute(d,g,r,lambda n:17*(n&7)+99,split=True)
            if support.MSX:self.simulate(d,g,expected='(17*(n&7)+99)')

    def test_same_bounds_never_imply_equal_values(self):
        source=('&~different(a:#u8,b:#u8)->#u8{a-b}'
                '&^entry(n:#u8){different(n&7,(n+1)&7)}')
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,source)
            self.execute(d,g,r,lambda n:((n&7)-((n+1)&7))&255,split=True)

    def test_contextual_return_does_not_remove_effects_or_traps(self):
        source=('&!observe(n:#u8)->#u8;'
                '&~check(n:#u8)->#u8{observe(n);n<8?(#u8)1:(#u8)0}'
                '&^entry(n:#u8){a=check(n&7);b=check(n);a+b}')
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,source)
            self.assertTrue(any(x['min']==x['max']==1 for x in r['value_ranges']['call_results']))
            dll=self.execute(d,g,r,lambda n:(16,) if n==13 else 1+int(n<8),
                'unsigned calls;void vx_external_observe(uint32_t *out,uint8_t n)'
                '{++calls;out[0]=n==13?999:n;}\n',split=True)
            self.assertEqual(ctypes.c_uint.in_dll(dll,'calls').value,512)
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,'&~check(n:#u8)->#u8{1/n;n<8?(#u8)1:(#u8)0}'
                '&^entry(n:#u8){check(n&7)+check(n)}')
            self.execute(d,g,r,lambda n:(1,) if (n&7)==0 else 1+int(n<8),split=True)

    def test_recursive_queries_fall_back_without_proving_completion(self):
        source=('&~walk(n:#u8)->#u8{n==0?(#u8)7:walk(n-1)+1}'
                '&^entry(n:#u8){walk(n&7)}')
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,source)
            self.execute(d,g,r,lambda n:(n&7)+7,split=True)


if __name__=='__main__':unittest.main(verbosity=2)
