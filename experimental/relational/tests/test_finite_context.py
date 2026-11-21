#!/usr/bin/env python3
"""Branch facts cross finite interfaces while keeping independent and narrowed inputs."""
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
[[c_binding(selected,full,1,exclusive)]]
&^selected(x:#u8,y:#u8,flag:#u8){z:#u8=flag!=0 ? x : y;(z*7,(#u16)z+(#u16)x,(#u16)z+(#u16)y)}
[[c_binding(equality,full,1,exclusive)]]
&^equality(x:#u8,y:#u8){z:#u8=x==y ? (#u8)1 : x;(z*7,(#u16)z+(#u16)y)}
[[c_binding(narrowed,full,1,exclusive)]]
&^narrowed(w:#u16,y:#u8){n:#u8=(#u8)w;z:#u8=n==1 ? n : y;(z*7,(#u16)z+w)}
'''
CLIENT='''
#include "vx_bind_selected.h"
#include "vx_bind_equality.h"
#include "vx_bind_narrowed.h"
uint32_t check_context(void) {
    uint16_t x,y,k,flag,z,w;static const uint8_t samples[]={0,1,2,63,127,128,254,255};
    VX_BIND_selected_LOCAL;VX_BIND_equality_LOCAL;VX_BIND_narrowed_LOCAL;
    for(x=0;x<256;++x)for(k=0;k<
#ifdef __SDCC
        sizeof(samples)
#else
        256
#endif
        ;++k) {
#ifdef __SDCC
        y=samples[k];
#else
        y=k;
#endif
        for(flag=0;flag<2;++flag) {
            z=flag ? x : y;
            vx_bind_selected_frame.a0=(uint8_t)x;vx_bind_selected_frame.a1=(uint8_t)y;
            vx_bind_selected_frame.a2=(uint8_t)flag;vx_bind_selected_run();
            if(vx_bind_selected_frame.result.v0!=(uint8_t)((z<<3)-z) ||
               vx_bind_selected_frame.result.v1!=z+x || vx_bind_selected_frame.result.v2!=z+y)return 1;
        }
        z=x==y ? 1 : x;
        vx_bind_equality_frame.a0=(uint8_t)x;vx_bind_equality_frame.a1=(uint8_t)y;vx_bind_equality_run();
        if(vx_bind_equality_frame.result.v0!=(uint8_t)((z<<3)-z) || vx_bind_equality_frame.result.v1!=z+y)return 2;
        w=(x<<8)|x;z=x==1 ? 1 : y;
        vx_bind_narrowed_frame.a0=w;vx_bind_narrowed_frame.a1=(uint8_t)y;vx_bind_narrowed_run();
        if(vx_bind_narrowed_frame.result.v0!=(uint8_t)((z<<3)-z) ||
           vx_bind_narrowed_frame.result.v1!=(uint16_t)((uint32_t)z+w))return 3;
    }return 0;
}
'''
if args.output:args.output.mkdir(parents=True,exist_ok=True)
workspace=contextlib.nullcontext(args.output.resolve()) if args.output else tempfile.TemporaryDirectory(prefix='vexel-finite-context-')
with workspace as temporary:
    root=Path(temporary);source=root/'context.vx';source.write_text(SOURCE)
    for frame,choice in ((frame,choice) for frame in (0,64) for choice in (0,8)):
        tag=f'{frame}-{choice}';generated=root/f'generated-{tag}'
        report=json.loads(support.run([solver,source,'--program','--prefer',f'value_frame_bytes={frame}',
                                      '--prefer',f'value_choice_branches={choice}','--emit-c',generated]).stdout)
        for plan in report['value_bindings']['bindings']:
            assert plan['codegen']['finite_tables']>0,plan
            assert (plan['codegen']['shared_select_branches']>0)==bool(choice),plan
        if choice:
            header=(generated/'vx_bind_selected.h').read_text()
            lookups=[line for line in header.splitlines() if 'result.v0 =' in line and 'vx_lut_' in line]
            assert len(lookups)==2 and all(' ? ' not in line for line in lookups),header
            header=(generated/'vx_bind_equality.h').read_text()
            assert any('result.v0 =' in line and '(7)' in line and 'vx_lut_' not in line for line in header.splitlines()),header
        client=root/f'host-{tag}.c';binary=root/f'host-{tag}'
        client.write_text(CLIENT+'\nint main(void){return check_context()!=0;}\n')
        support.run(['cc','-std=c11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',
                     '-I'+str(generated),'-I'+str(ROOT/'backends/msx-neo8/runtime'),client,
                     generated/'vx_binding_frames.c','-o',binary]);support.run([binary])
        if args.msx:
            project=root/f'project-{tag}';support.backend.package(generated,project,source,solver)
            (project/'user/frames.c').write_text('#include "vx_binding_frames.c"\n')
            (project/'user/main.c').write_text(CLIENT+'''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check_context();__asm halt __endasm;}
''')
            build=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=400000000',
                              'SDCCFLAGS=-mz80 --std-sdcc11 --sdcccall 0 --max-allocs-per-node 2000'])
            (root/f'build-{tag}.log').write_text(build.stdout+build.stderr)
            result=json.loads((project/'build/execution.json').read_text())
            assert result['observation']['last_result']==result['observation']['fault']==0,result
print('Finite context: selected indices, constant table evaluation and narrowed equality with distinct wide inputs passed'+
      (' on host and Z80' if args.msx else ' (UBSan)'))
