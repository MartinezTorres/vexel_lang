#!/usr/bin/env python3
"""Reuse abstract answers without conflating scopes, guards or executions."""
import ctypes
from pathlib import Path
import tempfile
import unittest
import test_result_demand as support


class SharedProofTests(unittest.TestCase):
    project=support.ResultDemandTests.project
    execute=support.ResultDemandTests.execute
    simulate=support.ResultDemandTests.simulate

    def test_common_guard_proofs_across_result_fields(self):
        source=(support.support.ROOT/'examples/shared_proofs.vx').read_text()
        for options in ((),('--schedule','lifo','--branch-order','reverse')):
            with self.subTest(options=options),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);g,r=self.project(d,source,'--program',*options)
                fn,=r['runtime_functions']
                self.assertEqual(fn['transported_argument_bytes'],4)
                self.assertEqual(fn['transported_result_bytes'],4)
                contexts=r['value_ranges']['contexts']
                self.assertGreater(contexts['query_hits'],contexts['query_misses'])
                self.assertFalse(contexts['budget_hit'])
                dll=self.execute(d,g,r,lambda n:3*n+120 if 8<=n<128 else 0,split=True)
                dll.vx_wide.argtypes=[ctypes.c_uint32]
                dll.vx_wide_read.argtypes=[ctypes.c_uint16]
                dll.vx_wide_read.restype=ctypes.c_uint32
                for n in (0,7,8,127,128,255,256,65535,0x80000000,0xffffffff):
                    dll.vx_wide(n)
                    self.assertEqual(dll.vx_wide_read(0),3*n+120 if 8<=n<128 else 0,n)
                if support.MSX and not options:
                    self.simulate(d,g,expected='(n>=8&&n<128?3*n+120:0)')

    def test_function_owners_and_branch_environments_remain_distinct(self):
        source=('&~small(n:#u32)->#u32{n+1}'
                '&~large(n:#u32)->#u32{n+1}'
                '&~guards(n:#u32)->(#u32,#u32){'
                '(n<8?n+1:(#u32)0,n<1024?n+1:(#u32)0)}'
                '&^entry(n:#u8){p=guards((#u32)n*17);'
                'small((#u32)(n&7))+large((#u32)n*256)+p.__0+p.__1}'
                '&^wide(n:#u32){guards(n)}')
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,source,'--program')
            sizes=sorted(f['transported_result_bytes'] for f in r['runtime_functions'])
            self.assertEqual(sizes,[1,2,3])
            self.execute(d,g,r,lambda n:(n&7)+1+n*256+1+
                         (n*17+1 if n*17<8 else 0)+(n*17+1 if n*17<1024 else 0),split=True)

    def test_cached_answers_do_not_cache_execution_or_capture_values(self):
        source=('&!observe(n:#u8)->#u8;'
                '&^entry(n:#u8){x:#u8=n&7;'
                '&~step()->#u32{observe(x);x=(x+1)&7;(#u32)x+1}'
                'a=step();b=step();a*16+b}')
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,source)
            dll=self.execute(d,g,r,lambda n:16*(((n+1)&7)+1)+((n+2)&7)+1,
                'unsigned calls;void vx_external_observe(uint32_t *out,uint8_t n)'
                '{++calls;out[0]=n;}\n',split=True)
            self.assertEqual(ctypes.c_uint.in_dll(dll,'calls').value,512)


if __name__=='__main__':unittest.main(verbosity=2)
