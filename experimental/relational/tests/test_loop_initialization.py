#!/usr/bin/env python3
"""Initialization safety is independent of the bits allocated for a loop frame."""
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

class LoopInitializationTests(unittest.TestCase):
    def execution(self,source,oracle,bounded_report=False):
        for schedule,branch in ORDERS:
            with self.subTest(source=source,schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-loop-init-') as temp:
                temp=Path(temp);path=temp/'source.vx';path.write_text(source);out=temp/'generated'
                result=run([EXE,path,'--source','entry','--budget','16','--emit-c',out,'--schedule',schedule,'--branch-order',branch])
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                if bounded_report:
                    self.assertLess(len(result.stdout),65536)
                    report=json.loads(result.stdout)
                    self.assertTrue(any(leaf.get('value_truncated') for leaf in report['sources'][0]['leaves']))
                library=temp/'program.so'
                compiled=run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(out),'-I'+str(ROOT/'backends/msx-neo8/runtime'),out/'vx_bridges.c',ROOT/'backends/msx-neo8/runtime/vx_runtime.c','-o',library])
                self.assertEqual(compiled.returncode,0,compiled.stderr)
                dll=ctypes.CDLL(str(library));dll.vx_entry.argtypes=[ctypes.c_uint8]
                dll.vx_entry_read.argtypes=[ctypes.c_uint16];dll.vx_entry_read.restype=ctypes.c_uint32
                for n in range(256):
                    dll.vx_entry(n)
                    expected=oracle(n)
                    if not isinstance(expected,list):expected=[expected]
                    self.assertEqual([dll.vx_entry_read(i) for i in range(len(expected))],[v&0xffffffff for v in expected],(n,source))

    def test_shared_loop_facts_and_report_previews(self):
        # The safety graph retains these updates before physical demand drops
        # the unused slot. Its shared expression has millions of printed paths.
        source=('&^entry(n:#u8){key:#u8=n;x:#u8=n;i:#u8=0;(i<1)!@{'
                +'x=x*x+key;'*18+'i+=1;};n}')
        self.execution(source,lambda n:n,bounded_report=True)

    def test_first_writes_and_aggregate_storage(self):
        for source,oracle in [
            ('&^entry(n:#u8){x:#u16;[1,2,3]!@{x=(#u16)n+_;};x}',lambda n:n+3),
            ('&^entry(n:#u8){x:#u16;n<128?{x=n;};[1,2,3]!@{x=(#u16)n+_;};x}',lambda n:n+3),
            ('&^entry(n:#u8){unused:#Missing;[1,2]!@{};n}',lambda n:n),
            ('&^entry(n:#u8){x:#i16[2];[1,2,3]!@{x=[(#i16)n-128,-(#i16)_];x[0]+=x[1];};x}',lambda n:[n-131,-3]),
            ('#P(x:#u16,y:#u16);&^entry(n:#u8){p:#P;[1,2,3]!@{p=#P(n,_);p.x+=p.y;};p}',lambda n:[n+3,3]),
        ]:self.execution(source,oracle)

    def test_control_and_invariant_guards(self):
        for source,oracle in [
            ('&^entry(n:#u8){x:#u16;(1)!@{x=(#u16)n+7;->|;};x}',lambda n:n+7),
            ('&^entry(n:#u8){x:#u16;({x=n;0})!@{};x}',lambda n:n),
            ('&^entry(n:#u8){x:#u16;(1)!@{n<128?{->(#u16)7;};x=n;->|;};x}',lambda n:7 if n<128 else n),
            ('&^entry(n:#u8){x:#u16;[1,2,3]!@{n<128?{x=(#u16)n+_;};};n<128?x:(#u16)0}',lambda n:n+3 if n<128 else 0),
            ('&^entry(n:#u8){x:#u16;[1,2,3]!@{n<128?{->>;};x=(#u16)n+_;};n<128?(#u16)0:x}',lambda n:0 if n<128 else n+3),
            ('&^entry(n:#u8){x:#u16;({x=n;n<128?{->|;};0})!@{};x}',lambda n:n),
        ]:self.execution(source,oracle)

    def test_nested_loops_and_captured_writes(self):
        for source,oracle in [
            ('&^entry(n:#u8){x:#u16;[1,2]!@{[3,4]!@{x=(#u16)n+_;};};x}',lambda n:n+4),
            ('&^entry(n:#u8){x:#u16;&set(){x=n;}[1,2]!@{set();};x}',lambda n:n),
            ('&^entry(n:#u8){x:#u16;&~set(k:#u8){x=k;}[1,2]!@{set(n);};x}',lambda n:n),
            ('&^entry(n:#u8){x:#u16;[1,2]!@{x=n;[3,4]!@{x+=_;};};x}',lambda n:n+7),
        ]:self.execution(source,oracle)

    def test_short_circuit_read_guards(self):
        for source,oracle in [
            ('&^entry(n:#u8){x:#b;[1,2]!@{n<128?{x=1;};};(n<128)&&x}',lambda n:int(n<128)),
            ('&^entry(n:#u8){x:#b;[1,2]!@{n<128?{x=0;};};!(n<128)||x}',lambda n:int(n>=128)),
            ('&^entry(n:#u8){x:#b;[1,2]!@{n<128?{x=1;};};flag:#b=n<128;flag&&=x;flag}',lambda n:int(n<128)),
            ('&^entry(n:#u8){x:#b;[1,2]!@{n<128?{x=0;};};flag:#b=!(n<128);flag||=x;flag}',lambda n:int(n>=128)),
            ('&^entry(n:#u8){x:#u16;n<128?{x=n;};n<128?{x+=1;x}:(#u16)0}',lambda n:n+1 if n<128 else 0),
        ]:self.execution(source,oracle)

    def test_missing_initialization_is_never_a_zero_value(self):
        sources=[
            '&^entry(n:#u8){x:#u16;[1,2]!@{x+=n;};x}',
            '&^entry(n:#u8){x:#u16;[]!@{x=n;};x}',
            '&^entry(n:#u8){x:#u16;i:#u16=0;(i<(#u16)n)!@{x=i;i+=1;};x}',
            '&^entry(n:#u8){x:#u16;[1,2]!@{n<128?{->>;};x=n;};x}',
            '&^entry(n:#u8){x:#u16;(1)!@{n<128?{->|;};x=n;->|;};x}',
            '&^entry(n:#u8){x:#u16;n<128?{x=n;};[1,2]!@{x+=1;};x}',
            '&^entry(n:#u8){x:#u16;[1,2]!@{[3,4]!@{x+=n;};};x}',
            '&^entry(n:#u8){x:#u16;n<128?{x=n;};x+=1;x}',
        ]
        for source in sources:
            with tempfile.TemporaryDirectory(prefix='vexel-loop-init-absence-') as temp:
                path=Path(temp)/'source.vx';path.write_text(source)
                for schedule,branch in ORDERS:
                    with self.subTest(source=source,schedule=schedule,branch=branch):
                        result=run([EXE,path,'--source','entry','--budget','2','--schedule',schedule,'--branch-order',branch])
                        self.assertEqual(result.returncode,1,result.stdout+result.stderr)
                        self.assertEqual(json.loads(result.stdout)['status'],'unresolved')

if __name__=='__main__':unittest.main(verbosity=2)
