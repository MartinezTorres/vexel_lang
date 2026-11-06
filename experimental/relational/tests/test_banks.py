#!/usr/bin/env python3
"""Compile the split projection, including recursive calls and boundary state."""
import ctypes
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support

class BankTests(unittest.TestCase):
    def execute(self, source, oracle, count=1, bindings=''):
        for schedule, branch in support.ORDERS:
            with self.subTest(schedule=schedule, branch=branch), tempfile.TemporaryDirectory(prefix='vexel-banks-') as temp:
                directory=Path(temp);path=directory/'source.vx';path.write_text(source);generated=directory/'generated'
                result=support.run([support.EXE,path,'--source','entry','--budget',count*4,'--emit-c',generated,
                                    '--schedule',schedule,'--branch-order',branch])
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                report=json.loads(result.stdout);modules=report['bank_modules']
                self.assertTrue(modules)
                self.assertEqual(len({m['module'] for m in modules}),len(modules))
                runtime=support.ROOT/'backends/msx-neo8/runtime';copied=directory/'runtime.c'
                copied.write_text((runtime/'vx_runtime.c').read_text().replace('uint16_t vx_bank_b(uint16_t segment)',
                                  'uint16_t original_bank(uint16_t segment)',1))
                harness=directory/'harness.c'
                harness.write_text('#include "vexel.h"\nunsigned switches;\n'
                    'uint16_t vx_bank_b(uint16_t segment){uint16_t old=vx_segment_b;++switches;vx_segment_b=segment;return old;}\n'+bindings)
                library=directory/'split.so'
                result=support.run(['cc','-O2','-std=c11','-shared','-fPIC','-I'+str(generated),'-I'+str(runtime),
                    generated/'vx_banked.c',*[generated/(m['module']+'.c') for m in modules],copied,harness,'-o',library])
                self.assertEqual(result.returncode,0,result.stderr)
                dll=ctypes.CDLL(str(library));dll.vx_entry.argtypes=[ctypes.c_uint8]
                dll.vx_entry_read.argtypes=[ctypes.c_uint16];dll.vx_entry_read.restype=ctypes.c_uint32
                segment=ctypes.c_uint16.in_dll(dll,'vx_segment_b');switches=ctypes.c_uint.in_dll(dll,'switches')
                total=0
                for n in range(256):
                    total+=n;segment.value=731;switches.value=0;dll.vx_entry(n)
                    self.assertEqual(segment.value,731,n);self.assertGreaterEqual(switches.value,2,n)
                    self.assertEqual([dll.vx_entry_read(i) for i in range(count)],oracle(n,total),n)

    def test_recursive_and_output_only_call_interfaces(self):
        self.execute('&~even(n:#u8)->#u8{n==0 ? (#u8)1 : odd(n-1)}'
                     '&~odd(n:#u8)->#u8{n==0 ? (#u8)0 : even(n-1)}&^entry(n:#u8){even(n)}',
                     lambda n,total:[int(n%2==0)])
        self.execute('&^entry(n:#u8){x:#u8;y:#u16;&~init(k:#u8){x=k;y=(#u16)k+300;}init(n);(x,y)}',
                     lambda n,total:[n,n+300],2)

    def test_persistent_and_external_edges(self):
        self.execute('&!get()->#u8;counter:#u8=get();&~bump(n:#u8)->#u8{counter+=n;counter}'
                     '&^entry(n:#u8){bump(n)}',lambda n,total:[(7+total)%256],
                     bindings='void vx_external_get(uint32_t *out){out[0]=7;}')

if __name__=='__main__':unittest.main(verbosity=2)
