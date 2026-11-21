#!/usr/bin/env python3
"""Proved private widths preserve logical arithmetic and public interfaces."""
import ctypes
from pathlib import Path
import tempfile
import unittest
import test_result_demand as support


class TransportTests(unittest.TestCase):
    project = support.ResultDemandTests.project
    execute = support.ResultDemandTests.execute
    simulate = support.ResultDemandTests.simulate

    def test_calls_use_small_storage_with_wide_arithmetic(self):
        source=(support.support.ROOT/'examples/transport_widths.vx').read_text()
        for options in ((),('--schedule','lifo','--branch-order','reverse')):
            with self.subTest(options=options),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);g,r=self.project(d,source,*options)
                for fn in r['runtime_functions']:
                    self.assertEqual(fn['transported_argument_bytes'],1)
                    self.assertEqual(fn['transported_result_bytes'],1)
                    for kind in ('arguments','results'):
                        self.assertEqual(fn['transport_types'][kind],[{'leaf':0,'logical':'#u32','storage':'#u8'}])
                self.execute(d,g,r,lambda n:7 if n==255 else (n&127)+1)
                self.execute(d,g,r,lambda n:7 if n==255 else (n&127)+1,split=True)
                if support.MSX and not options:
                    self.simulate(d,g,expected='(n==255?7:(n&127)+1)')

    def test_output_mask_narrows_private_transport_from_a_wide_entry(self):
        source=('&~masked(n:#u32)->#u32{(n+1)&255}'
                '&^entry(n:#u8){masked((#u32)n)}&^wide(n:#u32){masked(n)}')
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,source,'--program')
            fn=r['runtime_functions'][0]
            self.assertEqual(fn['transported_argument_bytes'],1)
            self.assertEqual(fn['transported_result_bytes'],1)
            dll=self.execute(d,g,r,lambda n:(n+1)&255,split=True)
            dll.vx_wide.argtypes=[ctypes.c_uint32];dll.vx_wide_read.restype=ctypes.c_uint32
            for n in (256,65535,65536,0x7fffffff,0x80000000,0xffffffff):
                dll.vx_wide(n);self.assertEqual(dll.vx_wide_read(0),(n+1)&255)

    def test_signed_results_extend_and_signed_arithmetic_stays_wide(self):
        cases=[
            ('&~negative(n:#i32)->#i32{n-7}'
             '&^entry(n:#u8){(#u32)(negative((#i32)(n&15))+8)}',
             '#u8','#i8',lambda n:(n&15)+1,'((n&15)+1)'),
            ('&~negative(n:#i32)->#i32{n-600}'
             '&^entry(n:#u8){(#u32)(negative((#i32)n+300)+400)}',
             '#u16','#i16',lambda n:n+100,'(n+100)'),
        ]
        for source,argument,result,oracle,expected in cases:
            with self.subTest(storage=result),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);g,r=self.project(d,source)
                fn=r['runtime_functions'][0]
                self.assertEqual(fn['transport_types']['arguments'][0]['storage'],argument)
                self.assertEqual(fn['transport_types']['results'][0]['storage'],result)
                self.execute(d,g,r,oracle,split=True)
                if support.MSX:self.simulate(d,g,expected=expected)

    def test_captures_and_recursive_frames_use_the_same_layout(self):
        cases=[
            ('&^entry(n:#u8){x:#u32=(#u32)(n&7);&~bump()->#u32{x+=3;x*2}bump()+x}',lambda n:3*((n&7)+3),'captures'),
            ('&~walk(n:#u32)->#u32{n==0?(#u32)0:(walk((n-1)&7)+1)&255}'
             '&^entry(n:#u8){walk((#u32)(n&7))}',lambda n:n&7,'arguments'),
        ]
        for source,oracle,kind in cases:
            with self.subTest(kind=kind),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);g,r=self.project(d,source)
                self.assertTrue(any(x['logical']=='#u32' and x['storage']=='#u8'
                    for fn in r['runtime_functions'] for x in fn['transport_types'][kind]))
                self.execute(d,g,r,oracle,split=True)

    def test_loop_storage_and_region_binding_preserve_snapshots(self):
        source=('[[c_binding(steps,region,1,exclusive)]] &^entry(n:#u8)->#u32{'
                'x:#u32=(#u32)(n&7);y:#u32=0;i:#u8=0;'
                '(i<3)!@{y=x;x=(x+3)&7;i+=1;};x+16*y}')
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,source)
            loops=r['interface_demand']['loops']
            self.assertTrue(any(x['retained_storage_bytes']<x['retained_logical_bytes'] for x in loops))
            self.execute(d,g,r,lambda n:((n+9)&7)+16*((n+6)&7),split=True)
            # The explicit C binding keeps its public u32 result. Its private
            # carried state uses the shared byte layout, including old y=x.
            types=(g/'vx_binding_types.h').read_text()
            self.assertIn('uint32_t v0;',types)
            self.assertNotRegex(types,r'uint32_t l\d+_s\d+_v\d+;')
            (d/'binding.c').write_text('#include "vx_bind_steps.h"\n'
                'unsigned check(unsigned n){VX_BIND_steps_LOCAL;vx_bind_steps_frame.a0=n;'
                'vx_bind_steps_run();return vx_bind_steps_frame.result.v0;}\n')
            rt=support.support.ROOT/'backends/msx-neo8/runtime';lib=d/'binding.so'
            run=support.support.run(['cc','-std=c11','-shared','-fPIC','-fsanitize=undefined','-fno-sanitize-recover=all',
                '-I'+str(g),'-I'+str(rt),d/'binding.c',g/'vx_binding_frames.c','-o',lib])
            self.assertEqual(run.returncode,0,run.stderr)
            dll=ctypes.CDLL(str(lib))
            for n in range(256):self.assertEqual(dll.check(n),((n+9)&7)+16*((n+6)&7))
            if support.MSX:self.simulate(d,g,expected='(((n+9)&7)+16*((n+6)&7))')

    def test_small_returns_keep_traps_and_foreign_reply_validation(self):
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,'&~small(n:#u32)->#u32{(1/n)&255}&^entry(n:#u8){small((#u32)n)}')
            self.assertEqual(r['runtime_functions'][0]['transported_result_bytes'],1)
            self.execute(d,g,r,lambda n:(1,) if n==0 else 1//n,split=True)
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);g,r=self.project(d,'&!observe(n:#u8)->#u8;'
                '&~small(n:#u8)->#u32{(#u32)observe(n)}&^entry(n:#u8){small(n)}')
            self.assertEqual(r['runtime_functions'][0]['transported_result_bytes'],1)
            dll=self.execute(d,g,r,lambda n:n if n!=13 else (16,),
                'unsigned calls;void vx_external_observe(uint32_t *out,uint8_t n)'
                '{++calls;out[0]=n==13?999:n;}\n',split=True)
            self.assertEqual(ctypes.c_uint.in_dll(dll,'calls').value,256)


if __name__=='__main__':unittest.main(verbosity=2)
