#!/usr/bin/env python3
"""Universal native-input proofs expose interfaces without inventing values."""
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

class FunctionTypeTests(unittest.TestCase):
    def query(self,source,answer=None,status='resolved',query='type:a'):
        with tempfile.TemporaryDirectory(prefix='vexel-function-interface-') as temp:
            path=Path(temp)/'source.vx';path.write_text(source)
            for schedule,branch in ORDERS:
                with self.subTest(source=source,schedule=schedule,branch=branch,query=query):
                    result=run([EXE,path,'--query',query,'--schedule',schedule,'--branch-order',branch])
                    self.assertIn(result.returncode,(0,1),result.stdout+result.stderr)
                    report=json.loads(result.stdout);self.assertEqual(report['status'],status,report)
                    if answer is not None:self.assertEqual(report['queries'][0]['answer'],answer,report)

    def test_unknown_arguments_and_closed_return_interfaces(self):
        cases=[
            ('&f(n:#b){n?{->(#u8)1;};(#u8)2}a={flag:#b;f(flag)};','#u8'),
            ('&~f(n:#b){n?{->(#u8)1;};(#u8)2}a={flag:#b;f(flag)};','#u8'),
            ('&f(n:#b){n?{->(#u8)1;};(#u8)2}&g(n:#b){f(n)}a={flag:#b;g(flag)};','#u8'),
            ('&f(n:#b){n?(#u8)1:(#u8)2}a={flag:#b;f(flag)};','#u8'),
            ('&f(n:#b){n?{->(#u8[2])[1,2];};(#u8[2])[3,4]}a={flag:#b;f(flag)};','#u8[2]'),
            ('&f(n:#b[2]){n[0]?{->(#u8)1;};(#u8)2}a={flags:#b[2];f(flags)};','#u8'),
            ('#P(flag:#b,data:#u8[2]);&f(p:#P){p.flag?{->p.data;};p.data}a={p:#P;f(p)};','#u8[2]'),
            ('#P(x:#u8);&f(n:#b){n?{->#P(1);};#P(2)}a={flag:#b;f(flag)};','#P(x:#u8)'),
            ('&f(n:#b){n?{->((#u8)1,(#i16)-2);};((#u8)2,(#i16)-3)}a={flag:#b;f(flag)};','(#u8,#i16)'),
            ('&f(n:#b){n?{->(#f64)1;};(#f64)2}a={flag:#b;f(flag)};','#f64'),
            ('&f(n:#b){n?{->(#i8.8)1;};(#i8.8)2}a={flag:#b;f(flag)};','#i8.8'),
            ('&f(n:#b){n?{->"ok";};"ok"}a={flag:#b;f(flag)};','#s'),
            ('&~f(n:#u8){n==0?{->(#u16)1;};f(n-1)+(#u16)1}a={n:#u8;f(n)};','#u16'),
            ('&f(n:#b){n?{->(#u8)1;}:{->(#u8)2;}missing()}a={flag:#b;f(flag)};','#u8'),
        ]
        for source,answer in cases:self.query(source,answer)
        # Signature availability does not validate or execute an argument body.
        self.query('&unknown()->#b{missing()}&f(n:#b){n?{->(#u8)1;};(#u8)2}a=f(unknown());','#u8')
        self.query('a={flag:#b;f(flag)};&f(n:#b){n?{->(#u8)1;};(#u8)2}','#u8')

    def test_proof_failure_falls_back_to_the_actual_call(self):
        for value,expected in [(0,'#u8'),(1,'#u16')]:
            source='&f(n:#b){n?{->(#u16)7;};(#u8)9}a=f('+str(value)+');'
            self.query(source,expected)
            self.query(source,'7' if value else '9',query='value:a')
        self.query('&f(n:#b){n?missing():(#u8)7}a=f(0);','#u8')
        self.query('&f(n:#u8,flag:#b){flag?{->(#u8[n])[1,2];};(#u8[n])[3,4]}a=f(2,1);','#u8[2]')
        self.query('&f(n:#u8){#P(x:#u8[n]);#P([1,2])}a=f(2);',
                   '#P(x:#u8[2]){Captures(n(2),type:n(UInt(8)))}')
        self.query('&make(offset:#u8,n:#u8,x){#P(n:#u8,data:#[x][n+offset]);#P(n,[x,x,x])}'
                   'a=make(1,2,(#u8)7);','#P(n:#u8,data:#u8[3]){Captures(offset(1),type:offset(UInt(8)),type:x(UInt(8)))}')

    def test_no_value_or_interface_is_invented(self):
        same='&f(n:#b){n?{->(#u8)1;};(#u8)2}a={flag:#b;f(flag)};'
        self.query(same,status='unresolved',query='value:a')
        self.query('&f(n:#b){n?{->(#u16)1;};(#u8)2}a={flag:#b;f(flag)};',status='unresolved')
        self.query('&~f(n:#u8){f(n)}a={n:#u8;f(n)};',status='unresolved')
        # Array extents depend on the argument value, not just its native type.
        self.query('&f(n:#u8){#P(x:#u8[n]);#P([1,2])}a={count:#u8;f(count)};',status='unresolved')

    def test_abstract_proofs_cannot_prune_concrete_source_choices(self):
        # The array branch is valid at n=0. Treating n as a runtime prototype
        # cannot turn the other branch into a theorem about this invocation.
        for marker in ('&','&~'):
            source=marker+'f(n:#u8){?{p=?(#b,[0,1]);p ? (#u8[n])[] : (#u8)7}}a=f(0);'
            self.query(source,status='ambiguous')
            self.query(source,status='ambiguous',query='value:a')
            source=marker+'f(n:#u8){?{p=?(#b,[0,1]);?(p);p ? (#u8[n])[] : (#u8)7}}a=f(0);'
            self.query(source,'#u8[0]')
            self.query(source,'Values',query='value:a')

    def test_emitted_annotation_uses_only_the_proved_type(self):
        source='&!emit(n:#u8);&infer(flag:#b){emit(7);flag?{->(#u16)1;};(#u16)2}' \
               '&^entry(n:#u8){flag:#b;answer:#[infer(flag)]=n;answer}'
        for schedule,branch in ORDERS:
            with self.subTest(schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-type-only-c-') as temp:
                temp=Path(temp);path=temp/'source.vx';path.write_text(source);out=temp/'generated'
                result=run([EXE,path,'--source','entry','--budget','2','--emit-c',out,'--schedule',schedule,'--branch-order',branch])
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                report=json.loads(result.stdout);self.assertEqual(report['source_accesses']['entry'],[])
                self.assertEqual(report['runtime_functions'],[])
                code=(out/'vx_bridges.c').read_text();self.assertNotIn('TypeProbeInput',code);self.assertNotIn('vx_invoke_external_emit',code)
                library=temp/'program.so'
                compiled=run(['cc','-O2','-std=c11','-shared','-fPIC','-I'+str(out),'-I'+str(ROOT/'backends/msx-neo8/runtime'),
                              out/'vx_bridges.c',ROOT/'backends/msx-neo8/runtime/vx_runtime.c','-o',library])
                self.assertEqual(compiled.returncode,0,compiled.stderr)
                dll=ctypes.CDLL(str(library));dll.vx_entry.argtypes=[ctypes.c_uint8]
                dll.vx_entry_read.argtypes=[ctypes.c_uint16];dll.vx_entry_read.restype=ctypes.c_uint32
                for n in range(256):dll.vx_entry(n);self.assertEqual(dll.vx_entry_read(0),n)

if __name__=='__main__':unittest.main(verbosity=2)
