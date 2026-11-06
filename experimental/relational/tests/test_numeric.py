#!/usr/bin/env python3
"""Fixed/IEEE source relations checked against rational and IEEE host oracles."""
import ctypes
import itertools
import json
import math
import random
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

EXE=Path(sys.argv.pop(1)).resolve()
ROOT=Path(__file__).resolve().parents[3]
ORDERS=list(itertools.product(('fifo','lifo','random'),('forward','reverse')))

def run(args):
    return subprocess.run([str(x) for x in args],capture_output=True,text=True,timeout=120)

def bits(value,width=32):
    fmt={16:'e',32:'f',64:'d'}[width]
    try:
        return int.from_bytes(struct.pack('<'+fmt,value),'little')
    except OverflowError:
        return int.from_bytes(struct.pack('<'+fmt,math.copysign(math.inf,value)),'little')

def decode(raw,width=32):
    return struct.unpack('<'+{16:'e',32:'f',64:'d'}[width],raw.to_bytes(width//8,'little'))[0]

def trunc(a,b):
    return abs(a)//abs(b)*(-1 if (a<0)!=(b<0) else 1)

def wrap(n,width):
    return (n+(1<<(width-1)))%(1<<width)-(1<<(width-1))

class NumericTests(unittest.TestCase):
    def check(self,source,expected=None,status='resolved',query='value:a'):
        with tempfile.TemporaryDirectory(prefix='vexel-numeric-') as temp:
            path=Path(temp)/'source.vx';path.write_text(source)
            for schedule,branch in ORDERS:
                with self.subTest(source=source,schedule=schedule,branch=branch):
                    r=run([EXE,path,'--query',query,'--schedule',schedule,'--branch-order',branch])
                    self.assertIn(r.returncode,(0,1),r.stderr)
                    report=json.loads(r.stdout);self.assertEqual(report['status'],status,r.stdout+r.stderr)
                    if expected is not None:self.assertEqual(report['queries'][0]['answer'],str(expected))

    def test_constant_numeric_rules(self):
        for source,expected in [
            ('a=(#i8.8)-3/2;',-384),('a=(#i8.8)1/3;',85),('a=(#i8.8)-5%2;',-256),
            ('a=(#i16)((#i8.8)-3/2);',-1),('a=(#i4.1)((#i8.8)-3/2);',-3),
            ('a=(#u0.8)1;',0),('a=(#i1.7)1;',-128),('a=~(#i4.4)1;',-17),
            ('a=(#i80.40)12345678901234567890/2;',6172839450617283945*(1<<40)),
            ('a=1.25e2;',bits(125,64)),('a=-0.0;',1<<63),('a=-0.0==0.0;',1),
            ('a=(#f16)1.00048828125;',0x3c00),('a=(#f16)1.00146484375;',0x3c02),
            ('a=(#f16)0.000000059604644775390625/2;',0),
            ('a=(#f16)0.000000178813934326171875/2;',2),
            ('a=1.0/0.0;',bits(math.inf,64)),('a=-1.0/0.0;',bits(-math.inf,64)),
            ('a=0.0/0.0;',0x7ff8000000000000),('a=(0.0/0.0)==(0.0/0.0);',0),
            ('a=(0.0/0.0)!=(0.0/0.0);',1),('a=(0.0/0.0)<1.0;',0),
            ('a=-5.5%2.0;',bits(-1.5,64)),('a=(#b)(0.0/0.0);',1),
            ('a=(#i16)-1.9;',-1),('a=(#u8)-1.9;',255),('a=(#i4.4)-1.1;',-17),
            ('a=(#f16)((#i8.8)3/2);',bits(1.5,16)),
            ('a={v:#[((#u8)(0.0/0.0))]=7;v};',7),
            ('a={s:#f64=0.0;[2.0,-1.0,1.0]@@{s=s*10.0+_;};s};',bits(-88,64)),
            ('&f(x:#f16){1}&f(x:#u8){2}a=f((#f16)2);',1),
            ('a=?{v=?(#f16,[0,1,2,3]);?(v*v==4);v};',bits(2,16)),
            ('a=?{v=?(#f16,[(#f16)1,(#f16)(0.0/0.0)]);n=(#u8)v;?(n==1);n};',1),
            ('a=?{v=?(#i4.4,[0,1,2,3]);?(v*v==4);v};',32),
            ('&copy(n){n}x:#[copy(y)+1]=(#f16)255;y:#[copy(x)+1]=(#f32)2;a=x+1;',bits(256)),
        ]:self.check(source,expected)
        self.check('a=(#f16)2+0.5;','#f64',query='type:a')
        self.check('a=(#i3.4)1;','#i3.4',query='type:a')
        for source in ('a:#f32=1.5;','a:#f16=2049;','a=(#u8)(1.0/0.0);','a=1.0&2.0;',
                       'a=~1.0;','a=(#i8.8)1+(#i4.4)1;','a=(#i8.8)1 << (#i8.8)1;',
                       'a:#i8.8=128;','a=|((#i8.8)0)..((#i8.8)2)|;',
                       'a={[(0.0/0.0),1.0]@@{};1};'):
            self.check(source,status='contradictory')

    def test_width_spelling_and_demand_budget(self):
        self.check('a=(#i08.08)-3/2;',-384)
        self.check('a=(#i08.08)1+(#i8.8)2;',768)
        self.check('a=(#u000.08)1;','#u0.8',query='type:a')
        self.check('unused:#i999999999999999999999.8;a=7;',7)
        self.check('a:#i999999999999999999999.8;',status='exhausted',query='type:a')
        for spelling in ('u0.0','i0.8','u00.00'):
            self.check('a:#'+spelling+';',status='contradictory',query='type:a')

    def execute(self,source,oracle,count=1,parameter='int8_t',samples=range(-128,128)):
        for schedule,branch in ORDERS:
            with self.subTest(source=source,schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-numeric-c-') as temp:
                directory=Path(temp);path=directory/'source.vx';generated=directory/'generated';path.write_text(source)
                r=run([EXE,path,'--source','entry','--budget',count*4,'--emit-c',generated,'--schedule',schedule,'--branch-order',branch])
                self.assertEqual(r.returncode,0,r.stdout+r.stderr)
                harness=directory/'harness.c';harness.write_text('#include "vexel.h"\n#include <setjmp.h>\nstatic jmp_buf trapped;\n'
                    'void vx_trap(uint8_t r){longjmp(trapped,r);}\n'
                    'unsigned checked('+parameter+' n){int r=setjmp(trapped);if(r)return r;vx_entry(n);return 0;}\n')
                runtime=directory/'runtime.c';runtime.write_text((ROOT/'backends/msx-neo8/runtime/vx_runtime.c').read_text().replace(
                    'void vx_trap(uint8_t reason)','void vx_unused_trap(uint8_t reason)',1))
                lib=directory/'run.so';rt=ROOT/'backends/msx-neo8/runtime'
                r=run(['cc','-O2','-std=c11','-shared','-fPIC','-DVX_FIXED_RUNTIME=1','-DVX_FLOAT_RUNTIME=1',
                       '-I'+str(generated),'-I'+str(rt),generated/'vx_bridges.c',runtime,harness,'-o',lib])
                self.assertEqual(r.returncode,0,r.stderr)
                dll=ctypes.CDLL(str(lib));dll.checked.argtypes=[{'int8_t':ctypes.c_int8,'uint16_t':ctypes.c_uint16,'uint32_t':ctypes.c_uint32}[parameter]]
                dll.checked.restype=ctypes.c_uint;dll.vx_entry_read.argtypes=[ctypes.c_uint16];dll.vx_entry_read.restype=ctypes.c_uint32
                for n in samples:
                    expected=oracle(n);reason=dll.checked(n)
                    if isinstance(expected,tuple):self.assertEqual(reason,expected[0],n)
                    else:
                        self.assertEqual(reason,0,n)
                        self.assertEqual([dll.vx_entry_read(i) for i in range(count)],[v&0xffffffff for v in expected],n)

    def test_runtime_fixed_scaling_and_frames(self):
        self.execute('&^entry(n:#i4.4){[n*3,n/3,n%3,n>>1,~n,|n|]}',
                     lambda n:[wrap(n*3,8),trunc(n,3),n-trunc(n,48)*48,n>>1,wrap(~n,8),wrap(abs(n),8)],6)
        self.execute('&^entry(n:#i8){x=(#i8.8)n;[(#i16)(x/3),(#i16)(x*3),(#i16)(x%3)]}',
                     lambda n:[trunc(trunc(n*256,3),256),trunc(wrap(n*256*3,16),256),trunc(n*256-trunc(n*256,768)*768,256)],3)
        self.execute('&~f(x:#i4.4)->#i4.4{x/3} saved:#i4.4;&^entry(n:#i4.4){saved=f(n);saved}',lambda n:[trunc(n,3)])
        self.execute('#P(integer:#i8,fraction:#i3.2);&^entry(n:#i4.4){#P((#i8)n,(#i3.2)n)}',lambda n:[trunc(n,16),wrap(trunc(n,4),5)],2)

    def test_runtime_float_formats_and_traps(self):
        self.execute('&^entry(n:#i8){x=(#f32)n;[x/3,x*3,x+3,-x,|x|]}',lambda n:[bits(n/3),bits(n*3),bits(n+3),bits(-float(n)),bits(abs(float(n)))],5)
        self.execute('&~f(x:#f16)->#f16{x/3} saved:#f16;&^entry(n:#i8){saved=f((#f16)n);saved}',lambda n:[bits(n/3,16)])
        samples=[0,0x8000,1,3,0x3c00,0xbc00,0x7bff,0xfbff,0x7c00,0xfc00,0x7e00,0x7fff]
        self.execute('&^entry(n:#f16){[n<0,n>=0,n==n,n!=n]}',
                     lambda n:list(map(int,[decode(n,16)<0,decode(n,16)>=0,decode(n,16)==decode(n,16),decode(n,16)!=decode(n,16)])),4,'uint16_t',samples)
        self.execute('&^entry(n:#f16){[-n,|n|,n/2]}',
                     lambda n:[n^0x8000,n&0x7fff,0x7e00 if math.isnan(decode(n,16)) else bits(decode(n,16)/2,16)],3,'uint16_t',samples)
        self.execute('&^entry(n:#f16){(#i8)n;7}',lambda n:(6,) if not math.isfinite(decode(n,16)) else [7],1,'uint16_t',samples)
        self.execute('&^entry(n:#f16){(#i8)n}',lambda n:(6,) if not math.isfinite(decode(n,16)) else [wrap(math.trunc(decode(n,16)),8)],1,'uint16_t',samples)
        self.execute('&^entry(n:#f16){(#i4.4)n}',lambda n:(6,) if not math.isfinite(decode(n,16)) else [wrap(math.trunc(decode(n,16)*16),8)],1,'uint16_t',samples)

    def test_ieee_arithmetic_against_independent_oracle(self):
        rng=random.Random(70613)
        for width in (16,32):
            canonical=0x7e00 if width==16 else 0x7fc00000
            samples=[0,1,3,1<<(width-1),bits(math.inf,width),bits(-math.inf,width),canonical,canonical|31]
            samples += [rng.getrandbits(width) for _ in range(128)]
            def oracle(raw):
                x=decode(raw,width)
                try: remainder=math.fmod(x,3)
                except ValueError: remainder=math.nan
                return [canonical if math.isnan(v) else bits(v,width) for v in (x+1,x-1,x*3,x/3,remainder)]
            self.execute('&^entry(n:#f'+str(width)+'){[n+1,n-1,n*3,n/3,n%3]}',oracle,5,
                         'uint16_t' if width==16 else 'uint32_t',samples)

if __name__=='__main__':unittest.main(verbosity=2)
