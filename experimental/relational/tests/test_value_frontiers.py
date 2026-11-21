#!/usr/bin/env python3
"""Already computed pure producers remain available after fixed outputs and across branch splits."""
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
[[c_binding(frontier,full,1,exclusive)]]
&^frontier(x:#u8,y:#u8,dir:#u8,ox:#u16,oy:#u16){
    room:#u8=20-x;
    step:#u8=x+(y<room ? y : room);
    index:#u8=(step-1)>>2;
    a:#i16=(#i16)(index==0 ? 333 : (index==1 ? 192 : (index==2 ? 0 : (index==3 ? -192 : -333))))*(#i16)3;
    b:#i16=(#i16)(index==0 ? -192 : (index==1 ? -333 : (index==2 ? -384 : (index==3 ? -333 : -192))))*(#i16)3;
    (step,step<20,ox+(#u16)(dir==1 ? a : -a),
     oy+(#u16)(dir==1 ? b : -b))
}
'''
CLIENT='''
#include "vx_bind_frontier.h"
uint32_t check_frontier(void) {
    uint16_t x,y,k,dir,ox,oy;uint8_t room,step,index;int16_t a,b;
    static const uint8_t samples[]={0,1,2,19,20,21,127,128,254,255};
    VX_BIND_frontier_LOCAL;
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
        room=(uint8_t)(20-x);step=(uint8_t)(x+(y<room ? y : room));
        index=(uint8_t)(step-1);index>>=2;
        a=3*(index==0 ? 333 : index==1 ? 192 : index==2 ? 0 : index==3 ? -192 : -333);
        b=3*(index==0 ? -192 : index==1 ? -333 : index==2 ? -384 : index==3 ? -333 : -192);
        ox=(x<<8)|y;oy=65535-ox;
        for(dir=0;dir<3;++dir) {
            vx_bind_frontier_frame.a0=(uint8_t)x;vx_bind_frontier_frame.a1=(uint8_t)y;
            vx_bind_frontier_frame.a2=(uint8_t)(dir==2 ? 255 : dir);
            vx_bind_frontier_frame.a3=ox;vx_bind_frontier_frame.a4=oy;vx_bind_frontier_run();
            if(vx_bind_frontier_frame.result.v0!=step || vx_bind_frontier_frame.result.v1!=(step<20) ||
               vx_bind_frontier_frame.result.v2!=(uint16_t)((uint32_t)ox+(uint16_t)(dir==1 ? a : -a)) ||
               vx_bind_frontier_frame.result.v3!=(uint16_t)((uint32_t)oy+(uint16_t)(dir==1 ? b : -b)))return 1;
        }
    }return 0;
}
'''
if args.output:args.output.mkdir(parents=True,exist_ok=True)
workspace=contextlib.nullcontext(args.output.resolve()) if args.output else tempfile.TemporaryDirectory(prefix='vexel-value-frontiers-')
with workspace as temporary:
    root=Path(temporary);source=root/'frontiers.vx';source.write_text(SOURCE)
    for frame,choice in ((frame,choice) for frame in (0,64) for choice in (0,8)):
        tag=f'{frame}-{choice}';generated=root/f'generated-{tag}'
        report=json.loads(support.run([solver,source,'--program','--prefer',f'value_frame_bytes={frame}',
                                      '--prefer',f'value_choice_branches={choice}','--emit-c',generated]).stdout)
        header=(generated/'vx_bind_frontier.h').read_text()
        assert header.count('VX_VALUE_LT8(')==1, header
        if choice:assert report['value_bindings']['bindings'][0]['codegen']['predicate_branches']==1,report
        client=root/f'host-{tag}.c';binary=root/f'host-{tag}'
        client.write_text(CLIENT+'\nint main(void){return check_frontier()!=0;}\n')
        support.run(['cc','-std=c11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',
                     '-I'+str(generated),'-I'+str(ROOT/'backends/msx-neo8/runtime'),client,
                     generated/'vx_binding_frames.c','-o',binary]);support.run([binary])
        if args.msx:
            project=root/f'project-{tag}';support.backend.package(generated,project,source,solver)
            (project/'user/frames.c').write_text('#include "vx_binding_frames.c"\n')
            (project/'user/main.c').write_text(CLIENT+'''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check_frontier();__asm halt __endasm;}
''')
            build=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=400000000',
                              'SDCCFLAGS=-mz80 --std-sdcc11 --sdcccall 0 --max-allocs-per-node 2000'])
            (root/f'build-{tag}.log').write_text(build.stdout+build.stderr)
            result=json.loads((project/'build/execution.json').read_text())
            assert result['observation']['last_result']==result['observation']['fault']==0,result
print('Value frontiers: byte-pair wrap, both branch directions, word displacement and fixed/automatic storage passed'+
      (' on host and Z80' if args.msx else ' (UBSan)'))
