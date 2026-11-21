#!/usr/bin/env python3
"""Finite realizations across computed byte values, with independent inputs."""
import argparse
import contextlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('solver',type=Path)
parser.add_argument('--msx',action='store_true')
parser.add_argument('--output',type=Path)
args=parser.parse_args();solver=args.solver.resolve()


def run(command):
    r=subprocess.run(list(map(str,command)),capture_output=True,text=True,timeout=300)
    assert r.returncode==0,f'{command}\n{r.stdout}\n{r.stderr}'
    return r


SOURCE='''
[[c_binding(mixed,full,1,exclusive)]]
&^mixed(x:#u8,y:#u8){z:#u8=x^y;(z*7+2,x)}
[[c_binding(selected,full,1,exclusive)]]
&^selected(x:#u8,y:#u8,flag:#u8){z:#u8=flag!=0 ? x : y;z*z+3}
[[c_binding(narrowed,full,1,exclusive)]]
&^narrowed(w:#u16,y:#u8){z:#u8=(#u8)(w+(#u16)y);z*11}
[[c_binding(signed_cut,full,1,exclusive)]]
&^signed_cut(x:#u8,y:#u8){s:#i8=(#i8)(x+y);
    s < -64 ? (#i16)-300 : (s < -32 ? (#i16)-1 : (s < 0 ? (#i16)3 :
    (s < 32 ? (#i16)256 : (s < 64 ? (#i16)32767 : (#i16)-32768))))
}
[[c_binding(independent,full,1,exclusive)]]
&^independent(x:#u8,y:#u8){x*y}
[[c_binding(budget,full,1,exclusive)]]
&^budget(x:#u8,y:#u8){(#u32)(x^y)*(#u32)17293}
'''
names=('mixed','selected','narrowed','signed_cut','independent','budget')
client=''.join(f'#include "vx_bind_{n}.h"\n' for n in names)
client+='static const uint32_t wide_products[]={'+','.join(str(i*17293)+'UL' for i in range(256))+'};\n'
client+='static const uint8_t squares[]={'+','.join(str((i*i+3)&255) for i in range(256))+'};\n'
client+='''
uint32_t check_cuts(void) {
    uint16_t i,j,k,w,z,t;uint8_t product;int16_t s,expected;
    static const uint8_t samples[]={0,1,2,63,127,128,254,255};
'''+''.join(f'    VX_BIND_{n}_LOCAL;\n' for n in names)+'''
    for(i=0;i<256;++i)for(k=0;k<
#ifdef __SDCC
        sizeof(samples)
#else
        256
#endif
        ;++k) {
#ifdef __SDCC
        j=samples[k];
#else
        j=k;
#endif
        z=i^j;
        vx_bind_mixed_frame.a0=(uint8_t)i;vx_bind_mixed_frame.a1=(uint8_t)j;vx_bind_mixed_run();
        if(vx_bind_mixed_frame.result.v0!=(uint8_t)((z<<3)-z+2) || vx_bind_mixed_frame.result.v1!=i)return 1;
        vx_bind_selected_frame.a0=(uint8_t)i;vx_bind_selected_frame.a1=(uint8_t)j;
        vx_bind_selected_frame.a2=1;vx_bind_selected_run();
        if(vx_bind_selected_frame.result.v0!=squares[i])return 2;
        vx_bind_selected_frame.a2=0;vx_bind_selected_run();
        if(vx_bind_selected_frame.result.v0!=squares[j])return 3;
        w=(uint16_t)((i<<8)|i);z=(i+j)&255;
        vx_bind_narrowed_frame.a0=w;vx_bind_narrowed_frame.a1=(uint8_t)j;vx_bind_narrowed_run();
        if(vx_bind_narrowed_frame.result.v0!=(uint8_t)((z<<3)+(z<<1)+z))return 4;
        s=z<128 ? (int16_t)z : (int16_t)z-256;
        expected=s < -64 ? -300 : s < -32 ? -1 : s < 0 ? 3 : s < 32 ? 256 : s < 64 ? 32767 : -32768;
        vx_bind_signed_cut_frame.a0=(uint8_t)i;vx_bind_signed_cut_frame.a1=(uint8_t)j;vx_bind_signed_cut_run();
        if(vx_bind_signed_cut_frame.result.v0!=expected)return 5;
        vx_bind_independent_frame.a0=(uint8_t)i;vx_bind_independent_frame.a1=(uint8_t)j;vx_bind_independent_run();
        product=0;for(t=0;t<j;++t)product=(uint8_t)(product+i);
        if(vx_bind_independent_frame.result.v0!=product)return 6;
        vx_bind_budget_frame.a0=(uint8_t)i;vx_bind_budget_frame.a1=(uint8_t)j;vx_bind_budget_run();
        if(vx_bind_budget_frame.result.v0!=wide_products[i^j])return 7;
    }
    return 0;
}
'''
if args.output:args.output.mkdir(parents=True,exist_ok=True)
workspace=contextlib.nullcontext(args.output.resolve()) if args.output else tempfile.TemporaryDirectory(prefix='vexel-finite-cuts-')
with workspace as temporary:
    root=Path(temporary);source=root/'cuts.vx';source.write_text(SOURCE)
    for budget in (0,64):
        generated=root/f'generated-{budget}'
        report=json.loads(run([solver,source,'--program','--prefer',f'value_frame_bytes={budget}','--emit-c',generated]).stdout)
        plans={b['name']:b for b in report['value_bindings']['bindings']}
        for name in names:
            plan=plans[name];assert plan['storage']==('fixed' if budget else 'automatic'),plan
            assert (plan['codegen']['finite_tables']>0)==(name not in ('independent','budget')),plan
            assert plan['codegen']['finite_table_bytes']<=512,plan
        host=root/f'host-{budget}.c';binary=root/f'host-{budget}'
        host.write_text(client+'\n#include <stdio.h>\nint main(void){uint32_t r=check_cuts();if(r)printf("cut mismatch %lu\\n",(unsigned long)r);return r!=0;}\n')
        run(['cc','-std=c11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',
             '-I'+str(generated),'-I'+str(ROOT/'backends/msx-neo8/runtime'),host,
             generated/'vx_binding_frames.c','-o',binary]);run([binary])
        if args.msx:
            spec=importlib.util.spec_from_file_location('project',ROOT/'experimental/relational/project_msx.py')
            backend=importlib.util.module_from_spec(spec);spec.loader.exec_module(backend)
            project=root/f'project-{budget}';backend.package(generated,project,source,solver)
            (project/'user/frames.c').write_text('#include "vx_binding_frames.c"\n')
            (project/'user/main.c').write_text(client+'''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check_cuts();__asm halt __endasm;}
''')
            built=run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=400000000',
                       'SDCCFLAGS=-mz80 --std-sdcc11 --sdcccall 0 --max-allocs-per-node 2000'])
            (root/f'build-{budget}.log').write_text(built.stdout+built.stderr)
            execution=json.loads((project/'build/execution.json').read_text())
            assert execution['observation']['last_result']==execution['observation']['fault']==0,execution
print('Finite cuts: computed, selected, narrowed and signed inputs; independent operands and ROM fallback passed'+
      (' on host and Z80' if args.msx else ' (UBSan)'))
