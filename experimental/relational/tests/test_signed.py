#!/usr/bin/env python3
"""Signed arithmetic and representation boundaries against integer oracles."""
import ctypes
import itertools
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXE = Path(sys.argv.pop(1)).resolve()
ROOT = Path(__file__).resolve().parents[3]
ORDERS = list(itertools.product(('fifo', 'lifo', 'random'), ('forward', 'reverse')))
MASK = (1 << 32) - 1

def wrap(n, bits=8):
    return (n + (1 << (bits-1))) % (1 << bits) - (1 << (bits-1))

def quotient(a, b):
    return (abs(a)//abs(b)) * (-1 if (a < 0) != (b < 0) else 1)

def run(args):
    return subprocess.run([str(x) for x in args], text=True, capture_output=True, timeout=120)

class SignedTests(unittest.TestCase):
    def check(self, source, expected=None, status='resolved', query='value:a'):
        with tempfile.TemporaryDirectory(prefix='vexel-signed-') as temp:
            path = Path(temp)/'source.vx'; path.write_text(source)
            for schedule, branch in ORDERS:
                with self.subTest(source=source, schedule=schedule, branch=branch):
                    r = run([EXE, path, '--query', query, '--schedule', schedule, '--branch-order', branch])
                    self.assertIn(r.returncode, (0, 1), r.stderr)
                    report = json.loads(r.stdout)
                    self.assertEqual(report['status'], status, r.stdout+r.stderr)
                    if expected is not None:
                        self.assertEqual(report['queries'][0]['answer'], expected)

    def test_constants_types_and_finite_constraints(self):
        for source, expected in [
            ('a=(#i8)255;', '-1'), ('a=(#i8)128;', '-128'),
            ('a=(#i8)127+1;', '-128'), ('a=(#i8)-128-1;', '127'),
            ('a=~(#i8)0;', '-1'), ('a=|(#i8)-128|;', '-128'),
            ('a=|(#i8)-7|;', '7'), ('a=(#i8)-7/3;', '-2'),
            ('a=(#i8)-7%3;', '-1'), ('a=(#i8)-128/-1;', '-128'),
            ('a=(#i8)-5 >> (#u16)1;', '-3'), ('a=(#i8)-5 << 1;', '-10'),
            ('a=(#i32)0x80000000/-1;', '-2147483648'),
            ('a=(#u8)(#i8)-1;', '255'), ('a=(#i16)(#i8)-1;', '-1'),
            ('a=((#i8)-1 < 0);', '1'),
            ('&f(n:#i8){1}&f(n:#u8){2}a=f((#i8)-1);', '1'),
            ('a=?{n=?(#i8,-4..4);?(n+2 == 0);n};', '-2'),
            ('x:#[y]=-1;y:#[x]=(#i8)7;a=x+y;', '6'),
        ]:
            self.check(source, expected)
        self.check('a=(#i8)-1+(#i16)2;', '#i16', query='type:a')
        for source in ('a=(#i8)1+(#u8)2;', 'a:#i8=128;', 'a:#i8=-129;',
                       'a:#i8=(#i16)1;', 'a=(#i8)1<<8;', 'a=(#i8)1>>-1;'):
            self.check(source, status='contradictory')

    def execute(self, source, oracle, count=1, bindings='', signed=False, samples=None):
        for schedule, branch in ORDERS:
            with self.subTest(source=source, schedule=schedule, branch=branch), tempfile.TemporaryDirectory(prefix='vexel-signed-c-') as temp:
                directory=Path(temp); path=directory/'source.vx'; generated=directory/'generated'
                path.write_text(source)
                r=run([EXE,path,'--source','entry','--budget',str(count*4),'--emit-c',generated,
                       '--schedule',schedule,'--branch-order',branch])
                self.assertEqual(r.returncode,0,r.stdout+r.stderr)
                parameter='int8_t' if signed else 'uint8_t'
                harness=directory/'harness.c'
                harness.write_text('#include "vexel.h"\n#include <setjmp.h>\nstatic jmp_buf trapped;\n'
                    'void vx_trap(uint8_t r){longjmp(trapped,r);}\n'+bindings+'\n'
                    'unsigned checked('+parameter+' n){int r=setjmp(trapped);if(r)return r;vx_entry(n);return 0;}\n')
                runtime=directory/'runtime.c'
                runtime.write_text((ROOT/'backends/msx-neo8/runtime/vx_runtime.c').read_text().replace(
                    'void vx_trap(uint8_t reason)','void vx_unused_trap(uint8_t reason)',1))
                library=directory/'test.so'
                r=run(['cc','-O2','-std=c11','-shared','-fPIC','-I'+str(generated),
                       '-I'+str(ROOT/'backends/msx-neo8/runtime'),generated/'vx_bridges.c',runtime,harness,'-o',library])
                self.assertEqual(r.returncode,0,r.stderr)
                dll=ctypes.CDLL(str(library));dll.checked.argtypes=[ctypes.c_int8 if signed else ctypes.c_uint8]
                dll.checked.restype=ctypes.c_uint;dll.vx_entry_read.argtypes=[ctypes.c_uint16];dll.vx_entry_read.restype=ctypes.c_uint32
                for n in samples if samples is not None else range(-128 if signed else 0,128 if signed else 256):
                    expected=oracle(n);reason=dll.checked(n)
                    if isinstance(expected,tuple):
                        self.assertEqual(reason,expected[0],n)
                    else:
                        self.assertEqual(reason,0,n)
                        self.assertEqual([dll.vx_entry_read(i) for i in range(count)],[v&MASK for v in expected],n)

    def test_runtime_arithmetic(self):
        self.execute('&^entry(n:#u8){x=(#i8)n;[x+3,x-3,x*3,x/3,x%3,x>>1,x<<1,|x|,~x]}',
                     lambda n:[wrap(wrap(n)+3),wrap(wrap(n)-3),wrap(wrap(n)*3),quotient(wrap(n),3),
                               wrap(n)-quotient(wrap(n),3)*3,wrap(n)>>1,wrap(wrap(n)<<1),wrap(abs(wrap(n))),wrap(~wrap(n))],9)
        self.execute('&^entry(n:#i8){[n < 0,n <= -1,n == -1,n != -1,n >= 0,n > 0]}',
                     lambda n:list(map(int,[n<0,n<=-1,n==-1,n!=-1,n>=0,n>0])),6,signed=True)
        self.execute('&^entry(n:#i8){[(#i32)n,(#i16)n,(#i8)(-n)]}',lambda n:[n,n,wrap(-n)],3,signed=True)
        self.execute('&^entry(n:#u8){x=(#i32)(0x80000000+(#u32)n);[x/-1,x%3,x>>7]}',
                     lambda n:[wrap(-(-2147483648+n),32),(-2147483648+n)-quotient(-2147483648+n,3)*3,(-2147483648+n)>>7],3)

    def test_wide_constant_integers_and_target_obligations(self):
        for source,expected in [
            ('a=(#u128)-1;',str((1<<128)-1)),
            ('a=(#i128)(1<<127);',str(-(1<<127))),
            ('a=(#i128)-1>>100;','-1'),
            ('a=(#u96)(1<<95)*2;','0'),
            ('a=(#i512)-7/3;','-2'),
            ('a=|(#i128)(1<<127)|;',str(-(1<<127))),
            ('a=?{x=?(#i128,-4..4);?(x+2==0);x};','-2'),
            ('a=(#u65536)7;','7'),
        ]: self.check(source,expected)
        self.check('a=(#u64)1+(#u128)2;','#u128',query='type:a')
        self.check('a:#u65537;',status='exhausted',query='type:a')
        self.check('a:#u0;',status='contradictory',query='type:a')
        for source in ('&^entry(n:#u8){(#u8)((#u64)n>>40)}',
                       '&f(n:#u64){n>0x100000000}&^entry(n:#u8){f(n)}'):
            self.check(source,status='unresolved',query='value:entry')

    def test_calls_captures_loops_and_storage(self):
        for source in (
            '&~f(x:#i8)->#i8{x+1}&^entry(n:#i8){f(n)}',
            '&^entry(n:#i8){x:#i8=n;&~f()->#i8{x+=1;x}f()}',
            'x:#i8;&^entry(n:#i8){x=n;x+=1;x}',
            '[[msx_address(0xC100)]]!!x:#i8;&^entry(n:#i8){x=n;x+=1;x}',
            '&^entry(n:#i8){x:#i8=n;[0]!@{x+=1;};x}',
        ):
            self.execute(source,lambda n:[wrap(n+1)],signed=True)
        self.execute('&!f(x:#i8)->#i8;&^entry(n:#i8){f(n)}',lambda n:[n],signed=True,
                     bindings='void vx_external_f(uint32_t *r,int8_t n){r[0]=(uint32_t)n;}')
        self.execute('&!f(x:#u8)->#i8;&^entry(n:#u8){f(n)}',lambda n:[n] if n<128 else (16,),
                     bindings='void vx_external_f(uint32_t *r,uint8_t n){r[0]=n;}')
        self.execute('&^entry(n:#i7){n}',lambda n:[n] if -64<=n<=63 else (16,),signed=True)
        self.execute('&^entry(n:#u8){(#i8)1 >> n;7}',lambda n:[7] if n<8 else (2,))
        self.execute('&^entry(n:#i8){n/n;7}',lambda n:[7] if n else (1,),signed=True)

if __name__=='__main__':
    unittest.main(verbosity=2)
