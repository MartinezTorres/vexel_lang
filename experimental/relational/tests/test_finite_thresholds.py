#!/usr/bin/env python3
"""Finite certificates admit exact threshold comparisons, including computed and signed byte cuts."""
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
[[c_binding(threshold,full,1,exclusive)]]
&^threshold(x:#u8){((x>>4)*7)<56 ? (#i16)-300 : (#i16)700}
[[c_binding(computed,full,1,exclusive)]]
&^computed(x:#u8,y:#u8){z:#u8=x+y;((z>>4)*7)<56 ? (#u16)65535 : (#u16)300}
[[c_binding(sign_bit,full,1,exclusive)]]
&^sign_bit(x:#i8){u:#u8=(#u8)x;((u>>4)*7)<56 ? (#i16)700 : (#i16)-300}
[[c_binding(specialized,full,1,exclusive)]]
&^specialized(x:#u8,y:#u8){z:#u8=x==y ? (#u8)3 : x;
    (((z>>4)*7)<56 ? (#i16)-300 : (#i16)700,(#u16)z+(#u16)y)}
[[c_binding(periodic,full,1,exclusive)]]
&^periodic(x:#u8){((x&7)*7)<28 ? (#u8)9 : (#u8)4}
'''
CLIENT='''
#include "vx_bind_threshold.h"
#include "vx_bind_computed.h"
#include "vx_bind_sign_bit.h"
#include "vx_bind_specialized.h"
#include "vx_bind_periodic.h"
uint32_t check_thresholds(void) {
    uint16_t x,y,k,z;static const uint8_t samples[]={0,1,3,63,127,128,254,255};
    VX_BIND_threshold_LOCAL;VX_BIND_computed_LOCAL;VX_BIND_sign_bit_LOCAL;
    VX_BIND_specialized_LOCAL;VX_BIND_periodic_LOCAL;
    for(x=0;x<256;++x) {
        vx_bind_threshold_frame.a0=(uint8_t)x;vx_bind_threshold_run();
        if(vx_bind_threshold_frame.result.v0!=(x<128 ? -300 : 700))return 1;
        vx_bind_sign_bit_frame.a0=VX_VALUE_I8(x);vx_bind_sign_bit_run();
        if(vx_bind_sign_bit_frame.result.v0!=(x<128 ? 700 : -300))return 2;
        vx_bind_periodic_frame.a0=(uint8_t)x;vx_bind_periodic_run();
        if(vx_bind_periodic_frame.result.v0!=((x&7)<4 ? 9 : 4))return 3;
        for(k=0;k<
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
            z=(x+y)&255;
            vx_bind_computed_frame.a0=(uint8_t)x;vx_bind_computed_frame.a1=(uint8_t)y;vx_bind_computed_run();
            if(vx_bind_computed_frame.result.v0!=(z<128 ? 65535U : 300U))return 4;
            z=x==y ? 3 : x;
            vx_bind_specialized_frame.a0=(uint8_t)x;vx_bind_specialized_frame.a1=(uint8_t)y;vx_bind_specialized_run();
            if(vx_bind_specialized_frame.result.v0!=(z<128 ? -300 : 700) ||
               vx_bind_specialized_frame.result.v1!=z+y)return 5;
        }
    }return 0;
}
'''
if args.output:args.output.mkdir(parents=True,exist_ok=True)
workspace=contextlib.nullcontext(args.output.resolve()) if args.output else tempfile.TemporaryDirectory(prefix='vexel-finite-thresholds-')
with workspace as temporary:
    root=Path(temporary);source=root/'thresholds.vx';source.write_text(SOURCE)
    for frame,choice in ((frame,choice) for frame in (0,64) for choice in (0,8)):
        tag=f'{frame}-{choice}';generated=root/f'generated-{tag}'
        report=json.loads(support.run([solver,source,'--program','--prefer',f'value_frame_bytes={frame}',
                                      '--prefer',f'value_choice_branches={choice}','--emit-c',generated]).stdout)
        for plan in report['value_bindings']['bindings']:
            if plan['name']=='periodic':continue
            assert plan['codegen']['finite_tables']==plan['codegen']['finite_table_bytes']==0,plan
            assert plan['codegen']['finite_guards']>0,plan
            assert 'vx_lut_' not in (generated/('vx_bind_'+plan['name']+'.h')).read_text()
        client=root/f'host-{tag}.c';binary=root/f'host-{tag}'
        client.write_text(CLIENT+'\nint main(void){return check_thresholds()!=0;}\n')
        support.run(['cc','-std=c11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',
                     '-I'+str(generated),'-I'+str(ROOT/'backends/msx-neo8/runtime'),client,
                     generated/'vx_binding_frames.c','-o',binary]);support.run([binary])
        if args.msx:
            project=root/f'project-{tag}';support.backend.package(generated,project,source,solver)
            (project/'user/frames.c').write_text('#include "vx_binding_frames.c"\n')
            (project/'user/main.c').write_text(CLIENT+'''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check_thresholds();__asm halt __endasm;}
''')
            build=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=400000000',
                              'SDCCFLAGS=-mz80 --std-sdcc11 --sdcccall 0 --max-allocs-per-node 2000'])
            (root/f'build-{tag}.log').write_text(build.stdout+build.stderr)
            result=json.loads((project/'build/execution.json').read_text())
            assert result['observation']['last_result']==result['observation']['fault']==0,result
print('Finite thresholds: exact byte domain, modular computed cuts, signed inputs, branch-known indices and periodic fallback passed'+
      (' on host and Z80' if args.msx else ' (UBSan)'))
