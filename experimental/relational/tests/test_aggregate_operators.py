#!/usr/bin/env python3
"""Aggregate operations with independent values and execution-order checks."""
import ctypes
import itertools
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXE=Path(sys.argv.pop(1)).resolve()
ROOT=Path(__file__).resolve().parents[3]
ORDERS=list(itertools.product(('fifo','lifo','random'),('forward','reverse')))
def run(args):return subprocess.run([str(x) for x in args],text=True,capture_output=True,timeout=120)

class AggregateOperatorTests(unittest.TestCase):
    def check(self,source,expected=None,status='resolved',query='value:a'):
        with tempfile.TemporaryDirectory(prefix='vexel-aggregate-op-') as temp:
            path=Path(temp)/'source.vx';path.write_text(source)
            for schedule,branch in ORDERS:
                with self.subTest(source=source,schedule=schedule,branch=branch):
                    r=run([EXE,path,'--query',query,'--schedule',schedule,'--branch-order',branch])
                    self.assertIn(r.returncode,(0,1),r.stderr);report=json.loads(r.stdout)
                    self.assertEqual(report['status'],status,r.stdout+r.stderr)
                    if expected is not None:self.assertEqual(report['queries'][0]['answer'],expected)

    def execute(self,source,oracle,count=1):
        for schedule,branch in ORDERS:
            with self.subTest(source=source,schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-aggregate-op-c-') as temp:
                directory=Path(temp);path=directory/'source.vx';generated=directory/'generated';path.write_text(source)
                r=run([EXE,path,'--source','entry','--budget',count*4,'--emit-c',generated,'--schedule',schedule,'--branch-order',branch])
                self.assertEqual(r.returncode,0,r.stdout+r.stderr)
                harness=directory/'harness.c';harness.write_text('#include "vexel.h"\n#include <setjmp.h>\nstatic jmp_buf trapped;\n'
                    'void vx_trap(uint8_t r){longjmp(trapped,r);}\nunsigned checked(uint8_t n){int r=setjmp(trapped);if(r)return r;vx_entry(n);return 0;}\n')
                rt=ROOT/'backends/msx-neo8/runtime';runtime=directory/'runtime.c'
                runtime.write_text((rt/'vx_runtime.c').read_text().replace('void vx_trap(uint8_t reason)','void vx_unused_trap(uint8_t reason)',1))
                lib=directory/'run.so';r=run(['cc','-O2','-std=c11','-shared','-fPIC','-DVX_FIXED_RUNTIME=1','-DVX_FLOAT_RUNTIME=1',
                    '-I'+str(generated),'-I'+str(rt),generated/'vx_bridges.c',runtime,harness,'-o',lib])
                self.assertEqual(r.returncode,0,r.stderr)
                dll=ctypes.CDLL(str(lib));dll.checked.argtypes=[ctypes.c_uint8];dll.checked.restype=ctypes.c_uint
                dll.vx_entry_read.argtypes=[ctypes.c_uint16];dll.vx_entry_read.restype=ctypes.c_uint32
                for n in range(256):
                    expected=oracle(n);reason=dll.checked(n)
                    if isinstance(expected,tuple):self.assertEqual(reason,expected[0],n)
                    else:
                        self.assertEqual(reason,0,n)
                        self.assertEqual([dll.vx_entry_read(i) for i in range(count)],[v&0xffffffff for v in expected],n)

    def test_lexical_comparisons(self):
        for source,expected in [
            ('a=[1,2]<[1,3];','1'),('a=[1]<[1,0];','1'),('a=[1,0]>[1];','1'),
            ('a=[]<=[1];','1'),('a=[]>=[];','1'),('a=[1]!=[1,0];','1'),
            ('a=[(#i8)-1,2] < [(#i16)0,-1];','1'),
            ('a=[[1,2],[3,4]]<[[1,2],[3,5]];','1'),
            ('#P(x:#i8,y:#u8);a=#P(-1,255)<#P(0,0);','1'),
            ('a=["a","bb"]<["a","c"];','1'),
            ('nan=0.0/0.0;a=[nan,0.0]<[0.0,-1.0];','0'),
            ('nan=0.0/0.0;a=[0.0,nan]<[1.0,0.0];','1'),
            ('nan=0.0/0.0;a=[nan]==[nan];','0'),
            ('nan=0.0/0.0;a=[nan]!=[nan];','1'),
            ('a=?{x=?(#u8[2],0..3);?(x<[1,1]);?(x[0]==1);x};','Values(1,0)'),
        ]:self.check(source,expected)
        for source in ('a=[(#i8)1]<[(#u8)2];', 'a=[1,300]<[(#u8)2];',
                       '#P(x:#u8);#Q(x:#u8);a=#P(1)==#Q(1);'):
            self.check(source,status='contradictory')

    def test_runtime_comparisons_and_evaluation_order(self):
        self.execute('&^entry(n:#u8){a=[n,1];b=[(#u16)128,2];[a<b,a<=b,a==b,a!=b,a>=b,a>b]}',
                     lambda n:list(map(int,[(n,1)<(128,2),(n,1)<=(128,2),(n,1)==(128,2),(n,1)!=(128,2),(n,1)>=(128,2),(n,1)>(128,2)])),6)
        self.execute('#P(x:#i8,y:#u8);&^entry(n:#u8){#P((#i8)n,7)<#P(-1,8)}',lambda n:[int((n if n<128 else n-256,7)<(-1,8))])
        self.execute('count:#u8;&~take(n:#u8)->#u8[2]{count+=1;[n,count]}#P(same:#b,calls:#u8);&^entry(n:#u8){count=0;a=take(n)==take(n);#P(a,count)}',lambda n:[0,2],2)
        self.execute('&^entry(n:#u8){x=(#f16)n;[x,0]<[(#f16)128,(#f16)(0.0/0.0)]}',lambda n:[int(n<128)])

    def test_canonical_bytes_and_numeric_array_copies(self):
        for source,expected in [
            ('a=(#u8[2])((#u16)0x1234);','Values(52,18)'),
            ('a=(#i12)((#u8[2])[255,255]);','-1'),
            ('a=(#u8[2])((#i12)-1);','Values(255,15)'),
            ('a=(#i3.4)((#u8[1])[127]);','-1'),
            ('a=(#u8[1])((#i3.4)-1);','Values(112)'),
            ('a=(#f16)((#u8[2])[1,0]);','1'),
            ('a=(#u8[4])((#f32)-0.0);','Values(0,0,0,128)'),
            ('a=(#u8[2])((#f16)((#u8[2])[17,126]));','Values(17,126)'),
            ('a=(#u64)((#u8[8])[0,1,2,3,4,5,6,7]);',str(int.from_bytes(bytes(range(8)),'little'))),
            ('#P(a:#u8,b:#u16);a=(#u8[3])#P(1,0x0203);','Values(1,3,2)'),
            ('#P(a:#u8,b:#u16);p=(#P)((#u8[3])[1,3,2]);a=p.b;','515'),
            ('#P(a:#u8,b:#u16);p=(#P)((#u8[3])[1,3,2]);a=|(#u8[3])p|;','3'),
            ('a=(#u8[2])[300,-1];','Values(44,255)'),
            ('a=(#i8[2])[(#i16)128,-1];','Values(-128,-1)'),
            ('a={x:#u16[2]=[1,2];y=(#u8[4])x;x[0]=7;y};','Values(1,0,2,0)'),
            ('#P();a=(#u8[0])#P();','Values'),
            ('a=(#u8)((#b[8])[1,0,1,0,0,0,0,1]);','133'),
            ('a=(#b[8])((#u8)133);','Values(1,0,1,0,0,0,0,1)'),
            ('a=(#u8[2])((#b[2])[1,0]);','Values(1,0)'),
            ('a=(#b[2])((#u8[2])[1,0]);','Values(1,0)'),
            ('a=|(#s)"abc"|;','3'),
            ('a=(#u8[0])[];','Values'),
            ('a=(#u16[0])((#u8[2][0])[]);','Values'),
            ('a=(#u8[0])((#b[8][0])[]);','Values'),
        ]:self.check(source,expected)
        for source in ('a=(#u8[1])1;', 'a=(#u8[1])((#u16)1);',
                       'a=(#b[2])((#u8[2])[1,2]);','a=(#u8)((#b[7])[1,0,0,0,0,0,0]);',
                       'a=(#s[0])((#u8[0])[]);'):
            self.check(source,status='contradictory')

    def test_runtime_representation_and_discarded_validation(self):
        self.execute('&^entry(n:#u8){(#u8[2])((#i12)(#i8)n)}',lambda n:[n,0 if n<128 else 15],2)
        self.execute('&^entry(n:#u8){(#i12)((#u8[2])[n,255])}',lambda n:[-256+n])
        self.execute('&^entry(n:#u8){(#u8)((#b[8])n)}',lambda n:[n])
        self.execute('#P(flag:#b,data:#u16);&^entry(n:#u8){p=(#P)((#u8[3])[n,1,2]);p.data}',lambda n:[513] if n<2 else (16,))
        self.execute('&^entry(n:#u8){(#b[2])((#u8[2])[1,n]);7}',lambda n:[7] if n<2 else (16,))
        self.execute('#P(count:#u8,data:#u8[count]);p=#P(2,[1,2]);&^entry(n:#u8){(#[p])((#u8[3])[n,7,8]);9}',lambda n:[9] if n==2 else (19,))

if __name__=='__main__':unittest.main(verbosity=2)
