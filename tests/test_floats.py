#!/usr/bin/env python3
"""IEEE formats, exact rounding, exceptional values and solver integration."""
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()


class FloatTests(unittest.TestCase):
    def check(self,source,expected=None,error=None):
        with tempfile.TemporaryDirectory(prefix='vexel-floats-') as scratch:
            path=Path(scratch)/'main.vx';path.write_text(source)
            for schedule in ('fifo','lifo','random'):
                run=subprocess.run([str(BINARY),str(path),'--json','--schedule',schedule],capture_output=True,text=True,timeout=30)
                if error:
                    self.assertEqual(run.returncode,1,run.stderr);self.assertIn(error,run.stderr)
                else:
                    self.assertEqual(run.returncode,0,run.stderr)
                    self.assertEqual({r['name']:r['value'] for r in json.loads(run.stdout)['roots']},expected)

    def test_literals_and_promotions(self):
        self.check('^a:#f64=1.25e2; ^b:#f32=(#f32)-1.5; ^c:#f64=((#f16)2)+0.5;',
                   {'a':'125','b':'-1.5','c':'2.5'})
        self.check('^x:#f32=1.5;',error='implicit floating conversion')
        self.check('^x:#f16=2049;',error='not exactly representable')
        self.check('^x:#f64=1.0e999;',error='exceeds #f64 range')

    def test_half_ties_round_to_even(self):
        self.check('^a:#u16=(#u16)((#u8[2])((#f16)1.00048828125)); '
                   '^b:#u16=(#u16)((#u8[2])((#f16)1.00146484375)); '
                   '^c:#u32=(#u32)((#u8[4])((#f32)0.1));',
                   {'a':'15360','b':'15362','c':'1036831949'})

    def test_subnormals_and_signed_zero(self):
        self.check('tiny=(#f16)((#u8[2])[1,0]); triple=(#f16)((#u8[2])[3,0]); '
                   '^a:#u16=(#u16)((#u8[2])(tiny/2)); '
                   '^b:#u16=(#u16)((#u8[2])(triple/2)); '
                   '^c:#u64=(#u64)((#u8[8])-0.0); ^d:#b=-0.0==0.0;',
                   {'a':'0','b':'2','c':'9223372036854775808','d':'1'})

    def test_infinities_nan_and_remainder(self):
        self.check('nan=0.0/0.0; ^a:#f64=1.0/0.0; ^b:#f64=-1.0/0.0; '
                   '^c:#b=nan==nan; ^d:#b=nan!=nan; ^e:#b=nan<1.0; '
                   '^f:#f64=-5.5%2.0; ^g:#b=(#b)nan;',
                   {'a':'inf','b':'-inf','c':'0','d':'1','e':'0','f':'-1.5','g':'1'})
        self.check('^x:#u8=(#u8)(1.0/0.0);',error='non-finite float')
        self.check('^x:#f64=1.0&2.0;',error='unsupported floating-point operator')

    def test_numeric_casts_and_independent_type_query(self):
        self.check('^a:#i16=(#i16)-1.9; ^b:#u8=(#u8)-1.9; '
                   '^c:#i4.4=(#i4.4)-1.1; ^d:#f16=(#f16)(((#i8.8)3)/2); '
                   '^e:#u8={v:#[((#u8)(0.0/0.0))]=7;v};',
                   {'a':'-1','b':'255','c':'-1.0625','d':'1.5','e':'7'})

    def test_aggregate_comparisons_broadcast_and_sort(self):
        self.check('^a:#f64[2]=[1.0,2.0] .+ ((#f16)0.5); '
                   '^b:#b=[-1.0,2.0]<[0.0,-1.0]; ^c:#f64={s:#f64=0.0;[2.0,-1.0,1.0]@@{s=s*10.0+_;};s};',
                   {'a':'[1.5, 2.5]','b':'1','c':'-88'})
        self.check('^x:#u8={[(0.0/0.0),1.0]@@{};1};',error='unordered NaN')

    def test_finite_float_relations(self):
        self.check('^x:#f16=?{v=?(#f16,[0,1,2,3]);?(v*v==4);v};',{'x':'2'})
        self.check('^x:#u8=?{v=?(#f16,[(#f16)1.0,(#f16)(0.0/0.0)]);n=(#u8)v;?(n==1);n};',{'x':'1'})

    def test_arithmetic_matches_independent_ieee_oracle(self):
        rng=random.Random(12019)
        source=[];expected={}
        for width,fmt,exponent,fraction in ((16,'e',5,10),(32,'f',8,23),(64,'d',11,52)):
            size=width//8
            decode=lambda n:struct.unpack('<'+fmt,n.to_bytes(size,'little'))[0]
            for _ in range(12):
                a,b=rng.randrange(1<<width),rng.randrange(1<<width)
                x,y=decode(a),decode(b)
                for op in ('+','-','*','/','%'):
                    try:
                        z=x+y if op=='+' else x-y if op=='-' else x*y if op=='*' else x/y if op=='/' else math.fmod(x,y)
                    except ZeroDivisionError:
                        z=math.nan if x==0 or math.isnan(x) else math.copysign(math.inf,math.copysign(1,x)*math.copysign(1,y))
                    except ValueError:
                        z=math.nan
                    if math.isnan(z): bits=((1<<exponent)-1)<<fraction | 1<<(fraction-1)
                    else:
                        try: bits=int.from_bytes(struct.pack('<'+fmt,z),'little')
                        except OverflowError: bits=int.from_bytes(struct.pack('<'+fmt,math.copysign(math.inf,z)),'little')
                    lhs=f'(#f{width})((#u8[{size}])((#u{width}){a}))'
                    rhs=f'(#f{width})((#u8[{size}])((#u{width}){b}))'
                    name='result'+str(len(expected));expected[name]=str(bits)
                    source.append(f'^{name}:#u{width}=(#u{width})((#u8[{size}])(({lhs}){op}({rhs})));')
        self.check('\n'.join(source),expected)


if __name__=='__main__':unittest.main(verbosity=2)
