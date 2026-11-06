#!/usr/bin/env python3
"""Source ABI facts: aggregate flattening, partial queries and entry validation."""
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
def run(args):return subprocess.run([str(x) for x in args],text=True,capture_output=True,timeout=120)
def scalar_type(text):
    if text=='#b':return 'uint8_t',ctypes.c_uint8
    width=int(text[2:].split('.')[0])
    if '.' in text:width+=int(text.split('.')[1])
    width=8 if width<=8 else 16 if width<=16 else 32
    signed=text.startswith('#i')
    return ('int' if signed else 'uint')+str(width)+'_t',getattr(ctypes,('c_int' if signed else 'c_uint')+str(width))

class InputTests(unittest.TestCase):
    def execute(self,source,cases,paths=None,bindings='',after=''):
        for schedule,branch in ORDERS:
            with self.subTest(source=source,schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-inputs-') as temp:
                d=Path(temp);path=d/'input.vx';generated=d/'generated';path.write_text(source)
                result=run([EXE,path,'--source','entry','--emit-c',generated,'--schedule',schedule,'--branch-order',branch])
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                report=json.loads(result.stdout);parameters=report['sources'][0]['parameters']
                leaves=[leaf for parameter in parameters for leaf in parameter['leaves']]
                if paths is not None:self.assertEqual([leaf['path'] for leaf in leaves],paths)
                types=[scalar_type(leaf['logical_type']) for leaf in leaves]
                args=', '.join(t[0]+' p'+str(i) for i,t in enumerate(types)) or 'void'
                actual=', '.join('p'+str(i) for i in range(len(leaves)))
                harness=d/'harness.c';harness.write_text('#include "vexel.h"\n#include <setjmp.h>\n'+bindings+'\n'
                    'static jmp_buf trapped;void vx_trap(uint8_t reason){longjmp(trapped,reason);}\n'
                    'unsigned checked('+args+'){int r=setjmp(trapped);if(r)return r;vx_entry('+actual+');'+after+'return 0;}\n')
                rt=ROOT/'backends/msx-neo8/runtime';runtime=d/'runtime.c'
                runtime.write_text((rt/'vx_runtime.c').read_text().replace('void vx_trap(uint8_t reason)','void unused_trap(uint8_t reason)',1))
                library=d/'run.so';built=run(['cc','-std=c11','-O2','-shared','-fPIC','-DVX_FIXED_RUNTIME=1','-DVX_FLOAT_RUNTIME=1',
                    '-I'+str(generated),'-I'+str(rt),generated/'vx_bridges.c',runtime,harness,'-o',library])
                self.assertEqual(built.returncode,0,built.stderr)
                dll=ctypes.CDLL(str(library));dll.checked.argtypes=[t[1] for t in types];dll.checked.restype=ctypes.c_uint
                dll.vx_entry_read.argtypes=[ctypes.c_uint16];dll.vx_entry_read.restype=ctypes.c_uint32
                for args,expected in cases:
                    reason=dll.checked(*args)
                    if isinstance(expected,tuple):self.assertEqual(reason,expected[0],args)
                    else:
                        self.assertEqual(reason,0,args)
                        self.assertEqual([dll.vx_entry_read(i) for i in range(len(expected))],[v&0xffffffff for v in expected],args)

    def test_nested_values_copies_native_receivers_and_flat_order(self):
        source='#Item(x:#u8,y:#i16);#Packet(items:#Item[2],scale:#u16);'
        source+='&~(p)#Packet::change(k:#u8){p.items[0].x+=k;p.items[1].y-=1;}'
        source+='&^entry(p:#Packet,n:#u8){old=p;p.change(n);(p.items[0].x,p.items[1].y,p.scale,old.items[0].x)}'
        cases=[([n,1000-n,(n+1)%256,-n,300+n,n],[(2*n)%256,-n-1,300+n,n]) for n in range(256)]
        self.execute(source,cases,['p.items[0].x','p.items[0].y','p.items[1].x','p.items[1].y','p.scale','n'])
        self.execute('&^entry(p:(#u8[2][2],#i8),n:#u8){(p.__0[n%2][(n/2)%2],p.__1)}',
            [([n,(n+1)%256,(n+2)%256,(n+3)%256,n%128-64,n],[(n+((n%2)*2+(n//2)%2))%256,n%128-64]) for n in range(256)],
            ['p.__0[0][0]','p.__0[0][1]','p.__0[1][0]','p.__0[1][1]','p.__1','n'])

    def test_schema_and_scalar_validation_before_effects(self):
        # Even an unused parameter must satisfy its fixed interface before a
        # source body invokes a sink. No source annotation invents zero data.
        source='#Packet(n:#u8,data:#u8[n]);&!hit();&^entry(p:#[#Packet(3,[1,2,3])]){hit();7}'
        self.execute(source,[([n,4,5,6],[7] if n==3 else (19,)) for n in range(256)],
            bindings='static unsigned calls;void vx_external_hit(uint32_t *out){(void)out;++calls;}',
            after='if(calls!=1)return 99;calls=0;')
        self.execute('&^entry(p:(#b,#u3,#i5)){p.__0 ? (#i16)p.__1+(#i16)p.__2 : (#i16)0}',
            [([1,7,-16],[-9]),([0,1,15],[0]),([2,0,0],(16,)),([1,8,0],(16,)),([1,0,16],(16,)),([1,0,-17],(16,))])
        self.execute('&^entry([[range(2,7)]] n:#u8){n+1}',[([n],[n+1] if 2<=n<=7 else (16,)) for n in range(256)])

    def test_partial_properties_aliases_and_empty_inputs(self):
        self.execute('shape:#u8[3];&^entry(a:#[shape],b:#[a],empty:#u16[0]){a[0]+b[2]+(#u8)|empty|}',
            [([n,2,3,4,5,(n+1)%256],[(2*n+1)%256]) for n in range(256)],
            ['a[0]','a[1]','a[2]','b[0]','b[1]','b[2]'])
        with tempfile.TemporaryDirectory(prefix='vexel-input-partial-') as temp:
            path=Path(temp)/'input.vx';path.write_text('#Packet(a:#u8,b:#Missing);&^entry(p:#Packet){p.a}')
            for schedule,branch in ORDERS:
                r=run([EXE,path,'--query','type:entry','--schedule',schedule,'--branch-order',branch])
                self.assertEqual(r.returncode,0,r.stdout+r.stderr)
                self.assertEqual(json.loads(r.stdout)['queries'][0]['answer'],'#u8')
                r=run([EXE,path,'--source','entry','--schedule',schedule,'--branch-order',branch])
                self.assertEqual(json.loads(r.stdout)['status'],'unresolved',r.stdout+r.stderr)

    def test_more_sources_and_larger_output_arena(self):
        with tempfile.TemporaryDirectory(prefix='vexel-input-many-') as temp:
            d=Path(temp);path=d/'input.vx';generated=d/'generated'
            path.write_text(''.join('&^s'+str(i)+'(values:#u8[40]){values}' for i in range(6)))
            r=run([EXE,path,*[arg for i in range(6) for arg in ['--source','s'+str(i)]],'--budget','80','--emit-c',generated])
            self.assertEqual(r.returncode,0,r.stdout+r.stderr);report=json.loads(r.stdout)
            self.assertEqual(len(report['sources']),6);self.assertEqual(report['extent'],80)
            rt=ROOT/'backends/msx-neo8/runtime';library=d/'run.so'
            built=run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(generated),'-I'+str(rt),generated/'vx_bridges.c',rt/'vx_runtime.c','-o',library])
            self.assertEqual(built.returncode,0,built.stderr);dll=ctypes.CDLL(str(library))
            for i in range(6):
                invoke=getattr(dll,'vx_s'+str(i));invoke.argtypes=[ctypes.c_uint8]*40
                read=getattr(dll,'vx_s'+str(i)+'_read');read.argtypes=[ctypes.c_uint16];read.restype=ctypes.c_uint32
                values=[(k*7+i*13)%256 for k in range(40)];invoke(*values)
                self.assertEqual([read(k) for k in range(40)],values)

if __name__=='__main__':unittest.main(verbosity=2)
