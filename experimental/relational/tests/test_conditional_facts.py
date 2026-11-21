#!/usr/bin/env python3
"""Branch assumptions refine shared queries without leaking across snapshots."""
import ctypes
from pathlib import Path
import tempfile
import unittest
import test_result_demand as support


class ConditionalTests(unittest.TestCase):
    project=support.ResultDemandTests.project
    execute=support.ResultDemandTests.execute
    simulate=support.ResultDemandTests.simulate

    def test_branch_bounds_cross_calls_with_unrestricted_callers(self):
        source=(support.support.ROOT/'examples/conditional_facts.vx').read_text()
        for options in ((),('--schedule','lifo','--branch-order','reverse')):
            with self.subTest(options=options),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);g,r=self.project(d,source,'--program',*options)
                self.assertEqual(len(r['runtime_functions']),2)
                for fn in r['runtime_functions']:
                    self.assertEqual(fn['transported_argument_bytes'],4)
                    self.assertEqual(fn['transported_result_bytes'],1)
                self.assertGreater(r['value_ranges']['contexts']['branch_depth_peak'],0)
                dll=self.execute(d,g,r,lambda n:n+1 if n<128 else 0,split=True)
                dll.vx_wide.argtypes=[ctypes.c_uint32];dll.vx_wide_read.restype=ctypes.c_uint32
                for n in (0,127,128,255,256,65535,0x80000000,0xffffffff):
                    dll.vx_wide(n);self.assertEqual(dll.vx_wide_read(0),n+1 if n<128 else 0)
                if support.MSX and not options:self.simulate(d,g,expected='(n<128?n+1:0)')

    def test_nested_guards_signed_values_and_disjunctions(self):
        cases=[
            ('n>=8&&n<=15?n-8:(#u32)0',lambda n:n-8 if 8<=n<=15 else 0),
            ('n<8||n>15?(#u32)0:n-8',lambda n:n-8 if 8<=n<=15 else 0),
            ('n!=0?(n<8?n-1:(#u32)0):(#u32)0',lambda n:n-1 if 0<n<8 else 0),
            ('n==7?n+1:(#u32)0',lambda n:8 if n==7 else 0),
            ('n<8?(n>=8?n+1000:n+1):(#u32)0',lambda n:n+1 if n<8 else 0),
        ]
        for body,oracle in cases:
            with self.subTest(body=body),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);g,r=self.project(d,'&~f(n:#u32)->#u32{'+body+'}'
                    '&^entry(n:#u8){f((#u32)n)}&^wide(n:#u32){f(n)}','--program')
                self.assertEqual(r['runtime_functions'][0]['transported_result_bytes'],1)
                self.execute(d,g,r,oracle,split=True)
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,'&~clip(n:#i32)->#i32{n < -7 ? (#i32)-7 : (n>7?(#i32)7:n)}'
                '&^entry(n:#u8){(#u32)(clip((#i32)n-128)+7)}&^wide(n:#i32){clip(n)}','--program')
            self.assertEqual(r['runtime_functions'][0]['transport_types']['results'][0]['storage'],'#i8')
            dll=self.execute(d,g,r,lambda n:max(-7,min(7,n-128))+7,split=True)
            dll.vx_wide.argtypes=[ctypes.c_int32];dll.vx_wide_read.restype=ctypes.c_uint32
            for n in (-0x80000000,-128,-8,-7,0,7,8,128,0x7fffffff):
                dll.vx_wide(n);self.assertEqual(dll.vx_wide_read(0),max(-7,min(7,n))&0xffffffff)
            if support.MSX:self.simulate(d,g,expected='(n<121?0:n>135?14:n-121)')

    def test_assumptions_do_not_escape_arms_or_wrapping_conversions(self):
        cases=[
            ('&~f(n:#u32)->#u32{a=n<8?n+1:(#u32)0;a+n}'
             '&^entry(n:#u8){f((#u32)n*257)}',lambda n:1 if n==0 else n*257),
            ('&~f(n:#u32)->#u32{(#u8)n<8?n:(#u32)0}'
             '&^entry(n:#u8){f((#u32)n*257)}',lambda n:n*257 if n<8 else 0),
            ('&~f(n:#u32)->#u32{(#i8)n<0?n:(#u32)0}'
             '&^entry(n:#u8){f((#u32)n)}',lambda n:n if n>=128 else 0),
            ('&~f(n:#u8)->#u32{n<128?(#u32)(n+250):(#u32)0}'
             '&^entry(n:#u8){f(n)}',lambda n:(n+250)&255 if n<128 else 0),
        ]
        for source,oracle in cases:
            with self.subTest(source=source),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);g,r=self.project(d,source)
                self.execute(d,g,r,oracle,split=True)

    def test_branch_facts_follow_helper_arguments_and_keep_snapshot_clocks(self):
        cases=[
            ('&~inc(n:#u32)->#u32{n+1}&~f(n:#u32)->#u32{n<8?inc(n):(#u32)0}'
             '&^entry(n:#u8){f((#u32)n)}',lambda n:n+1 if n<8 else 0),
            ('&^entry(n:#u8){x:#u32=(#u32)n;&~bump()->#u32{x+=1;x}'
             'a=bump();b=bump();a<8?b:a}',lambda n:n+2 if n<7 else n+1),
            ('&^entry(n:#u8){x:#u32=(#u32)(n&7);i:#u8=0;'
             '(i<3)!@{x=x<8?x+3:x-5;i+=1;};x}',
             lambda n:self.steps(n&7)),
        ]
        for source,oracle in cases:
            with self.subTest(source=source),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);g,r=self.project(d,source)
                self.execute(d,g,r,oracle,split=True)

    def test_two_operands_and_guarded_loop_transfers(self):
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,'&~minimum(a:#u32,b:#u32)->#u32{a<b?a:b}'
                '&^entry(n:#u8){minimum((#u32)n*257,(#u32)(n&7))}')
            self.assertEqual(r['runtime_functions'][0]['transported_result_bytes'],1)
            self.execute(d,g,r,lambda n:n&7,split=True)
        source=('[[c_binding(steps,region,1,exclusive)]] &^entry(n:#u8)->#u32{'
                'x:#u32=(#u32)n;i:#u8=0;(i<3)!@{x=x<8?x+1:(#u32)0;i+=1;};x}')
        def oracle(n):
            for _ in range(3):n=n+1 if n<8 else 0
            return n
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,source)
            self.assertTrue(any(x['retained_storage_bytes']<x['retained_logical_bytes']
                                for x in r['interface_demand']['loops']))
            self.execute(d,g,r,oracle,split=True)
            (d/'binding.c').write_text('#include "vx_bind_steps.h"\n'
                'unsigned check(unsigned n){VX_BIND_steps_LOCAL;vx_bind_steps_frame.a0=n;'
                'vx_bind_steps_run();return vx_bind_steps_frame.result.v0;}\n')
            rt=support.support.ROOT/'backends/msx-neo8/runtime';lib=d/'binding.so'
            run=support.support.run(['cc','-std=c11','-shared','-fPIC','-fsanitize=undefined','-fno-sanitize-recover=all',
                '-I'+str(g),'-I'+str(rt),d/'binding.c',g/'vx_binding_frames.c','-o',lib])
            self.assertEqual(run.returncode,0,run.stderr)
            dll=ctypes.CDLL(str(lib))
            for n in range(256):self.assertEqual(dll.check(n),oracle(n))
            if support.MSX:self.simulate(d,g,expected='(n<6?n+3:n<9?n-6:2)')

    @staticmethod
    def steps(x):
        for _ in range(3):x=x+3 if x<8 else x-5
        return x

    def test_effects_traps_and_foreign_validation_remain_required(self):
        source=('&!observe(n:#u8)->#u8;&~f(n:#u8)->#u32{'
                'observe(n);n<8?(n>=8?(#u32)99:(#u32)1):(#u32)1}'
                '&^entry(n:#u8){f(n)}')
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,source)
            dll=self.execute(d,g,r,lambda n:(16,) if n==13 else 1,
                'unsigned calls;void vx_external_observe(uint32_t *out,uint8_t n)'
                '{++calls;out[0]=n==13?999:n;}\n',split=True)
            self.assertEqual(ctypes.c_uint.in_dll(dll,'calls').value,256)
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,'&!observe(n:#u8)->#u8;'
                '&~f(n:#u32)->#u32{n<8&&observe((#u8)n)!=0?n+1:(#u32)0}'
                '&^entry(n:#u8){f((#u32)n)}')
            self.assertEqual(r['runtime_functions'][0]['transported_result_bytes'],1)
            dll=self.execute(d,g,r,lambda n:(16,) if n==5 else n+1 if 0<n<8 else 0,
                'unsigned calls;void vx_external_observe(uint32_t *out,uint8_t n)'
                '{++calls;out[0]=n==5?999:n;}\n',split=True)
            self.assertEqual(ctypes.c_uint.in_dll(dll,'calls').value,8)
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,'&~f(n:#u32)->#u32{1/n;n<8?(n>=8?99:1):1}'
                '&^entry(n:#u8){f((#u32)n)}')
            self.execute(d,g,r,lambda n:1 if n else (1,),split=True)


if __name__=='__main__':unittest.main(verbosity=2)
