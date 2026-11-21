#!/usr/bin/env python3
"""Derived integer ranges can narrow storage without changing scalar semantics."""
import argparse
import contextlib
import importlib.util
import json
from pathlib import Path
import tempfile

ROOT=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('support',ROOT/'examples/relational_source_msx.py')
support=importlib.util.module_from_spec(spec);spec.loader.exec_module(support)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('solver',type=Path);parser.add_argument('--msx',action='store_true');parser.add_argument('--output',type=Path)
args=parser.parse_args();solver=args.solver.resolve()
SOURCE='''
[[c_binding(word,full,1,exclusive)]]
&^word(n:#u16,m:#u16,flag:#u8) {
 q:#u16=n>>8;r:#u16=(n&255)+256;
 (q,q+1,q==m?(#u8)7:(#u8)3,flag!=0?q:r,r,r-m)
}
[[c_binding(signed_range,full,1,exclusive)]]
&^signed_range(n:#i16,m:#i16) {
 q:#i16=n/512;r:#i16=n%512;s:#i16=n>>9;
 (q,r,s,q+1,s==m?(#u8)1:(#u8)0)
}
[[c_binding(wide,full,1,exclusive)]]
&^wide(n:#u32,m:#u32) {
 lo:#u32=n&255;hi:#u32=n>>24;x:#u32=lo+hi;
 (lo,hi,x,(x*x)&65535,lo==m?(#u8)1:(#u8)0,(lo<<24)|hi)
}
[[c_binding(wrapped,full,1,exclusive)]]
&^wrapped(n:#u8) {
 v:#u8=(n&7)+250;w:#i8=(#i8)n;
 (v,(#u16)v,(#i16)w,((#i16)w)<<1,v<6?(#u8)1:(#u8)0)
}
[[c_binding(widen,full,1,exclusive)]]
&^widen(a:#u32,b:#u32) {
 v:#u16=(#u16)b;
 s:#i8=((#i8)a<(#i8)v) ? (#i8)(v+(#u16)b) : (#i8)((#u16)b^(#u16)a);
 q:#u8=(#u8)s;
 w:#u16=(#u16)((#i16)a+(#i16)b);
 ((#u32)q,(#u32)(q+q),(#u32)(q+q+q),(#u32)(q+q)+100000,
  (#u32)w,(#u32)(w+w),(#u32)(w+w+w))
}
&axis(a:#u16,b:#u16,half:#u8)->#u8 {
 distance:#u8=(#u8)((#u16)(a-b)>>5);
 (#u8)(distance+half)<=(#u8)(half<<1) ? 1 : 0
}
[[c_binding(box,full,1,exclusive)]]
&^box(a:#u16,b:#u16,c:#u16,d:#u16,rows:#u8,cols:#u8)->#u8 {
 h:#u8=(#u8)(((#u16)rows+16)>>1);w:#u8=(#u8)(((#u16)cols+16)>>1);
 axis(a,c,h)!=0 && axis(b,d,w)!=0 ? 1 : 0
}
[[c_binding(region,region,1,exclusive)]]
&^region(n:#u16) {
 v:#u16=n;i:#u8=0;
 (i<2)!@{q:#u16=v>>8;v=q*q+q;i+=1;};v
}
'''


def vectors():
    rows={name:[] for name in ('word','signed_range','wide','wrapped','widen','box','region')}
    words=sorted({0,1,127,128,255,256,511,512,513,32767,32768,65534,65535,
                  *(n*257 for n in range(0,256,17))})
    for n in words:
        q=n>>8;r=(n&255)+256
        for m in (q,n,65535):
            for flag in (0,1):rows['word'].append(([n,m,flag],[q,q+1,7 if q==m else 3,q if flag else r,r,r-m]))
        x=n
        for _ in range(2):x=((x>>8)**2+(x>>8))&65535
        rows['region'].append(([n],[x]))
    for n in sorted({-32768,-32767,-513,-512,-511,-1,0,1,511,512,513,32766,32767,
                     *(i*257-32768 for i in range(0,256,17))}):
        q=(abs(n)//512)*(-1 if n<0 else 1);s=n>>9;r=n-q*512
        for m in (s,n):rows['signed_range'].append(([n,m],[q,r,s,q+1,int(s==m)]))
    for n in sorted({0,1,255,256,65535,65536,0x7fffffff,0x80000000,0xffffffff,
                     *(i*0x01010101 for i in range(0,256,17))}):
        lo=n&255;hi=n>>24;x=lo+hi
        for m in (lo,n):rows['wide'].append(([n,m],[lo,hi,x,(x*x)&65535,int(lo==m),(lo<<24)|hi]))
    for n in range(256):
        v=((n&7)+250)&255;w=n if n<128 else n-256
        rows['wrapped'].append(([n],[v,v,w,w*2,int(v<6)]))
    # A retained signed cell can feed a wrapped unsigned computation which
    # is then widened. Removing another shared temporary exposed SDCC's
    # incorrect sign extension of the inlined doubled byte.
    edges=(0,1,127,128,255,256,32767,32768,65535,65536,0x7fffffff,0x80000000,0xffffffff)
    signed_byte=lambda n:(n&255)-(256 if n&128 else 0)
    for a,b in [(n,n) for n in edges]+[(n,0xffffffff-n) for n in edges]:
        q=(2*b if signed_byte(a)<signed_byte(b) else b^a)&255
        w=(a+b)&65535
        rows['widen'].append(([a,b],[q,(2*q)&255,(3*q)&255,((2*q)&255)+100000,
                                  w,(2*w)&65535,(3*w)&65535]))
    # Two shifted narrowed cells feed both an addition and its comparison.
    # This includes the Z80 counterexample found by the linked game probe.
    inputs=[[10,13,14,20,21,127]]
    for size in (0,1,127,128,239,240,254,255):
        for distance in (-257,-32,-1,0,1,31,32,257):
            inputs.append([distance&65535,(-distance)&65535,0,0,size,255-size])
    for a,b,c,d,r,s in inputs:
        h=(r+16)>>1;w=(s+16)>>1
        axis=lambda x,y,half: ((((x-y)&65535)>>5)+half)&255 <= (half<<1)&255
        rows['box'].append(([a,b,c,d,r,s],[int(axis(a,c,h) and axis(b,d,w))]))
    return rows


if args.output:args.output.mkdir(parents=True,exist_ok=True)
workspace=contextlib.nullcontext(args.output.resolve()) if args.output else tempfile.TemporaryDirectory(prefix='vexel-value-ranges-')
with workspace as temporary:
    root=Path(temporary);source=root/'ranges.vx';source.write_text(SOURCE)
    rows=vectors()
    for budget,reuse in ((0,1),(256,1),(256,0)):
        label=f'{budget}-{reuse}';generated=root/('generated-'+label)
        report=json.loads(support.run([solver,source,'--program','--prefer','value_frame_bytes='+str(budget),
            '--prefer','value_scratch_reuse='+str(reuse),'--prefer','value_table_bytes=0',
            '--prefer','value_choice_branches=0','--emit-c',generated]).stdout)
        bindings={b['name']:b for b in report['value_bindings']['bindings']}
        sources={s['name']:s for s in report['sources']}
        clients=[]
        for number,(name,cases) in enumerate(rows.items(),1):
            binding=bindings[name];original=sources[name]
            assert binding['storage']==('fixed' if budget else 'automatic'),binding
            inputs=[l for p in original['parameters'] for l in p['leaves']]
            outputs=original['leaves']
            assert all(len(a)==len(inputs) and len(b)==len(outputs) for a,b in cases),name
            table=',\n'.join('{'+','.join(str(v&0xffffffff)+'UL' for v in a+b)+'}' for a,b in cases)
            ctype=lambda l: ('int' if l['logical_type'][1]=='i' else 'uint')+l['logical_type'][2:]+'_t'
            text='#include "vx_bind_'+name+'.h"\n'
            text+=f'static const uint32_t data[{len(cases)}][{len(inputs)+len(outputs)}]={{\n'+table+'};\n'
            text+=f'uint32_t check_{name}(void){{unsigned row;VX_BIND_{name}_LOCAL;\n'
            text+=f'for(row=0;row<{len(cases)};++row){{\n'
            for i,leaf in enumerate(inputs):text+=f'vx_bind_{name}_frame.a{i}=({ctype(leaf)})data[row][{i}];\n'
            text+=f'vx_bind_{name}_run();\n'
            for i,leaf in enumerate(outputs):
                mask=(1<<int(leaf['logical_type'][2:]))-1
                text+=f'if((((uint32_t)vx_bind_{name}_frame.result.v{i})&{mask}UL)!=(data[row][{len(inputs)+i}]&{mask}UL))return {number*1000}UL+row;\n'
            text+='}return 0;}\n'
            path=root/(name+'-'+label+'.c');path.write_text(text);clients.append(path)
        main='#include <stdint.h>\n'+''.join('uint32_t check_'+name+'(void);\n' for name in rows)
        main+='static uint32_t check(void){uint32_t failed;\n'
        main+=''.join('failed=check_'+name+'();if(failed)return failed;\n' for name in rows)+'return 0;}\n'
        host=root/('main-'+label+'.c');host.write_text(main+'''#include <stdio.h>
#include <stdlib.h>
void vx_trap(uint8_t reason){exit(reason);}
int main(void){uint32_t result=check();if(result)printf("failed %lu\\n",(unsigned long)result);return result!=0;}
''')
        binary=root/('host-'+label)
        support.run(['cc','-std=c11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',
            '-I'+str(generated),'-I'+str(ROOT/'backends/msx-neo8/runtime'),*clients,
            generated/'vx_binding_frames.c',host,'-o',binary]);support.run([binary])
        if args.msx:
            # Keep all vectors, but separate the resident harnesses. Their
            # combined tables exceed a cartridge's fixed address window.
            for name,path in zip(rows,clients):
                project=root/('project-'+label+'-'+name);support.backend.package(generated,project,source,solver)
                (project/'user/fixture.c').write_text(path.read_text())
                (project/'user/frames.c').write_text('#include "vx_binding_frames.c"\n')
                (project/'user/main.c').write_text('#include "vexel.h"\nuint32_t check_'+name+'(void);\n'+'''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check_'''+name+'''();__asm halt __endasm;}
''')
                result=support.run(['make','-C',project,'-j1','simulate','SIMFLAGS=--max-ticks=400000000'])
                (root/('build-'+label+'-'+name+'.log')).write_text(result.stdout+result.stderr)
                observation=json.loads((project/'build/execution.json').read_text())['observation']
                assert observation['last_result']==observation['fault']==0,(name,observation)
        print(label,{name:b['bytes'] for name,b in bindings.items()},flush=True)
print('Integer value ranges: signed/unsigned wrapping, narrowed scratch, wide result copies, equality and regions passed'+
      (' on host and Z80' if args.msx else ' (UBSan)'))
