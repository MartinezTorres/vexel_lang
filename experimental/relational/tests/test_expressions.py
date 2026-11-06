#!/usr/bin/env python3
"""Hygienic deferred arguments and transfers across expression boundaries."""
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
def run(args):return subprocess.run([str(a) for a in args],capture_output=True,text=True,timeout=120)
class ExpressionTests(unittest.TestCase):
    def query(self,source,answer):
        with tempfile.TemporaryDirectory(prefix='vexel-expression-') as temp:
            path=Path(temp)/'source.vx';path.write_text(source)
            for schedule,branch in ORDERS:
                r=run([EXE,path,'--query','value:a','--schedule',schedule,'--branch-order',branch])
                self.assertEqual(r.returncode,0,r.stdout+r.stderr)
                self.assertEqual(json.loads(r.stdout)['queries'][0]['answer'],answer)
    def test_hygiene_freshness_forwarding_and_arguments(self):
        for source,answer in [
            ('&twice($a){$a;$a}a={x:#u8=0;twice(x+=1);x};','2'),
            ('&use($a){x:#u8=9;$a}a={x:#u8=3;use(x)};','3'),
            ('&ignore($a){42}a=ignore(1/0);','42'),
            ('&choose(c:#b,$a,$b){c?$a:$b}a=choose(1,7,1/0);','7'),
            ('&twice($a){$a;$a}&forward($b){twice($b)}a={x:#u8=0;forward(x+=1);x};','2'),
            ('&mix($a,b:#u8){$a+b}a={x:#u8=0;mix(x+=1,x+=5)+x};','17'),
            ('&use($a:#u8){$a}a=use(8);','8'),
        ]:
            with self.subTest(source=source):self.query(source,answer)
    def test_transfers_have_lexical_targets(self):
        for source,answer in [
            ('&use($a){$a;undefined();}&f(){use({->7;});9}a=f();','7'),
            ('&use($a:#u8){$a;undefined();}&f(){use({->700;});9}a=f();','700'),
            ('&use($a:#u8){$a;undefined();}&forward($b:#u8){use($b);}&f(){forward({->700;});9}a=f();','700'),
            ('&use($a){$a;}&f(){s:#u8=0;0..4@{use({_==2?{->>;};});s+=_;};s}a=f();','4'),
            ('&each($a){0..4@{$a;};}&forward($b){each($b);}&f(){forward({->7;});undefined();}a=f();','7'),
            ('&repeat2($body){[1,2]@{$body;};}&f(){x:#u8=0;[1,2,3]@{repeat2({_==2?{->|;};x+=1;});};x}a=f();','2'),
            ('&repeat2($body){[1,2]@{$body;};}&f(){x:#u8=0;[1,2,3]@{repeat2({_==2?{->>;};x+=1;});};x}a=f();','4'),
        ]:
            with self.subTest(source=source):self.query(source,answer)
    def test_operand_transfer_stops_the_remaining_operation(self):
        expressions=['x:#u8={->7;};','x:#u8=1;x={->7;};','take({->7;});','({->7;})+missing();','({->7;}).x;','#P({->7;});','({->7;})?missing():missing();',
                     'data:#u8[2]=[1,2];data[{->7;}]=missing();','data:#u8[2]=[1,2];data[0]={->7;};','({->7;})@{missing();};']
        for expression in expressions:
            with self.subTest(expression=expression):self.query('#P(x:#u8);&take(n:#u8){n+1}&f(){'+expression+'9}a=f();','7')
    def test_runtime_deferred_state_and_caller_return(self):
        cases=[
            ('&^entry(n:#u8){({-> (#u16)7;})!@{missing();};(#u16)9}',lambda n:7),
            ('&^entry(n:#u8){[]!@{missing();};(#u16)9}',lambda n:9),
            ('&use($a:#u8){$a;}&^entry(n:#u8){use({n<128?{->(#u16)700;};7});(#u16)n}',lambda n:700 if n<128 else n),
            ('&twice($a){$a;$a}&forward($b){twice($b)}&^entry(n:#u8){x:#u8=n;forward(x+=1);x}',lambda n:(n+2)%256),
            ('&repeat(k:#u8,$body){i:#u8=0;(i<k)!@{$body;i+=1;};}&^entry(n:#u8){x:#u16=0;repeat(n,{x+=1;});x}',lambda n:n),
            ('&repeat(k:#u8,$body){i:#u8=0;(i<k)!@{$body;i+=1;};}&^entry(n:#u8){x:#u16=0;repeat(n,{x+=1;x==3?{-> (#u16)77;};});x}',lambda n:77 if n>=3 else n),
        ]
        for source,oracle in cases:
            for schedule,branch in ORDERS:
                with self.subTest(source=source,schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-expression-c-') as temp:
                    temp=Path(temp);path=temp/'source.vx';path.write_text(source);out=temp/'generated'
                    r=run([EXE,path,'--source','entry','--budget','2','--emit-c',out,'--schedule',schedule,'--branch-order',branch])
                    self.assertEqual(r.returncode,0,r.stdout+r.stderr)
                    lib=temp/'program.so';r=run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(out),'-I'+str(ROOT/'backends/msx-neo8/runtime'),out/'vx_bridges.c',ROOT/'backends/msx-neo8/runtime/vx_runtime.c','-o',lib]);self.assertEqual(r.returncode,0,r.stderr)
                    dll=ctypes.CDLL(str(lib));dll.vx_entry.argtypes=[ctypes.c_uint8];dll.vx_entry_read.argtypes=[ctypes.c_uint16];dll.vx_entry_read.restype=ctypes.c_uint32
                    for n in range(256):dll.vx_entry(n);self.assertEqual(dll.vx_entry_read(0),oracle(n),n)
if __name__=='__main__':unittest.main(verbosity=2)
