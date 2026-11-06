#!/usr/bin/env python3
"""Scalar operator, short-circuit state and discarded-trap observations."""
import ctypes
import itertools
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXE = Path(sys.argv.pop(1)).resolve()
REPO = Path(__file__).resolve().parents[3]
ORDERS = list(itertools.product(('fifo', 'lifo', 'random'), ('forward', 'reverse')))


def run(args):
    return subprocess.run([str(arg) for arg in args], text=True, capture_output=True, timeout=120)


class OperatorTests(unittest.TestCase):
    def check(self, source, expected=None, status='resolved', query='value:a'):
        with tempfile.TemporaryDirectory(prefix='vexel-operators-') as temporary:
            path = Path(temporary)/'source.vx'
            path.write_text(source)
            for schedule, branch in ORDERS:
                result = run([EXE, path, '--query', query, '--schedule', schedule, '--branch-order', branch])
                self.assertIn(result.returncode, (0, 1), result.stderr)
                report = json.loads(result.stdout)
                self.assertEqual(report['status'], status, result.stdout+result.stderr)
                if expected is not None:
                    self.assertEqual(report['queries'][0]['answer'], expected)

    def test_types_widths_unary_and_short_circuit(self):
        cases = [
            ('a=(#u8)128 << (#u16)1;', '0'),
            ('a=(#u8)128 >> (#u32)7;', '1'),
            ('a=~(#u8)3;', '252'), ('a=~7;', '-8'), ('a=!(#b)1;', '0'),
            ('a=|(#u8)7|;', '7'), ('a=|-7|;', '7'), ('&!get()->#u8[3];a=|get()|;', '3'),
            ('&f(){|{->7;0}|;missing()}a=f();', '7'),
            ('a=0 && missing();', '0'), ('a=1 || (1/0);', '1'),
            ('a={x:#u8=0;0&&{x+=1;1};1||{x+=2;0};x};', '0'),
            ('&f(){1&&{->7;};missing()}a=f();', '7'),
            ('a={x:#b=0;x&&=missing();x};', '0'),
            ('a={x:#b=1;x||=missing();x};', '1'),
            ('a=?{x=?(#u8,0..4);?(x>0 && x<2);x};', '1'),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.check(source, expected)
        self.check('a=(#u8)128 << (#u16)1;', '#u8', query='type:a')
        self.check('a=1 << (#u16)8;', '#integer', query='type:a')
        self.check('a:#[0&&missing()]=1;', '#b', query='type:a')
        for source in ('a=(#u8)1 << (#u16)8;', 'a=(#u8)1 << -1;',
                       'a=(#u8)1 && 1;', 'a=!2;', 'a=~[1,2];', 'a=1&&{};'):
            with self.subTest(source=source):
                self.check(source, status='contradictory')

    def test_runtime_operators_and_completion(self):
        cases = [
            ('&^entry(n:#u8){~n}', lambda n:255-n),
            ('&^entry(n:#u8){|n|}', lambda n:n),
            ('x:#u8;&^entry(n:#u8){x=n;|x|}', lambda n:n),
            ('&^entry(n:#u8){!(n<128)}', lambda n:int(n>=128)),
            ('&^entry(n:#u8){x:#u8=0;n<128&&{x+=1;1};n<128||{x+=2;0};x}', lambda n:1 if n<128 else 2),
            ('&^entry(n:#u8){x:#u8=0;b:#b=n<128;b&&={x+=1;1};x}', lambda n:int(n<128)),
            ('#P(flag:#b,tail:#u8);&^entry(n:#u8){p=#P(n<128,4);p.flag||={p.tail+=1;1};p.tail}', lambda n:4 if n<128 else 5),
            ('flag:#b;&^entry(n:#u8){flag=n<128;flag&&={flag=0;1};(#u8)flag}', lambda n:int(n<128)),
            ('&^entry(n:#u8){x:#u8=n;x|=3;x&=127;x^=5;x<<=(#u16)1;x>>=(#u16)1;x/=3;x%=5;x}', lambda n:((((((n|3)&127)^5)<<1)&255)>>1)//3%5),
            ('&^entry(n:#u8){x:#u8=1;x/=n;7}', lambda n:65537 if n==0 else 7),
            ('&^entry(n:#u8){x:#u8[1]=[7];x[0]%=n;7}', lambda n:65537 if n==0 else 7),
            ('&^entry(n:#u8){x:#u8=1;x<<=(#u16)n;7}', lambda n:7 if n<8 else 65538),
            ('&^entry(n:#u8){x:#b[2]=[0,0];x[n]&&=1;7}', lambda n:7 if n<2 else 65552),
            ('&^entry(n:#u8){n<128&&{->(#u16)700;};(#u16)n}', lambda n:700 if n<128 else n),
            ('&^entry(n:#u8){((#u32)n+0x12345600)^0xff00ff00}', lambda n:(n+0x12345600)^0xff00ff00),
        ]
        for source, oracle in cases:
            for schedule, branch in ORDERS:
                with self.subTest(source=source, schedule=schedule, branch=branch), tempfile.TemporaryDirectory(prefix='vexel-operators-c-') as temporary:
                    directory = Path(temporary)
                    path, generated = directory/'source.vx', directory/'generated'
                    path.write_text(source)
                    result = run([EXE, path, '--source', 'entry', '--budget', '4', '--schedule', schedule,
                                  '--branch-order', branch, '--emit-c', generated])
                    self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                    harness = directory/'harness.c'
                    harness.write_text('''#include "vexel.h"
#include <setjmp.h>
static jmp_buf trapped;
void vx_trap(uint8_t reason) {longjmp(trapped,reason);}
uint32_t checked(uint8_t n) {
    int reason=setjmp(trapped);
    if(reason) return 65536UL+(uint32_t)reason;
    vx_entry(n);return vx_entry_read(0);
}
''')
                    runtime = directory/'runtime.c'
                    runtime.write_text((REPO/'backends/msx-neo8/runtime/vx_runtime.c').read_text().replace(
                        'void vx_trap(uint8_t reason)', 'void vx_unused_default_trap(uint8_t reason)', 1))
                    library = directory/'execution.so'
                    built = run(['cc', '-std=c11', '-O2', '-shared', '-fPIC', '-I'+str(generated),
                                 '-I'+str(REPO/'backends/msx-neo8/runtime'), generated/'vx_bridges.c', runtime, harness, '-o', library])
                    self.assertEqual(built.returncode, 0, built.stderr)
                    dll = ctypes.CDLL(str(library))
                    dll.checked.argtypes, dll.checked.restype = [ctypes.c_uint8], ctypes.c_uint32
                    for n in range(256):
                        self.assertEqual(dll.checked(n), oracle(n), n)


if __name__ == '__main__':
    unittest.main(verbosity=2)
