#!/usr/bin/env python3
"""Bundled pure math, finite alternatives, shapes and independent native oracles."""
import json
import math
from pathlib import Path
import struct
import tempfile
import unittest
import test_tuples as support

def bits(x,width=32):
    try:return int.from_bytes(struct.pack('<'+{16:'e',32:'f',64:'d'}[width],x),'little')
    except OverflowError:return bits(math.copysign(math.inf,x),width)
def wrap(x,width=8):return (x+(1<<(width-1)))%(1<<width)-(1<<(width-1))
def truncdiv(a,b):return abs(a)//abs(b)*(-1 if (a<0)!=(b<0) else 1)
def fixedpow(base,exponent):
    result=16
    while exponent:
        if exponent&1:result=wrap(truncdiv(result*base,16))
        exponent>>=1
        if exponent:base=wrap(truncdiv(base*base,16))
    return result

class MathTests(unittest.TestCase):
    execute=support.TupleTests.execute
    def check(self,source,expected=None,**kwargs):
        support.TupleTests.check(self,'::std::math;'+source,None if expected is None else str(expected),**kwargs)

    def test_constant_families_and_finite_alternatives(self):
        cases=[('a=abs(-7);',7),('a=abs((#i8)-128);',-128),('a=min((#u8)7,(#u16)3);',3),
               ('a=max(-7,-2);',-2),('a=isqrt(100000000000000000000000000);',10000000000000),
               ('a=sqrt(2.0);',bits(math.sqrt(2),64)),('a=signbit(sqrt(-0.0));',1),
               ('a=isnan(sqrt(-1.0));',1),('a=isinf(1.0/0.0);',1),('a=isfinite(1.0/0.0);',0),
               ('a=min(0.0/0.0,3.0);',bits(3,64)),('a=signbit(min(0.0,-0.0));',1),
               ('a=signbit(max(0.0,-0.0));',0),('a=copysign(2.0,-0.0);',bits(-2,64)),
               ('a=floor(-1.25);',bits(-2,64)),('a=ceil(-1.25);',bits(-1,64)),
               ('a=trunc(-1.25);',bits(-1,64)),('a=round(-2.5);',bits(-3,64)),
               ('a=roundeven(-2.5);',bits(-2,64)),('a=roundeven((#i4.4)2.5);',32),
               ('a=round((#i4.4)2.5);',48),('a=floor((#i4.4)-1.5);',-32),
               ('a=ceil((#i4.4)-1.5);',-16),('a=fmod((#i8)-7,3);',-1),
               ('a=pow(2,40);',1<<40),('a=pow((#u8)3,6);',217),
               ('a=pow((#i4.4)1.5,3);',54),('a=pow((#u0.8)0.5,0);',0),
               ('a=pow((#f16)1.5,3);',bits(3.375,16)),('a=pow(0.0/0.0,0);',bits(1,64)),
               ('a=?{x=?(#u8,0..10);?(isqrt(x)==2);?(x==5);x};',5),
               ('a=?{x=?(#i8,[-1,9]);isqrt(x)};',3)]
        for source,expected in cases:self.check(source,expected)
        for source in ('a=sqrt(2);','a=isqrt(-1);','a=pow(2,-1);','a=pow(2,(#i8)3);','a=abs((#b)1);'):
            self.check(source,status='contradictory')
        self.check('a=pow(2,100000);',status='exhausted')

    def test_shapes_empty_arguments_and_type_only_queries(self):
        self.check('x:#f32;a=sqrt(x);','#f32',query='type:a')
        self.check('&!input()->#f16;a=round(input());','#f16',query='type:a')
        self.check('a=min([[1,4],[3,2]],[2,3]);','Values(Values(1,3),Values(2,2))')
        self.check('a=sqrt((#f32[0])[]);','#f32[0]',query='type:a')
        self.check('a=min((#u8[0])[],(#u8)3);','Values')
        self.check('a={count:#u8=0;&first(){count+=1;(#u8[0])[]}&second(){count+=10;(#u8)2}'
                   'min(first(),second());count};',11)
        self.check('a=min([1,2],[3,4,5]);',status='contradictory')
        self.check('a=?{x=round(2.5);?(x==3.0);(#u8)x};',3)

    def test_native_integer_math_and_discarded_traps(self):
        self.execute('::std::math;&^entry(n:#u8){x=(#i8)n;(min(x,(#i8)-12),max(x,(#i8)-12),abs(x),isqrt(n),pow(n,3),fmod(n,7))}',
                     lambda n:[min(wrap(n),-12),max(wrap(n),-12),wrap(abs(wrap(n))),math.isqrt(n),pow(n,3,256),n%7],count=6)
        self.execute('::std::math;&^entry(n:#u8){isqrt((#i8)n);n}',lambda n:(10,) if n>=128 else [n])
        self.execute('::std::math;&^entry(n:#u8){fmod(n,n-1);n}',lambda n:(1,) if n==1 else [n])

    def test_native_float_and_fixed_math(self):
        self.execute('::std::math;&^entry(n:#u8){x=(#f32)n/2-(#f32)64.5;'
                     '(floor(x),ceil(x),trunc(x),round(x),roundeven(x),sqrt((#f32)n),copysign(x,(#f32)-0.0),pow((#f32)n/16,3))}',
                     lambda n:[bits(math.floor(n/2-64.5)),bits(math.copysign(abs(math.ceil(n/2-64.5)),n/2-64.5)),
                               bits(math.copysign(abs(math.trunc(n/2-64.5)),n/2-64.5)),
                               bits(math.copysign(math.floor(abs(n/2-64.5)+.5),n/2-64.5)),
                               bits(math.copysign(abs(round(n/2-64.5)),n/2-64.5)),bits(math.sqrt(n)),bits(-abs(n/2-64.5)),bits((n/16)**3)],count=8)
        self.execute('::std::math;&^entry(n:#u8){x=(#i4.4)n/2;(floor(x),ceil(x),round(x),roundeven(x),trunc(x),pow(x,n%6))}',
                     lambda n:[wrap(v*16) for v in (math.floor(wrap(n*16)/32),math.ceil(wrap(n*16)/32),
                         int(math.copysign(math.floor(abs(wrap(n*16)/32)+.5),wrap(n*16))),round(wrap(n*16)/32),math.trunc(wrap(n*16)/32))]
                         +[fixedpow(truncdiv(wrap(n*16),2),n%6)],count=6)

    def test_project_override_has_ordinary_semantics(self):
        with tempfile.TemporaryDirectory(prefix='vexel-math-override-') as tmp:
            root=Path(tmp);(root/'std').mkdir();(root/'std/math.vx').write_text('&sqrt(n){n+7}')
            path=root/'source.vx';path.write_text('::std::math;a=sqrt(4);')
            for schedule,branch in support.ORDERS:
                r=support.run([support.EXE,path,'--query','value:a','--schedule',schedule,'--branch-order',branch])
                self.assertEqual(r.returncode,0,r.stderr);self.assertEqual(json.loads(r.stdout)['queries'][0]['answer'],'11')

    def test_native_ieee_special_values_and_payloads(self):
        patterns=[0x7f800000,0xff800000,0x7f800001,0xffc12345,0,0x80000000,0x3f800000,0xbf800000]
        minima=[0,0xff800000,0,0,0,0x80000000,0,0xbf800000]
        maxima=[0x7f800000,0,0,0,0,0,0x3f800000,0]
        source='::std::math;::std::bits;&^entry(n:#u8){raw:#u32[8]=['+','.join(map(str,patterns))+'];'
        source+='x=u32_as_f32(raw[n%8]);(copysign(x,(#f32)-0.0),isnan(x),isinf(x),isfinite(x),signbit(x),min(x,(#f32)0),max(x,(#f32)0),pow(x,0))}'
        self.execute(source,lambda n:[patterns[n%8]|0x80000000,int(n%8 in (2,3)),int(n%8 in (0,1)),int(n%8>=4),
                                      patterns[n%8]>>31,minima[n%8],maxima[n%8],bits(1)],count=8)

if __name__=='__main__':unittest.main(verbosity=2)
