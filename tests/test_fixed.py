#!/usr/bin/env python3
"""Fixed-point rules checked against independent integer/rational arithmetic."""
import ctypes
from fractions import Fraction
import json
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1) if len(sys.argv) > 1 else 'build/vexel').resolve()
REPO = Path(__file__).resolve().parent.parent


def truncate(a, b):
    return (abs(a) // abs(b)) * (-1 if (a < 0) != (b < 0) else 1)


class FixedTests(unittest.TestCase):
    def check(self, source, expected=None, error=None):
        with tempfile.TemporaryDirectory(prefix='vexel-fixed-') as scratch:
            path = Path(scratch) / 'main.vx'
            path.write_text(source)
            for schedule in ('fifo', 'lifo', 'random'):
                run = subprocess.run([str(BINARY), str(path), '--json', '--schedule', schedule],
                                     text=True, capture_output=True, timeout=30)
                if error:
                    self.assertEqual(run.returncode, 1, run.stderr)
                    self.assertIn(error, run.stderr)
                else:
                    self.assertEqual(run.returncode, 0, run.stderr)
                    self.assertEqual({r['name']: r['value'] for r in json.loads(run.stdout)['roots']}, expected)

    def test_arithmetic_and_truncation(self):
        self.check('^a:#i8.8=((#i8.8)-3)/2; ^b:#i8.8=((#i8.8)1)/3; '
                   '^c:#i8.8={x:#i8.8=3;x/=2;x*=x;x}; '
                   '^d:#i8.8=((#i8.8)-5)%2;',
                   {'a': '-1.5', 'b': '0.33203125', 'c': '2.25', 'd': '-1'})

    def test_cast_rescales_towards_zero(self):
        self.check('x:#i8.8=((#i8.8)-3)/2; ^a:#i16=(#i16)x; '
                   '^b:#i4.1=(#i4.1)x; ^c:#i3.0=(#i3.0)x; ^d:#b=(#b)x;',
                   {'a': '-1', 'b': '-1.5', 'c': '-1', 'd': '1'})
        self.check('^a:#u0.8=(#u0.8)1; ^b:#i1.7=(#i1.7)1;', {'a': '0', 'b': '-1'})

    def test_raw_bits_representation_and_odd_widths(self):
        self.check('source:#i3.4=(#i3.4)-1; ^raw:#u8[1]=(#u8[1])source; '
                   '^x:#i3.4=(#i3.4)((#u8[1])[127]);', {'raw': '[112]', 'x': '-0.0625'})
        self.check('^x:#i80.40=((#i80.40)12345678901234567890)/2;',
                   {'x': '6172839450617283945'})
        self.check('^x:#i4.4={a:#i4.4=1;a<<=1;a|=(#i4.4)((#u8[1])[1]);a};', {'x': '2.0625'})

    def test_finite_relations_and_type_queries(self):
        self.check('^x:#i4.4=?{candidate=?(#i4.4,[0,1,2,3]);?(candidate*candidate==4);candidate};', {'x': '2'})
        self.check('^x:#i8.8={a:#[((#i8.8)1)/0]=3;a};', {'x': '3'})
        self.check('^x:#i8.8[2]=[(#i8.8)1,2];', {'x': '[1, 2]'})

    def test_invalid_family_shape_and_width(self):
        self.check('^x:#i8.8=((#i8.8)1)+((#i4.4)1);', error='incompatible types')
        self.check('^x:#i8.8=((#i8.8)1)<<((#i8.8)1);', error='shift count must be an integer')
        self.check('^x:#i8.8=128;', error='not representable')
        self.check('^x:#i0.8=0;', error='sign bit')
        self.check('^x:#u0.0=0;', error='width')
        self.check('^x:#u8=|((#i8.8)0)..((#i8.8)2)|;', error='range bounds must be integers')
        self.check('^x:#b=((#i8.8)1)&&1;', error='explicit cast')

    def test_runtime_helpers_against_independent_oracle(self):
        with tempfile.TemporaryDirectory(prefix='vexel-fixed-runtime-') as scratch:
            library = Path(scratch) / 'runtime.so'
            run = subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', '-DVX_FIXED_RUNTIME=1',
                                  str(REPO / 'backends/msx-neo8/runtime/vx_runtime.c'), '-o', str(library)],
                                 capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            runtime = ctypes.CDLL(str(library))
            for name in ('vx_fixed_mul', 'vx_fixed_div'):
                function = getattr(runtime, name)
                function.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint8]
                function.restype = ctypes.c_uint32
            runtime.vx_rescale.argtypes = [ctypes.c_uint32, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint8]
            runtime.vx_rescale.restype = ctypes.c_uint32
            rng = random.Random(34091)
            for width in range(1, 33):
                for signed in (0, 1):
                    for fraction in sorted({0, width // 2, width - signed}):
                        scale = 1 << fraction
                        mask = (1 << width) - 1
                        decode = lambda x: x - (1 << width) if signed and x & (1 << (width - 1)) else x
                        for _ in range(25):
                            a, b = rng.randrange(mask + 1), rng.randrange(1, mask + 1)
                            x, y = decode(a), decode(b)
                            self.assertEqual(runtime.vx_fixed_mul(a, b, width, signed, fraction) & mask,
                                             truncate(x*y, scale) & mask, (width, signed, fraction, x, y))
                            self.assertEqual(runtime.vx_fixed_div(a, b, width, signed, fraction) & mask,
                                             truncate(x*scale, y) & mask, (width, signed, fraction, x, y))
                            target_fraction = rng.randrange(33)
                            exact = Fraction(x, scale) * (1 << target_fraction)
                            self.assertEqual(runtime.vx_rescale(a, width, signed, fraction, target_fraction),
                                             truncate(exact.numerator, exact.denominator) & 0xffffffff)


if __name__ == '__main__':
    unittest.main(verbosity=2)
