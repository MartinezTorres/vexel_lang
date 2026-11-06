#!/usr/bin/env python3
"""Bundled pure math: source relations, independent oracles, and MSX execution."""
import ctypes
import json
import math
from pathlib import Path
import random
import shutil
import struct
import subprocess
import tempfile
import unittest

from test_msx import EXE, project_environment, run

ROUNDING=('sqrt','floor','ceil','trunc','round','roundeven')
def encode(value,bits):
    if math.isnan(value): return {16:0x7e00,32:0x7fc00000,64:0x7ff8000000000000}[bits]
    try:return int.from_bytes(struct.pack({16:'<e',32:'<f',64:'<d'}[bits],value),'little')
    except OverflowError:return encode(math.copysign(math.inf,value),bits)
def decode(raw,bits):return struct.unpack({16:'<e',32:'<f',64:'<d'}[bits],raw.to_bytes(bits//8,'little'))[0]
def oracle(name,raw,bits):
    value=decode(raw,bits)
    if math.isnan(value):return encode(math.nan,bits)
    if name=='sqrt':return encode(math.nan if value<0 else math.sqrt(value),bits)
    if not math.isfinite(value) or value==0:return raw
    if name=='round':
        result=value if abs(value)>=2**{16:10,32:23,64:52}[bits] else math.copysign(math.floor(abs(value)+0.5),value)
    else:result={'floor':math.floor,'ceil':math.ceil,'trunc':math.trunc,'roundeven':round}[name](value)
    return encode(math.copysign(0.0,value) if result==0 else result,bits)

class MathSourceTests(unittest.TestCase):
    def check(self,source,expected=None,error=None,modules=None,flags=()):
        with tempfile.TemporaryDirectory(prefix='vexel-math-source-') as directory:
            root=Path(directory);(root/'main.vx').write_text(source)
            for name,text in (modules or {}).items():
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)
            for schedule in ('fifo','lifo','random'):
                result=subprocess.run([str(EXE),str(root/'main.vx'),'--json','--schedule',schedule,*flags],capture_output=True,text=True,timeout=45)
                if error:self.assertEqual(result.returncode,1,result.stderr);self.assertIn(error,result.stderr)
                else:
                    self.assertEqual(result.returncode,0,result.stderr)
                    self.assertEqual({r['name']:r['value'] for r in json.loads(result.stdout)['roots']},expected)

    def test_numeric_families_and_rounding_contract(self):
        self.check('''::std::math;
^a:#u16=abs(-200);^b=abs((#i8)-128);^c=min((#u8)42,3);^d=max((#u8)2,(#u16)700);
^e:#u128=isqrt(10000000000000000000000000000000000000001);
^f:#u64=pow(3,40);^g=pow((#u8)3,6);^h:#i8=fmod(-11,3);
^i=(#i16)floor((#i8.8)-1.25);^j=(#i16)ceil((#i8.8)-1.25);
^k=(#i16)round((#i8.8)-2.5);^l=(#i16)roundeven((#i8.8)-2.5);
^m=(#i16)round((#i8.8)127.75);^n=(#u8)round((#u0.8)0.75);
^o=(#i16)pow((#i8.8)1.5,3);^p=(#u16)sqrt((#f32)144);
''',{'a':'200','b':'-128','c':'3','d':'700','e':'100000000000000000000','f':str(3**40),
     'g':'217','h':'-2','i':'-2','j':'-1','k':'-3','l':'-2','m':'-128','n':'0','o':'3','p':'12'})

    def test_float_roots_and_rounding_match_independent_oracle(self):
        rng=random.Random(503);source='::std::math;';expected={}
        for bits in (16,32,64):
            samples=[0,1,1<<(bits-1),encode(-2.5,bits),encode(2.5,bits),encode(math.inf,bits),encode(-math.inf,bits),encode(math.nan,bits)]
            samples += [rng.getrandbits(bits) for _ in range(16)]
            for raw in samples:
                for name in ROUNDING:
                    key='r'+str(len(expected))
                    source+=f'^'+key+f':#u{bits}=(#u{bits})(#u8[{bits//8}]){name}((#f{bits})(#u8[{bits//8}])((#u{bits}){raw}));\n'
                    expected[key]=str(oracle(name,raw,bits))
        self.check(source,expected)

    def test_signed_zero_nan_and_classification(self):
        self.check('''::std::math;::std::bits;
^a=f32_as_u32(min(u32_as_f32(0),u32_as_f32(0x80000000)));
^b=f32_as_u32(max(u32_as_f32(0x80000000),u32_as_f32(0)));
^c=f32_as_u32(min(u32_as_f32(0x7f800001),(#f32)2));
^d=f32_as_u32(copysign(u32_as_f32(0x7f800123),u32_as_f32(0x80000000)));
^e=isnan(sqrt((#f32)-1));^f=isinf((#f32)(1.0/0.0));^g=isfinite((#f16)3);
^h=signbit(u32_as_f32(0x80000000));
''',{'a':str(0x80000000),'b':'0','c':str(0x40000000),'d':str(0xff800123),'e':'1','f':'1','g':'1','h':'1'})

    def test_broadcasting_and_single_argument_evaluation(self):
        self.check('''::std::math;
^a:#u8[3][2]=min([[1],[2]],[5,0,3]);^b:#u8[2][2]=pow([2,3],[[2],[3]]);
^c=sqrt((#f32[0])[]);
^d:#u8={count:#u8=0;&next(){count+=1;(#f32)count} values=min([next(),next()],next());(#u8)values[1]+count*10};
^e:#u8={count:#u8=0;&next(){count+=1;(#f32)count} values=min((#f32[0])[],next());count+(#u8)|values|};
''',{'a':'[[1, 0, 1], [2, 0, 2]]','b':'[[4, 9], [8, 27]]','c':'[]','d':'32','e':'1'})
        self.check('::std::math;^a=min([1,2],[3,4,5]);',error='incompatible math broadcast shapes')

    def test_independent_types_and_finite_relations(self):
        self.check('::std::math;^a:#u8={v:#[sqrt((#f32)(1/0))]=7;(#u8)v};'
                   '^b:#u8=?{n=?(#u8,0..30);?(isqrt(n)==5);?(n==26);n};'
                   '^c:#u8=?{e=?(#u8,0..7);?(pow((#u8)3,e)==217);e};',{'a':'7','b':'26','c':'6'})
        self.check('::std::math;^a:#u8=?{n=?(#i8,-4..3);?(isqrt(n)==1);?(n==1);(#u8)n};',{'a':'1'})

    def test_scoped_imports_and_project_override(self):
        self.check('^a:#u8={::std::math;(#u8)sqrt((#f32)49)};',{'a':'7'})
        self.check('::std::math;^a:#u8=sqrt(9);',{'a':'42'},modules={'std/math.vx':'&sqrt(value){42}'})
        self.check('::std::math;^a=isqrt(9);',error='undefined function',modules={'std/math.vx':'&sqrt(value){42}'})
        self.check('[[bundled_math]] &!sqrt(value);^a=sqrt(4);',error='ABI function parameters require types')

    def test_numeric_errors_and_bit_budget(self):
        for source,error in [('sqrt(4)','floating-point'),('isqrt(-1)','nonnegative'),('pow(2,-1)','not representable'),
                             ('sqrt(1,2)','overload'),('min("a","b")','numeric'),('isqrt(2.0)','integer')]:
            self.check('::std::math;^a='+source+';',error=error)
        self.check('::std::math;^a=pow(2,65536);',error='bit budget')
        self.check('::std::math;^a:#u8={#{isqrt(-1);};7};',error='nonnegative')
        self.check('^a:#u8=?{n=?(#u32,[0,65536]);?(1<<n);7};',error='solver bit budget')

RUNTIME='::std::math;\n'
for bits in (16,32):
    for name in ROUNDING+('min','max','copysign','isnan','isinf','isfinite','signbit','abs','fmod'):
        binary=name in ('min','max','copysign','fmod');classification=name in ('isnan','isinf','isfinite','signbit')
        params=f'a:#u{bits}'+(f',b:#u{bits}' if binary else '')
        args=f'(#f{bits})(#u8[{bits//8}])a'+(f',(#f{bits})(#u8[{bits//8}])b' if binary else '')
        value=f'{name}({args})';value=f'(#u{bits})(#u8[{bits//8}])'+value if not classification else value
        RUNTIME+=f'&^m_{name}{bits}({params})->#u{bits}{{(#u{bits})({value})}}\n'
RUNTIME+='''&^m_isqrt(a:#u32)->#u32{isqrt(a)}
&^m_signed(a:#i32)->#i32{isqrt(a)}
&^m_discard(a:#i8)->#u8{isqrt(a);7}
&^m_pow(a:#u16,b:#u8)->#u16{pow(a,b)}
&^m_fixed(a:#u16)->#u16{(#u16)(#u8[2])roundeven((#i8.8)(#u8[2])a)}
&^m_narrow(a:#u32)->#u32{(#u32)(#u8[4])ceil((#u0.32)(#u8[4])a)}
&^m_floatpow(a:#u32,b:#u8)->#u32{(#u32)(#u8[4])pow((#f32)(#u8[4])a,b)}
&^m_fixedpow(a:#u16,b:#u8)->#u16{(#u16)(#u8[2])pow((#i8.8)(#u8[2])a,b)}
&^m_arrays(a:#u8)->#u8{values=min([[a],[2]],[5,0,3]);values[0][2]+values[1][0]}
'''

class MathBackendTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='vexel-math-backend-');cls.root=Path(cls.tmp.name)
        cls.source=cls.root/'main.vx';cls.source.write_text(RUNTIME);cls.project=cls.root/'project'
        run([EXE,cls.source,'--emit-msx',cls.project]);library=cls.root/'host.so'
        run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(cls.project/'generated'),'-I'+str(cls.project/'runtime'),
             *sorted((cls.project/'generated').glob('*.c')),cls.project/'runtime/vx_runtime.c','-o',library])
        cls.dll=ctypes.CDLL(str(library))
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def call(self,name,args,bits=32):
        fn=getattr(self.dll,'vx_'+name);ctype=ctypes.c_uint16 if bits==16 else ctypes.c_uint32
        fn.argtypes=[ctype]*len(args);fn.restype=ctype;return fn(*args)
    def source_result(self,name,args):
        cmd=[EXE,self.source,'--call',name,'--json']
        for arg in args:cmd += ['--arg',str(arg)]
        return int(json.loads(run(cmd).stdout)['roots'][0]['value'])

    def test_exhaustive_half_roots_and_rounding(self):
        for raw in range(65536):
            for name in ROUNDING:
                self.assertEqual(self.call('m_'+name+'16',[raw],16),oracle(name,raw,16),(name,hex(raw)))

    def test_random_single_precision_and_integer_arithmetic(self):
        rng=random.Random(7812)
        for _ in range(700):
            raw=rng.getrandbits(32)
            for name in ROUNDING:self.assertEqual(self.call('m_'+name+'32',[raw]),oracle(name,raw,32),(name,hex(raw)))
            self.assertEqual(self.call('m_isqrt',[raw]),math.isqrt(raw))
            self.assertEqual(self.call('m_narrow',[raw]),0)
            fixed=rng.getrandbits(16);signed=fixed if fixed<32768 else fixed-65536
            self.assertEqual(self.call('m_fixed',[fixed],16),(round(signed/256)*256)&65535)
            base=rng.randrange(65536);power=rng.randrange(256)
            self.assertEqual(self.call('m_pow',[base,power],16),pow(base,power,65536))
        for name,args in [('m_min32',[0x80000000,0]),('m_max32',[0x80000000,0]),('m_copysign32',[0x7f800001,0x80000000]),
                          ('m_floatpow',[0x3fc00000,5]),('m_fixedpow',[384,5]),('m_arrays',[7])]:
            self.assertEqual(self.call(name,args,16 if name=='m_fixedpow' else 32),self.source_result(name,args))

    @unittest.skipUnless(shutil.which('sdcc') and shutil.which('sdasz80'),'SDCC toolchain unavailable')
    def test_z80_math_and_discarded_traps(self):
        cases=[('m_sqrt32',[1]),('m_sqrt32',[0x7f7fffff]),('m_sqrt16',[0x8000]),('m_sqrt16',[0xbc00]),
               ('m_round32',[0xbfc00000]),('m_roundeven16',[0x4100]),('m_floor32',[0x80000001]),
               ('m_ceil16',[1]),('m_trunc32',[0xff800000]),('m_min32',[0x80000000,0]),
               ('m_max32',[0x7fc00123,0x40000000]),('m_copysign32',[0x7f800123,0x80000000]),
               ('m_isqrt',[0xffffffff]),('m_fixed',[0xfd80]),('m_narrow',[0xabcdef00]),('m_pow',[7,17]),
               ('m_floatpow',[0x3fc00000,5]),('m_fixedpow',[384,5]),('m_arrays',[7])]
        main='#include "vexel.h"\nvolatile uint32_t vx_last_result;\nvoid main(void) VX_ABI VX_RESIDENT {\n'
        for i,(name,args) in enumerate(cases,1):
            expected=self.source_result(name,args)&0xffffffff
            main+=f'if ((uint32_t)vx_{name}('+','.join(str(x)+'UL' for x in args)+f') != {expected}UL) vx_last_result={i};\n'
        main+='for (;;) { __asm halt __endasm; }}\n';(self.project/'user/main.c').write_text(main)
        run(['make','-j2','simulate','SIMFLAGS=--irq-period 997 --expanded-slot'],cwd=self.project,timeout=180)
        self.assertEqual(json.loads((self.project/'build/execution.json').read_text())['observation']['last_result'],0)
        self.assertIn('MATH_RUNTIME := 1',(self.project/'generated/modules.mk').read_text())
        for name in ('m_signed','m_discard'):
            (self.project/'user/main.c').write_text('#include "vexel.h"\nvolatile uint32_t vx_last_result;\n'
                f'void main(void) VX_ABI VX_RESIDENT {{vx_last_result=vx_{name}(-1);__asm halt __endasm;}}\n')
            result=subprocess.run(['make','-j2','simulate'],cwd=self.project,env=project_environment(),capture_output=True,text=True,timeout=180)
            self.assertNotEqual(result.returncode,0);self.assertIn('runtime trapped with reason 10',result.stderr)

if __name__=='__main__':unittest.main(verbosity=2)
