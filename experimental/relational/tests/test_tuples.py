#!/usr/bin/env python3
"""Tuple properties, representation, snapshots, destructuring and native frames."""
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

class TupleTests(unittest.TestCase):
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
                lib=directory/'run.so';r=run(['cc','-O2','-std=c11','-shared','-fPIC','-DVX_FIXED_RUNTIME=1','-DVX_FLOAT_RUNTIME=1','-DVX_MATH_RUNTIME=1',
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

    def test_independent_properties_and_conversions(self):
        cases=[
            ('a=((#u8)1,(#u16)2);','(#u8,#u16)','type:a'),
            ('a=((#u8)1,missing()).__0;','#u8','type:a'),
            ('a: (#u8,#Missing);','#u8','type:a.__0'),
            ('a=((#u8[2])[1,2],missing());','2','length:a.__0'),
            ('a=((#u8)1,((#i8)-2,missing()));','#i8','type:a.__1.__0'),
            ('a={p=((#u8)3,(#u16)4);p.__0=7;p.__0};','7','value:a'),
            ('a=((#u8,#u16))((#u8[3])[1,2,3]);','RecordValue(Tuple(2),Values(1,770))','value:a'),
            ('a=(#u8[3])((#u8)1,(#u16)770);','Values(1,2,3)','value:a'),
            ('a=[((#u8)255,(#i8)-1),((#u16)2,(#i16)-2)];','(#u16,#i16)[2]','type:a'),
            ('&f(x:(#u8,#u16)){x.__0+x.__1}&f(x:#u8){x}a=f((1,300));','301','value:a'),
            ('&parts(x:#u16)->(#u16,#u16){(x/10,x%10)}a={p=parts(42);p.__0+p.__1};','6','value:a'),
            ('a=((#u8)1,(#i8)-1)<((#u16)1,(#i16)0);','1','value:a'),
            ('a=?{x=?(#u8,0..4);y=?(#u8,0..4);?((x,y)==(1,2));x+y};','3','value:a'),
        ]
        for source,expected,query in cases:self.check(source,expected,query=query)
        for source in ('a=(1,2)==(1,2,3);','a=((#u8,#u16))(1,2,3);'):
            self.check(source,status='contradictory')

    def test_destructuring_order_state_and_rejections(self):
        cases=[
            ('a={x,y=(2,3);x,y=(y,x);x*10+y};','32'),
            ('a={i:#u8=0;&pair(){i+=1;(i,i+1)} x,y=pair();x+y+i};','4'),
            ('a={x:#u8=1;y:#u16=2;x,y=(3,4);x+(#u8)y};','7'),
            ('a={x:#u8=1;x,x=(2,3);x};','3'),
            ('a={x:#u8; y:#u8; x,y=(1,2);x+y};','3'),
            ('a={p=((#u8)1,(#u8)2);x,y=(p,p);x.__0=9;y.__0};','1'),
            ('a={count:#u8=0;&pair(){count+=1;count==1 ? ((#u8)1,0) : ((#u16)2,0)}count=1;x,y=pair();v:#[x]=300;v};','300'),
        ]
        for source,expected in cases:self.check(source,expected)
        for source in ('a={x,y=(1,2,3);x};','#P(x,y);a={x,y=#P(1,2);x};',
                       'a={x:#u8=1;y:#u8=2;x,y=(999,3);x};'):
            self.check(source,status='contradictory')

    def test_nested_schema_requirements_and_partial_identity(self):
        self.check('#P(count,data:#u8[count+1]);&read(p:(#P,#u8)){p.__0.data[0]}a=read((#P((#u8)2,[1,2,3]),9));','1')
        self.check('#P(count,data:#u8[count+1]);&read(p:#P[1]){p[0].data[0]}a=read([#P((#u8)2,[1,2,3])]);','1')
        self.check('#P(count,data:#u8[count+1]);&read(p:(#P[1],#u8)){p.__0[0].data[0]}a=read(([#P((#u8)2,[1,2,3])],9));','1')
        # Identity is independent of the complete field schema, including when
        # a helper puts the owner inside another aggregate and projects it out.
        for body in ('value','(value,(#u8)1).__0','#Box(value,(#u8)1).x'):
            self.check('#Box(x,y);#Pair(a:#[forward(b)],b:#[forward(a)]);&forward(value){'+body+'}'
                       'a={v:#[#Pair(#Box((#u8)1,missing()),#Box((#u16)2,other())).a.x]=300;v};','300')
        self.check('#P(n:#u8,bytes:#u8[n]);a={p=(#P(2,[1,2]),0);p.__0.n=3;0};',status='contradictory')

    def test_runtime_frames_captures_and_completion(self):
        self.execute('&~parts(n:#u8)->(#u8,#u16){(n,(#u16)n+300)}&^entry(n:#u8){x,y=parts(n);(x,y)}',lambda n:[n,n+300],2)
        self.execute('&^entry(n:#u8){x:#u8=n;y:#u8=n+1;&~swap(){x,y=(y,x);}swap();(x,y)}',lambda n:[(n+1)%256,n],2)
        self.execute('&^entry(n:#u8){x:#u8;y:#u16;&~init(k:#u8){x,y=(k,(#u16)k+300);}init(n);(x,y)}',lambda n:[n,n+300],2)
        self.execute('&^entry(n:#u8){x:#u8=n;y:#u8=n+1;i:#u8=0;(i<3)!@{x,y=(y,x);i+=1;};(x,y)}',lambda n:[(n+1)%256,n],2)
        self.execute('&^entry(n:#u8){p=(n,(#u16)n+300);n<128 ? p : ((#u8)0,(#u16)0)}',lambda n:[n,n+300] if n<128 else [0,0],2)
        self.execute('&^entry(n:#u8)->#u16{x:#u8=1;y:#u8=2;x,y=(n,{n<128 ? {->(#u16)n+300;};n});(#u16)x+y}',lambda n:[n+300 if n<128 else 2*n])
        self.execute('&^entry(n:#u8){(#u8[3])((#u8)n,(#u16)n+300)}',lambda n:[n,(n+300)%256,(n+300)//256],3)

if __name__=='__main__':unittest.main(verbosity=2)
