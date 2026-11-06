#!/usr/bin/env python3
"""Completion-path and loop-carried-state oracles, independent of emitted C."""
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
def run(args):
    return subprocess.run([str(a) for a in args],text=True,capture_output=True,timeout=120)
class CompletionTests(unittest.TestCase):
    def check(self, source, answer=None, status='resolved'):
        with tempfile.TemporaryDirectory(prefix='vexel-completion-') as temp:
            path=Path(temp)/'source.vx';path.write_text(source)
            for schedule,branch in ORDERS:
                r=run([EXE,path,'--query','value:a','--schedule',schedule,'--branch-order',branch])
                self.assertIn(r.returncode,(0,1),r.stderr)
                report=json.loads(r.stdout);self.assertEqual(report['status'],status,report)
                if answer is not None:self.assertEqual(report['queries'][0]['answer'],answer)
    def test_unrolled_completion(self):
        for source,answer in [
            ('&f(){-> 7;missing();}a=f();','7'),
            ('&f(n){n ? {-> 7;} : {-> 9;}}a=f(0);','9'),
            ('a={x:#u8=0;[3,1,2]@@{x=x*10+_;};x};','123'),
            ('a={x:#u8=0;[1,2,3]@{_==2 ? {->|;};x+=_;};x};','1'),
            ('a={x:#u8=0;[1,2,3]@{_==2 ? {->>;};x+=_;};x};','4'),
            ('&f(){[1,2,3]@{_==2 ? {-> _;};};0}a=f();','2'),
            ('a={x:#u8=0;(x<5)@{x+=1;};x};','5'),
            ('a={x:#u8=0;(x<5)@{x+=1;x==3 ? {->|;};};x};','3'),
            ('a={x:#u8=0;y:#u8=0;(x<5)@{x+=1;x==3 ? {->>;};y+=x;};y};','12'),
            ('a={i:#u8=0;s:#u8=0;({i+=1;i==2?{->>;};i==5?{->|;};1})@{s+=i;};s};','8'),
            ('&f(){({->7;})@{missing();};missing()}a=f();','7'),
            ('&f(){{->7;}@{missing();};missing()}a=f();','7'),
            ('&f(){{->7;}!@{missing();};missing()}a=f();','7'),
            ('a={s:#u8=0;[1,2,3]@{s+=_;{->|;}@{missing();};s+=10;};s};','1'),
            ('a={s:#u8=0;[1,2,3]@{s+=_;{->>;}!@{missing();};s+=10;};s};','6'),
        ]:
            with self.subTest(source=source):self.check(source,answer)
        self.check('a={x:#u8=0;(x==0)@{x=0;};x};',status='unresolved')
        self.check('a={[1,2]@{_=7;};0};',status='contradictory')
        self.check('a={({->>;})@{missing();};0};',status='unresolved')
    def test_runtime_completion_and_loops(self):
        cases=[
            ('&^entry(n:#u8){n<128 ? {-> (#u16)7;};(#u16)n}',lambda n:7 if n<128 else n),
            ('&^entry(n:#u8){n<128 ? {-> (#u16)7;} : {-> (#u16)9;}}',lambda n:7 if n<128 else 9),
            ('&^entry(n:#u8){x:#u16;n<128 ? {x=7;} : {-> (#u16)9;};x}',lambda n:7 if n<128 else 9),
            ('&^entry(n:#u8){x:#u16=0;i:#u16=0;(i<(#u16)n)!@{i+=1;x+=i;};x}',lambda n:n*(n+1)//2),
            ('&^entry(n:#u8){x:#u16=(#u16)n;[1,2,3]!@{x+=_;};x}',lambda n:n+6),
            ('&^entry(n:#u8){x:#u16=0;[1,2,3]!@{_==2 ? {->>;};x+=(#u16)n+_;};x}',lambda n:2*n+4),
            ('&^entry(n:#u8){x:#u16=0;[1,2,3]!@{_==2 ? {->|;};x+=(#u16)n+_;};x}',lambda n:n+1),
            ('&^entry(n:#u8){x:#u16=0;i:#u16=0;(i<(#u16)n)!@{i+=1;i==3 ? {-> (#u16)99;};x+=i;};x}',lambda n:99 if n>=3 else n*(n+1)//2),
            ('&^entry(n:#u8){x:#u16=(#u16)n;[1,2]!@{a=_;[3,4]!@{x+=a+_;};};x}',lambda n:n+20),
            ('&^entry(n:#u8){x:#u16=(#u16)n;&~bump(){x+=1;}[1,2,3]!@{bump();};x}',lambda n:n+3),
            ('&^entry(n:#u8){i:#u16=0;s:#u16=0;({i+=1;i==2?{->>;};i==5?{->|;};i<=(#u16)n})!@{s+=i;};s}',lambda n:sum(i for i in range(1,min(n,4)+1) if i!=2)),
            ('&^entry(n:#u8){i:#u16=0;s:#u16=0;({i+=1;i==3?{->(#u16)99;};i<=(#u16)n})!@{s+=i;};s}',lambda n:99 if n>=2 else n),
            ('&^entry(n:#u8){{n<128?{->(#u16)7;};[1,2]}@{->(#u16)n;};(#u16)9}',lambda n:7 if n<128 else n),
            ('&^entry(n:#u8){{n<128?{->(#u16)7;};[1,2]}!@{->(#u16)n;};(#u16)9}',lambda n:7 if n<128 else n),
        ]
        for source,oracle in cases:
            for schedule,branch in ORDERS:
                with self.subTest(source=source,schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-runtime-completion-') as temp:
                    temp=Path(temp);path=temp/'source.vx';path.write_text(source);out=temp/'generated'
                    r=run([EXE,path,'--source','entry','--budget','2','--emit-c',out,'--schedule',schedule,'--branch-order',branch])
                    self.assertEqual(r.returncode,0,r.stdout+r.stderr)
                    library=temp/'program.so'
                    r=run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(out),'-I'+str(ROOT/'backends/msx-neo8/runtime'),out/'vx_bridges.c',ROOT/'backends/msx-neo8/runtime/vx_runtime.c','-o',library])
                    self.assertEqual(r.returncode,0,r.stderr)
                    dll=ctypes.CDLL(str(library));dll.vx_entry.argtypes=[ctypes.c_uint8];dll.vx_entry_read.argtypes=[ctypes.c_uint16];dll.vx_entry_read.restype=ctypes.c_uint32
                    for n in range(256):
                        dll.vx_entry(n);self.assertEqual(dll.vx_entry_read(0),oracle(n),(n,source))
if __name__=='__main__':unittest.main(verbosity=2)
