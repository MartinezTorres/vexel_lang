#!/usr/bin/env python3
"""Byte literals, immutable strings and runtime reads into known contents."""
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
ORDERS = list(itertools.product(('fifo','lifo','random'),('forward','reverse')))


def run(args):
    return subprocess.run([str(arg) for arg in args], text=True, capture_output=True, timeout=120)


class StringTests(unittest.TestCase):
    def check(self, source, expected=None, query='value:a', status='resolved'):
        with tempfile.TemporaryDirectory(prefix='vexel-string-') as temporary:
            path = Path(temporary)/'source.vx'
            path.write_text(source)
            for schedule, branch in ORDERS:
                with self.subTest(source=source, schedule=schedule, branch=branch):
                    r = run([EXE,path,'--query',query,'--schedule',schedule,'--branch-order',branch])
                    self.assertIn(r.returncode,(0,1),r.stderr)
                    report = json.loads(r.stdout)
                    self.assertEqual(report['status'],status,r.stdout+r.stderr)
                    if expected is not None:
                        self.assertEqual(report['queries'][0]['answer'],expected)

    def test_literals_lengths_and_aliases(self):
        for source, expected in [(r"a='\xFF';", '255'), (r"a='\0';", '0'),
                                 (r'a="A\0B";', 'Bytes(65,0,66)'),
                                 (r'a=|"A\0B"|;', '3'), ('a=|""|;', '0'),
                                 ('a="abc"[1];', '98'), ('a=(#s)"abc";', 'Bytes(97,98,99)'),
                                 ('&f(s:#s){|s|}a=f("ab");', '2'),
                                 ('&f(s:#s){|s|}&f(n:#u8){n+1}a=f("abc");', '3'),
                                 ('&f(s:#s){|s|}&f(n:#u8){n+1}a=f((#u8)7);', '8'),
                                 ('x:#[y]="a";y:#[x]="abc";a=|x|*10+|y|;', '13'),
                                 ('a={s:#s="a";s="ab";|s|};', '2')]:
            self.check(source, expected)
        self.check("a='a';", '#u8',query='type:a')
        self.check('a="abc";', '#s',query='type:a')
        self.check('#P(s:#s);p=#P("ab");a=|p.s|;', '2')
        self.check('#P(s:#s);p=#P("ab");', '2',query='length:p.s')
        self.check('#P(s:#s,bad:#Missing);p:#P;a={v:#[p.s[0]]=7;v};', '7')
        self.check('#P(s:#s,bad:#Missing);p=#P("ab",missing());',query='length:p.s',status='unresolved')
        self.check('a={s="ab";#P(n:#u8,data:#u8[|s|]);p=#P(2,[1,2]);s="abc";p.n=2;|p.data|};', '2')
        for source in ('a="abc"[3];','a=""[0];','a={s="abc";s[0]=1;0};','a=(#s)7;'):
            self.check(source,status='contradictory')

    def test_unsigned_lexical_comparisons(self):
        strings = ['', 'A', 'AB', 'B', '\x00', '\xff']
        operations = [('==',lambda a,b:a==b),('!=',lambda a,b:a!=b),('<',lambda a,b:a<b),
                      ('<=',lambda a,b:a<=b),('>',lambda a,b:a>b),('>=',lambda a,b:a>=b)]
        def literal(value):
            return '"'+''.join('\\x%02x'%ord(c) for c in value)+'"'
        for a,b in itertools.product(strings,repeat=2):
            expressions = [literal(a)+op+literal(b) for op,_ in operations]
            self.check('a=['+','.join(expressions)+'];',
                       'Values('+','.join(str(int(compare(a,b))) for _,compare in operations)+')')

    def test_runtime_indexing_and_discarded_bounds(self):
        for discarded in (False, True):
            for schedule,branch in ORDERS:
                with tempfile.TemporaryDirectory(prefix='vexel-string-c-') as temporary:
                    directory=Path(temporary);path=directory/'source.vx';generated=directory/'generated'
                    path.write_text('&^entry(n:#u8){"A\\0B"[n]'+(';7' if discarded else '')+'}')
                    r=run([EXE,path,'--source','entry','--budget','2','--emit-c',generated,
                           '--schedule',schedule,'--branch-order',branch])
                    self.assertEqual(r.returncode,0,r.stdout+r.stderr)
                    harness=directory/'check.c'
                    harness.write_text('#include "vexel.h"\n#include <setjmp.h>\nstatic jmp_buf trapped;\n'
                        'void vx_trap(uint8_t r){longjmp(trapped,r);}\n'
                        'uint32_t checked(uint8_t n){int r=setjmp(trapped);if(r)return 65536UL+r;'
                        'vx_entry(n);return vx_entry_read(0);}\n')
                    runtime=directory/'runtime.c'
                    runtime.write_text((ROOT/'backends/msx-neo8/runtime/vx_runtime.c').read_text().replace(
                        'void vx_trap(uint8_t reason)','void vx_unused_trap(uint8_t reason)',1))
                    library=directory/'test.so'
                    r=run(['cc','-O2','-std=c11','-shared','-fPIC','-I'+str(generated),
                           '-I'+str(ROOT/'backends/msx-neo8/runtime'),generated/'vx_bridges.c',runtime,harness,'-o',library])
                    self.assertEqual(r.returncode,0,r.stderr)
                    dll=ctypes.CDLL(str(library));dll.checked.argtypes=[ctypes.c_uint8];dll.checked.restype=ctypes.c_uint32
                    for n in range(256):
                        self.assertEqual(dll.checked(n),65552 if n>=3 else 7 if discarded else [65,0,66][n])


if __name__ == '__main__':
    unittest.main(verbosity=2)
