#!/usr/bin/env python3
"""Exact finite constants, guarded values and shared tables at their ROM ceiling."""
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
[[c_binding(reuse,full,1,exclusive)]] &^reuse(x:#u8,y:#u8){(x*7,y*7)}
[[c_binding(step,full,1,exclusive)]] &^step(x:#u8){(#i16)(x*7==0 ? -300 : 700)}
[[c_binding(constant,full,1,exclusive)]] &^constant(x:#u8,y:#u8){z:#u8=x+y;(z*z-z)&1}
'''
CLIENT='''
#include "vx_bind_reuse.h"
#include "vx_bind_step.h"
#include "vx_bind_constant.h"
uint32_t check_storage(void) {
    uint16_t x,y,k;static const uint8_t samples[]={0,1,2,63,127,128,254,255};
    VX_BIND_reuse_LOCAL;VX_BIND_step_LOCAL;VX_BIND_constant_LOCAL;
    for(x=0;x<256;++x) {
        vx_bind_step_frame.a0=(uint8_t)x;vx_bind_step_run();
        if(vx_bind_step_frame.result.v0!=(x==0 ? -300 : 700))return 1;
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
            vx_bind_reuse_frame.a0=(uint8_t)x;vx_bind_reuse_frame.a1=(uint8_t)y;vx_bind_reuse_run();
            if(vx_bind_reuse_frame.result.v0!=(uint8_t)((x<<3)-x) ||
               vx_bind_reuse_frame.result.v1!=(uint8_t)((y<<3)-y))return 2;
            vx_bind_constant_frame.a0=(uint8_t)x;vx_bind_constant_frame.a1=(uint8_t)y;vx_bind_constant_run();
            /* Consecutive integers have an even product, including modulo256. */
            if(vx_bind_constant_frame.result.v0!=0)return 3;
        }
    }return 0;
}
'''
if args.output:args.output.mkdir(parents=True,exist_ok=True)
workspace=contextlib.nullcontext(args.output.resolve()) if args.output else tempfile.TemporaryDirectory(prefix='vexel-finite-storage-')
with workspace as temporary:
    root=Path(temporary);source=root/'storage.vx';source.write_text(SOURCE)
    for frame in (0,64):
        generated=root/f'generated-{frame}'
        report=json.loads(support.run([solver,source,'--program','--prefer',f'value_frame_bytes={frame}',
                                      '--prefer','value_table_bytes=256','--emit-c',generated]).stdout)
        plans={b['name']:b['codegen'] for b in report['value_bindings']['bindings']}
        assert plans['reuse']['finite_tables']==1 and plans['reuse']['finite_lookups']==2,plans
        assert plans['reuse']['finite_table_bytes']==256,plans
        assert plans['step']['finite_guards']==1,plans
        assert plans['constant']['finite_constants']==1,plans
        for name in ('step','constant'):
            assert plans[name]['finite_table_bytes']==plans[name]['finite_tables']==plans[name]['finite_lookups']==0,plans
            assert 'vx_lut_' not in (generated/f'vx_bind_{name}.h').read_text()
        header=(generated/'vx_bind_constant.h').read_text()
        assert all(token not in header for token in ('.a0','.a1','->a0','->a1')),header
        client=root/f'host-{frame}.c';binary=root/f'host-{frame}'
        client.write_text(CLIENT+'\nint main(void){return check_storage()!=0;}\n')
        support.run(['cc','-std=c11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',
                     '-I'+str(generated),'-I'+str(ROOT/'backends/msx-neo8/runtime'),client,
                     generated/'vx_binding_frames.c','-o',binary]);support.run([binary])
        if args.msx:
            project=root/f'project-{frame}';support.backend.package(generated,project,source,solver)
            (project/'user/frames.c').write_text('#include "vx_binding_frames.c"\n')
            (project/'user/main.c').write_text(CLIENT+'''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check_storage();__asm halt __endasm;}
''')
            build=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=400000000',
                              'SDCCFLAGS=-mz80 --std-sdcc11 --sdcccall 0 --max-allocs-per-node 2000'])
            (root/f'build-{frame}.log').write_text(build.stdout+build.stderr)
            result=json.loads((project/'build/execution.json').read_text())
            assert result['observation']['last_result']==result['observation']['fault']==0,result
print('Finite realization storage: shared table at its byte ceiling, guarded immediates and constant producer elimination passed'+
      (' on host and Z80' if args.msx else ' (UBSan)'))
