#!/usr/bin/env python3
"""Software floating arithmetic through editable C and the actual Z80."""
import ctypes
import json
import math
import random
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_msx import EXE, run, project_environment

FLOAT_SOURCE = ''
for width in (16,32):
    for name,op in (('add','+'),('sub','-'),('mul','*'),('div','/'),('rem','%')):
        FLOAT_SOURCE += f'''&^fp_{name}{width}(a:#u{width},b:#u{width})->#u{width} {{
            x=(#f{width})((#u8[{width//8}])a);y=(#f{width})((#u8[{width//8}])b);
            (#u{width})((#u8[{width//8}])(x{op}y))
        }}\n'''
FLOAT_SOURCE += '''
&^fp_compare(a:#u32,b:#u32)->#u8 {
 x=(#f32)((#u8[4])a);y=(#f32)((#u8[4])b);
 (#u8)(x==y)+((#u8)(x!=y))*2+((#u8)(x<y))*4+((#u8)(x<=y))*8+((#u8)(x>y))*16+((#u8)(x>=y))*32
}
&^fp_array_compare(a:#u32,b:#u32)->#u8 {
 x=[(#f32)((#u8[4])a),2];y=[(#f32)((#u8[4])b),3];
 (#u8)(x==y)+((#u8)(x!=y))*2+((#u8)(x<y))*4+((#u8)(x<=y))*8+((#u8)(x>y))*16+((#u8)(x>=y))*32
}
&^fp_half(a:#u32)->#u16 {x=(#f32)((#u8[4])a);(#u16)((#u8[2])((#f16)x))}
&^fp_widen(a:#u16)->#u32 {x=(#f16)((#u8[2])a);(#u32)((#u8[4])((#f32)x))}
&^fp_integer(a:#u32)->#i16 {x=(#f32)((#u8[4])a);(#i16)x}
&^fp_fixed(a:#u32)->#u16 {x=(#f32)((#u8[4])a);(#u16)((#u8[2])((#i8.8)x))}
&^fp_from_fixed(a:#u16)->#u32 {x=(#i8.8)((#u8[2])a);(#u32)((#u8[4])((#f32)x))}
&^fp_from_signed(a:#i32)->#u32 {(#u32)((#u8[4])((#f32)a))}
&^fp_from_unsigned(a:#u32)->#u32 {(#u32)((#u8[4])((#f32)a))}
&^fp_truth(a:#u32)->#b {x=(#f32)((#u8[4])a);(#b)x}
&^fp_negate(a:#u32)->#u32 {x=(#f32)((#u8[4])a);(#u32)((#u8[4])-x)}
&^fp_absolute(a:#u32)->#u32 {x=(#f32)((#u8[4])a);(#u32)((#u8[4])|x|)}
&^fp_huge_constant(a:#u32)->#u32 {
 x=(#f32)((#u8[4])a);values=[x,1267650600228229401496703205376];(#u32)((#u8[4])(values[0]+values[1]))
}
#FloatPair(a:#f32,b:#f16);
&^fp_record(a:#u32)->#u32 {p=#FloatPair((#f32)((#u8[4])a),2);(#u32)((#u8[4])(p.a+p.b))}
&~fp_recursive(n:#u8,a:#f32)->#f32 {n==0?a:fp_recursive(n-1,a/2)}
&^fp_call(n:#u8)->#u32 {(#u32)((#u8[4])fp_recursive(n,4))}
&^fp_discard_cast(a:#u32)->#u8 {x=(#f32)((#u8[4])a);(#u8)x;7}
&^fp_discard_array_cast(a:#u32)->#u8 {x=[(#f32)((#u8[4])a),1];(#u16[2])x;7}
'''


def encode(value, width=32):
    fmt='e' if width==16 else 'f'
    if math.isnan(value): return 0x7e00 if width==16 else 0x7fc00000
    try: return int.from_bytes(struct.pack('<'+fmt,value),'little')
    except OverflowError: return int.from_bytes(struct.pack('<'+fmt,math.copysign(math.inf,value)),'little')


def decode(bits,width=32):
    return struct.unpack('<'+('e' if width==16 else 'f'),bits.to_bytes(width//8,'little'))[0]


class FloatBackendTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='vexel-float-backend-')
        cls.base=Path(cls.tmp.name);cls.source=cls.base/'main.vx';cls.source.write_text(FLOAT_SOURCE)
        cls.project=cls.base/'project';run([EXE,cls.source,'--emit-msx',cls.project])
        library=cls.project/'host.so'
        run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(cls.project/'generated'),'-I'+str(cls.project/'runtime'),
             *sorted((cls.project/'generated').glob('*.c')),cls.project/'runtime/vx_runtime.c','-o',library])
        cls.lib=ctypes.CDLL(str(library))

    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()

    def call(self,name,args,types=None,result=None):
        if result is None:result=ctypes.c_uint16 if name.endswith('16') or name in ('fp_half','fp_fixed') else ctypes.c_uint32
        argument=ctypes.c_uint16 if name.endswith('16') else ctypes.c_uint32
        fn=getattr(self.lib,'vx_'+name);fn.argtypes=types or [argument]*len(args);fn.restype=result
        return fn(*args)

    def oracle(self,name,args):
        command=[EXE,self.source,'--call',name,'--json']
        for arg in args:command.extend(['--arg',str(arg)])
        return int(json.loads(run(command).stdout)['roots'][0]['value'])

    def test_arithmetic_and_comparisons(self):
        rng=random.Random(30941)
        for width in (16,32):
            for _ in range(400):
                a,b=rng.randrange(1<<width),rng.randrange(1<<width);x,y=decode(a,width),decode(b,width)
                for op in ('add','sub','mul','div','rem'):
                    try:z=x+y if op=='add' else x-y if op=='sub' else x*y if op=='mul' else x/y if op=='div' else math.fmod(x,y)
                    except ZeroDivisionError:z=math.nan if x==0 or math.isnan(x) else math.copysign(math.inf,math.copysign(1,x)*math.copysign(1,y))
                    except ValueError:z=math.nan
                    expected=encode(z,width)
                    self.assertEqual(self.call(f'fp_{op}{width}',[a,b]),expected,(width,op,hex(a),hex(b)))
                if width==32:
                    expected=sum(int(c)<<i for i,c in enumerate((x==y,x!=y,x<y,x<=y,x>y,x>=y)))
                    self.assertEqual(self.call('fp_compare',[a,b]),expected)

    def test_casts_and_aggregate_paths(self):
        u16,u8=ctypes.c_uint16,ctypes.c_uint8
        rng=random.Random(91401)
        for _ in range(400):
            bits=rng.randrange(2**32);value=decode(bits)
            self.assertEqual(self.call('fp_half',[bits]),encode(value,16))
            half=rng.randrange(2**16)
            self.assertEqual(self.call('fp_widen',[half],[u16]),encode(decode(half,16)))
            self.assertEqual(self.call('fp_truth',[bits],result=u8),value!=0)
            self.assertEqual(self.call('fp_negate',[bits]),bits^0x80000000)
            self.assertEqual(self.call('fp_absolute',[bits]),bits&0x7fffffff)
            self.assertEqual(self.call('fp_from_unsigned',[bits]),encode(bits))
            signed=bits if bits<2**31 else bits-2**32
            self.assertEqual(self.call('fp_from_signed',[signed],[ctypes.c_int32]),encode(signed))
            fixed=half if half<2**15 else half-2**16
            self.assertEqual(self.call('fp_from_fixed',[half],[u16]),encode(fixed/256))
            if math.isfinite(value):
                integer=int(value)&0xffff;integer=integer if integer<2**15 else integer-2**16
                self.assertEqual(self.call('fp_integer',[bits],result=ctypes.c_int16),integer)
                self.assertEqual(self.call('fp_fixed',[bits]),int(value*256)&0xffff)
        for bits in (0,0x80000000,0x3fc00000,0xbfc00000,0x7fc00000):
            for name in ('fp_record','fp_huge_constant'):
                self.assertEqual(self.call(name,[bits]),self.oracle(name,[bits]))
            for other in (bits,0,0x7fc00000):
                self.assertEqual(self.call('fp_array_compare',[bits,other],result=u8),self.oracle('fp_array_compare',[bits,other]))
        for n in range(8):self.assertEqual(self.call('fp_call',[n],[u8]),self.oracle('fp_call',[n]))

    @unittest.skipUnless(shutil.which('sdcc') and shutil.which('sdasz80'),'SDCC toolchain unavailable')
    def test_z80_arithmetic_and_traps(self):
        project=self.project
        cases=[('fp_add32',[0x80000001,0x00800000]),('fp_sub32',[0x3f800000,0x3f7fffff]),
               ('fp_mul32',[0x7f7fffff,0x00800001]),('fp_div32',[1,0x3fc00000]),('fp_rem32',[0xc0b00000,0x40000000]),
               ('fp_div32',[0,0]),('fp_div32',[0x3f800000,0]),('fp_mul16',[1,0x7bff]),
               ('fp_div16',[3,0x4000]),('fp_add16',[0x3c00,0x1000]),('fp_half',[0x3f801000]),
               ('fp_widen',[1]),('fp_integer',[0xbff33333]),('fp_fixed',[0xbf8ccccd]),
               ('fp_from_fixed',[0xfe80]),('fp_from_signed',[-2147483648]),('fp_from_unsigned',[4294967295]),
               ('fp_compare',[0x7fc00000,0]),('fp_array_compare',[0x7fc00000,0]),
               ('fp_record',[0x3fc00000]),('fp_huge_constant',[0x3f800000]),('fp_call',[3]),
               ('fp_truth',[0x80000000]),('fp_negate',[0x7f800001]),('fp_absolute',[0x80000000])]
        main='#include "vexel.h"\nvolatile uint32_t vx_last_result;\nvoid main(void) VX_ABI VX_RESIDENT {\n'
        for index,(name,args) in enumerate(cases,1):
            expected=self.oracle(name,args)&0xffffffff
            arguments=','.join(str(a)+('UL' if a>=0 else 'L') for a in args)
            main+=f'if ((uint32_t)vx_{name}({arguments}) != {expected}UL) vx_last_result={index};\n'
        main+='for (;;) { __asm halt __endasm; }\n}\n';(project/'user/main.c').write_text(main)
        run(['make','-j2','simulate'],cwd=project)
        report=json.loads((project/'build/execution.json').read_text())
        self.assertEqual(report['observation']['last_result'],0)
        for name in ('fp_discard_cast','fp_discard_array_cast'):
            (project/'user/main.c').write_text('#include "vexel.h"\nvolatile uint32_t vx_last_result;\n'
                f'void main(void) VX_ABI VX_RESIDENT {{vx_last_result=vx_{name}(0x7fc00000UL);for (;;){{ __asm halt __endasm; }} }}\n')
            result=subprocess.run(['make','-j2','simulate'],cwd=project,text=True,capture_output=True,env=project_environment())
            self.assertNotEqual(result.returncode,0)
            self.assertIn('runtime trapped with reason 6',result.stderr)
            self.assertFalse((project/'build/execution.json').exists())
        (project/'user/main.c').write_text(main)
        run(['make','-j2','simulate','SIMFLAGS=--irq-period 997 --expanded-slot'],cwd=project)
        report=json.loads((project/'build/execution.json').read_text())
        self.assertEqual(report['observation']['last_result'],0)
        self.assertGreater(report['observation']['interrupts_accepted'],0)


if __name__=='__main__':unittest.main(verbosity=2)
