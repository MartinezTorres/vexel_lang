#!/usr/bin/env python3
"""Closed physical input domains reach calculations without weakening checks."""
import ctypes
from pathlib import Path
import tempfile
import unittest
import test_result_demand as support


class TransportDomainTests(unittest.TestCase):
    project = support.ResultDemandTests.project
    execute = support.ResultDemandTests.execute
    simulate = support.ResultDemandTests.simulate

    def simulate_values(self,directory,generated,oracle):
        table='static const uint32_t expected[256]={'+','.join(str(oracle(n)&0xffffffff)+'UL' for n in range(256))+'};'
        self.simulate(directory,generated,expected='expected[n]',includes=table)

    def test_formals_captures_and_compacted_results_retain_domains(self):
        cases = [
            '&~divide(x:#u32,d:#u32)->#u32{x/d}'
            '&^entry(n:#u8){divide((#u32)n,(#u32)(n&15)+1)}',
            '&^entry(n:#u8){d:#u32=(#u32)(n&15)+1;'
            '&~divide(x:#u32)->#u32{x/d}divide((#u32)n)}',
            '&~divisor(n:#u8)->(#u8,#u32,#u32){(n,(#u32)n+700,(#u32)(n&15)+1)}'
            '&^entry(n:#u8){d=divisor(n).__2;(#u32)n/d}',
        ]
        for source in cases:
            for budget in (0,128):
                with self.subTest(source=source,budget=budget),tempfile.TemporaryDirectory() as temporary:
                    d=Path(temporary);generated,report=self.project(d,source,'--frame-ram-bytes',str(budget))
                    code=(generated/'vx_bridges.c').read_text()
                    self.assertIn('vx_value_divu8_runtime',code)
                    self.assertNotIn('vx_divmod(',code)
                    self.execute(d,generated,report,lambda n:n//((n&15)+1))
                    self.execute(d,generated,report,lambda n:n//((n&15)+1),split=True)
                    if support.MSX:self.simulate_values(d,generated,lambda n:n//((n&15)+1))

    def test_loop_backedges_establish_the_physical_snapshot_range(self):
        source=('&^entry(n:#u8){d:#u32=(#u32)(n&15)+1;i:#u8=0;sum:#u32=0;'
                '(i<3)!@{sum+=(#u32)n/d;d=((d+2)&15)+1;i+=1;};sum}')
        def oracle(n):
            divisor=(n&15)+1;result=0
            for _ in range(3):
                result+=n//divisor;divisor=((divisor+2)&15)+1
            return result
        with tempfile.TemporaryDirectory() as temporary:
            d=Path(temporary);generated,report=self.project(d,source)
            self.assertIn('vx_value_divu8_runtime',(generated/'vx_bridges.c').read_text())
            self.execute(d,generated,report,oracle)
            self.execute(d,generated,report,oracle,split=True)
            if support.MSX:self.simulate_values(d,generated,oracle)

    def test_signed_domains_keep_the_complete_quotient(self):
        source=('&~divide(x:#i32,d:#i32)->#i32{x/d}'
                '&^entry(n:#u8){divide((#i32)(#i8)n,0-((#i32)(n&15)+1))}')
        def oracle(n):
            value=n if n<128 else n-256
            return ((abs(value)//((n&15)+1))*(-1 if value>=0 else 1)) & 0xffffffff
        with tempfile.TemporaryDirectory() as temporary:
            d=Path(temporary);generated,report=self.project(d,source)
            # -128/-1 is 128 at the declared width, so reducing to signed
            # byte arithmetic would be wrong even though both inputs fit.
            self.assertIn('vx_value_divs16_runtime',(generated/'vx_bridges.c').read_text())
            self.execute(d,generated,report,oracle,split=True)
            if support.MSX:self.simulate_values(d,generated,oracle)

    def test_all_callers_and_wrapping_still_require_checks(self):
        cases=[
            ('&~divide(d:#u32)->#u32{255/d}'
             '&^entry(n:#u8){a=divide((#u32)(n&15)+1);a+divide((#u32)n)}',
             lambda n:255//((n&15)+1)+255//n if n else (1,),'255/((n&15)+1)+255/n'),
            ('&~divide(d:#u8)->#u32{(#u32)255/(#u32)d}'
             '&^entry(n:#u8){divide((#u8)((#u16)n+1))}',
             lambda n:255//(n+1) if n<255 else (1,),'255/(n+1)'),
        ]
        for index,(source,oracle,expected) in enumerate(cases):
            with self.subTest(index=index),tempfile.TemporaryDirectory() as temporary:
                d=Path(temporary);generated,report=self.project(d,source)
                self.assertIn('vx_divmod(',(generated/'vx_bridges.c').read_text())
                self.execute(d,generated,report,oracle,split=True)
                if support.MSX:
                    self.simulate(d,generated,expected='0',first=256,
                        setup='vx_entry('+('0' if index==0 else '255')+');',expected_fault=1)

    def test_source_contract_limits_finite_enumeration_and_retains_traps(self):
        source='&^entry([[range(1,15)]] n:#u8){(#u32)255/(#u32)n}'
        for budget in (0,512):
            with self.subTest(table_bytes=budget),tempfile.TemporaryDirectory() as temporary:
                d=Path(temporary);generated,report=self.project(d,source,'--prefer','value_table_bytes='+str(budget))
                dll=self.execute(d,generated,report,lambda n:255//n if 1<=n<=15 else (16,),
                    '#include "vx_value_entry.h"\nuint32_t binding(uint8_t n)'
                    '{vx_entry_result r;vx_entry_value(&r,n);return r.v0;}\n',split=True)
                dll.binding.argtypes=[ctypes.c_uint8];dll.binding.restype=ctypes.c_uint32
                for n in range(1,16):self.assertEqual(dll.binding(n),255//n)
                if support.MSX:
                    table='static const uint16_t expected[16]={0,'+','.join(str(255//n) for n in range(1,16))+'};'
                    self.simulate(d,generated,first=256,includes='#include "vx_value_entry.h"\n'+table,setup=
                        'vx_entry_result r;vx_last_result=0;for(n=1;n<16;++n){vx_entry(n);'
                        'vx_entry_value(&r,(uint8_t)n);if(vx_entry_read(0)!=expected[n] || r.v0!=expected[n]){vx_last_result=1;break;}}')
        with tempfile.TemporaryDirectory() as temporary:
            d=Path(temporary);generated,report=self.project(d,source)
            if support.MSX:self.simulate(d,generated,first=256,setup='vx_entry(0);',expected_fault=16)

    def test_call_results_are_stable_across_later_capture_updates(self):
        source=('&^entry(n:#u8){d:#u32=(#u32)(n&7)+1;'
                '&~take()->#u32{old:#u32=d;d=(d&7)+1;old}'
                'a=take();b=take();(#u32)n/a+(#u32)n/b}')
        oracle=lambda n:n//((n&7)+1)+n//((((n&7)+1)&7)+1)
        with tempfile.TemporaryDirectory() as temporary:
            d=Path(temporary);generated,report=self.project(d,source,'--frame-ram-bytes','128')
            self.execute(d,generated,report,oracle,split=True)
            if support.MSX:self.simulate_values(d,generated,oracle)


if __name__=='__main__':
    unittest.main(verbosity=2)
