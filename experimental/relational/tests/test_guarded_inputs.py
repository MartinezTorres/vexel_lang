#!/usr/bin/env python3
"""Shared native input domains include every execution path and its clock."""
import ctypes
from pathlib import Path
import tempfile
import unittest
import test_result_demand as support


class GuardedInputTests(unittest.TestCase):
    project=support.ResultDemandTests.project
    execute=support.ResultDemandTests.execute
    simulate=support.ResultDemandTests.simulate

    def test_guarded_inputs_cross_forwarding_without_cloning(self):
        declarations=(support.support.ROOT/'examples/guarded_inputs.vx').read_text().splitlines()[2:]
        for options,source in (((),declarations),(('--schedule','lifo','--branch-order','reverse'),declarations[::-1])):
            with self.subTest(options=options),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);g,r=self.project(d,'\n'.join(source),'--program',*options)
                self.assertEqual(len(r['runtime_functions']),2)
                for fn in r['runtime_functions']:
                    self.assertEqual(fn['transported_argument_bytes'],1)
                    self.assertEqual(fn['transported_result_bytes'],1)
                self.assertTrue(any(x['arguments']==[{'leaf':0,'min':0,'max':127}]
                                    for x in r['value_ranges']['call_inputs']))
                dll=self.execute(d,g,r,lambda n:n+1 if n<128 else 0,split=True)
                dll.vx_wide.argtypes=[ctypes.c_uint32];dll.vx_wide_read.restype=ctypes.c_uint32
                for n in (0,127,128,255,256,65535,0x80000000,0xffffffff):
                    dll.vx_wide(n);self.assertEqual(dll.vx_wide_read(0),n+1 if n<128 else 0)
                if support.MSX and not options:self.simulate(d,g,expected='(n<128?n+1:0)')

    def test_unguarded_callers_and_calls_before_a_branch_keep_full_inputs(self):
        sources=[
            '&~advance(n:#u32)->#u32{n+1}&^entry(n:#u8){n<8?advance((#u32)n):(#u32)0}'
            '&^wide(n:#u32){advance(n)}',
            '&~advance(n:#u32)->#u32{n+1}&^entry(n:#u8){a=advance((#u32)n);n<8?a:(#u32)0}'
            '&^wide(n:#u32){a=advance(n);n<8?a:(#u32)0}',
        ]
        for index,source in enumerate(sources):
            with self.subTest(index=index),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);g,r=self.project(d,source,'--program')
                self.assertEqual(r['runtime_functions'][0]['transported_argument_bytes'],4)
                dll=self.execute(d,g,r,lambda n:n+1 if n<8 else 0,split=True)
                dll.vx_wide.argtypes=[ctypes.c_uint32];dll.vx_wide_read.restype=ctypes.c_uint32
                for n in (0,7,8,65535,0xffffffff):
                    dll.vx_wide(n);self.assertEqual(dll.vx_wide_read(0),(n+1)&0xffffffff if index==0 else n+1 if n<8 else 0)

    def test_captures_and_wrapping_predicates_use_logical_values(self):
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,'&^entry(n:#u8){x:#i32=(#i32)n-128;'
                '&~step()->#i32{x-=1;x}x >= -7 && x<=7 ? (#u32)(step()+8) : (#u32)0}')
            fn=r['runtime_functions'][0]
            self.assertEqual(fn['transport_types']['captures'][0]['storage'],'#i8')
            self.execute(d,g,r,lambda n:n-121 if 121<=n<=135 else 0,split=True)
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,'&~advance(n:#u32)->#u32{n+1}'
                '&^entry(n:#u8){x:#u32=(#u32)n*257;(#u8)x<8?advance(x):(#u32)0}')
            self.assertGreaterEqual(r['runtime_functions'][0]['transported_argument_bytes'],2)
            self.execute(d,g,r,lambda n:n*257+1 if n<8 else 0,split=True)

    def test_loop_body_guard_does_not_constrain_condition_calls(self):
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,'&~advance(n:#u32)->#u32{n+1}'
                '&^entry(n:#u8){x:#u32=(#u32)n;(x<8)!@{x=advance(x);};x}'
                '&^wide(n:#u32){x=n;(x<8)!@{x=advance(x);};x}','--program')
            self.assertEqual(r['runtime_functions'][0]['transported_argument_bytes'],1)
            dll=self.execute(d,g,r,lambda n:max(n,8),split=True)
            dll.vx_wide.argtypes=[ctypes.c_uint32];dll.vx_wide_read.restype=ctypes.c_uint32
            for n in (0,7,8,256,0xffffffff):dll.vx_wide(n);self.assertEqual(dll.vx_wide_read(0),max(n,8))
            if support.MSX:self.simulate(d,g,expected='(n<8?8:n)')
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,'&~check(n:#u32)->#b{n<8}'
                '&^entry(n:#u8){x:#u32=(#u32)n;(check(x))!@{x+=1;};x}'
                '&^wide(n:#u32){x=n;(check(x))!@{x+=1;};x}','--program')
            self.assertEqual(r['runtime_functions'][0]['transported_argument_bytes'],4)
            self.execute(d,g,r,lambda n:max(n,8),split=True)

    def test_condition_updates_and_nested_loops_keep_their_own_snapshots(self):
        cases=[
            ('&~advance(n:#u32)->#u32{n+1}&^entry(n:#u8){x:#u32=(#u32)n;'
             '&~move(){x+=300;}n<8?{move();advance(x)}:(#u32)0}',lambda n:n+301 if n<8 else 0),
            ('&~advance(n:#u32)->#u32{n+1}&^entry(n:#u8){x:#u32=(#u32)n;r:#u32=0;'
             '({x+=1;x<8})!@{r=advance(x);};r}',lambda n:8 if n<7 else 0),
            ('&~advance(n:#u32)->#u32{n+1}&^entry(n:#u8){x:#u32=(#u32)(n&3);r:#u32=0;k:#u8=0;'
             'x<8?{(k<3)!@{r+=advance(x);x+=100;k+=1;};};r}',lambda n:3*(n&3)+303),
        ]
        for source,oracle in cases:
            with self.subTest(source=source),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);g,r=self.project(d,source)
                self.execute(d,g,r,oracle,split=True)

    def test_narrow_inputs_keep_checks_and_effects(self):
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,'&!read(n:#u8)->#u8;&~advance(n:#u32)->#u32{1/n;n+1}'
                '&^entry(n:#u8){x=read(n);x<8?advance((#u32)x):(#u32)0}')
            self.assertEqual(r['runtime_functions'][0]['transported_argument_bytes'],1)
            dll=self.execute(d,g,r,lambda n:(16,) if n==13 else (1,) if n==0 else n+1 if n<8 else 0,
                'unsigned calls;void vx_external_read(uint32_t *out,uint8_t n)'
                '{++calls;out[0]=n==13?999:n;}\n',split=True)
            self.assertEqual(ctypes.c_uint.in_dll(dll,'calls').value,256)


if __name__=='__main__':unittest.main(verbosity=2)
