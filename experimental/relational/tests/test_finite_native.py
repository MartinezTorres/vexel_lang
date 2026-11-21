#!/usr/bin/env python3
"""Exact finite native rules rejoin branch specialization and modular arithmetic."""
import argparse
import contextlib
import importlib.util
import json
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('support', ROOT/'examples/relational_source_msx.py')
support = importlib.util.module_from_spec(spec)
spec.loader.exec_module(support)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('solver', type=Path)
parser.add_argument('--msx', action='store_true')
parser.add_argument('--output', type=Path)
args = parser.parse_args()
solver = args.solver.resolve()
SOURCE = '''
[[c_binding(exception,full,1,exclusive)]]
&^exception(x:#u8,y:#u8){z:#u8=x+y;((z&15)*3)==9 ? (#i16)-300 : (#i16)700}
[[c_binding(window,full,1,exclusive)]]
&^window(x:#u8,y:#u8){z:#u8=x+y;u:#u8=(z&15)*3;u<9 ? (#i16)9 : (u<27 ? (#i16)-7 : (#i16)9)}
[[c_binding(pair,full,1,exclusive)]]
&^pair(x:#u8,a:#u16,b:#u16){u:#u8=(x&15)*3;v:#u8=(x&15)*5;
    left:#i16=u<12 ? (#i16)-7 : (#i16)9;right:#i16=v<20 ? (#i16)3 : (#i16)-5;
    (a+(#u16)left,b+(#u16)right)}
[[c_binding(ascending,full,1,exclusive)]]
&^ascending(x:#u8){z:#u8=(x^85)^85;z<5 ? z+1 : (#u8)0}
[[c_binding(descending,full,1,exclusive)]]
&^descending(x:#u8){z:#u8=(x^85)^85;z<5 ? (#u8)255-z : (#u8)7}
[[c_binding(wrapped,full,1,exclusive)]]
&^wrapped(x:#u8){z:#u8=(x^85)^85;z<5 ? (#u16)z+(#u16)65534 : (#u16)7}
[[c_binding(sign_boundary,full,1,exclusive)]]
&^sign_boundary(x:#u8){z:#u8=(x^85)^85;z<6 ? (#i16)((#i8)(z+126)) : (#i16)9}
'''
CLIENT = '''
#include "vx_bind_exception.h"
#include "vx_bind_window.h"
#include "vx_bind_pair.h"
#include "vx_bind_ascending.h"
#include "vx_bind_descending.h"
#include "vx_bind_wrapped.h"
#include "vx_bind_sign_boundary.h"
uint32_t check_native(void) {
    uint16_t x,y,k,z,a,b;static const uint8_t samples[]={0,1,3,8,127,128,254,255};
    VX_BIND_exception_LOCAL;VX_BIND_window_LOCAL;VX_BIND_pair_LOCAL;
    VX_BIND_ascending_LOCAL;VX_BIND_descending_LOCAL;VX_BIND_wrapped_LOCAL;VX_BIND_sign_boundary_LOCAL;
    for(x=0;x<256;++x) {
        vx_bind_ascending_frame.a0=(uint8_t)x;vx_bind_ascending_run();
        if(vx_bind_ascending_frame.result.v0!=(x<5 ? x+1 : 0))return 4;
        vx_bind_descending_frame.a0=(uint8_t)x;vx_bind_descending_run();
        if(vx_bind_descending_frame.result.v0!=(x<5 ? 255-x : 7))return 5;
        vx_bind_wrapped_frame.a0=(uint8_t)x;vx_bind_wrapped_run();
        if(vx_bind_wrapped_frame.result.v0!=(x<5 ? (uint16_t)(x+65534U) : 7))return 6;
        vx_bind_sign_boundary_frame.a0=(uint8_t)x;vx_bind_sign_boundary_run();
        if(vx_bind_sign_boundary_frame.result.v0!=(x<6 ? (x<2 ? (int16_t)x+126 : (int16_t)x-130) : 9))return 7;
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
            z=((x+y)&255)&15;
            vx_bind_exception_frame.a0=(uint8_t)x;vx_bind_exception_frame.a1=(uint8_t)y;vx_bind_exception_run();
            if(vx_bind_exception_frame.result.v0!=(z==3 ? -300 : 700))return 1;
            vx_bind_window_frame.a0=(uint8_t)x;vx_bind_window_frame.a1=(uint8_t)y;vx_bind_window_run();
            if(vx_bind_window_frame.result.v0!=(z>=3 && z<9 ? -7 : 9))return 2;
            a=x*257U;b=y*257U;
            vx_bind_pair_frame.a0=(uint8_t)x;vx_bind_pair_frame.a1=a;vx_bind_pair_frame.a2=b;vx_bind_pair_run();
            if(vx_bind_pair_frame.result.v0!=(uint16_t)(a+((x&15)<4 ? 65529U : 9U)) ||
               vx_bind_pair_frame.result.v1!=(uint16_t)(b+((x&15)<4 ? 3U : 65531U)))return 3;
        }
    }return 0;
}
'''
if args.output:
    args.output.mkdir(parents=True, exist_ok=True)
workspace = contextlib.nullcontext(args.output.resolve()) if args.output else tempfile.TemporaryDirectory(prefix='vexel-finite-native-')
with workspace as temporary:
    root = Path(temporary)
    source = root/'native.vx'
    source.write_text(SOURCE)
    for frame,choice in ((frame,choice) for frame in (0,64) for choice in (0,8)):
        tag = f'{frame}-{choice}'
        generated = root/f'generated-{tag}'
        report = json.loads(support.run([solver,source,'--program','--prefer',f'value_frame_bytes={frame}',
                                         '--prefer',f'value_choice_branches={choice}','--emit-c',generated]).stdout)
        for plan in report['value_bindings']['bindings']:
            code = plan['codegen']
            assert code['finite_tables']==code['finite_table_bytes']==0, plan
            assert code['finite_guards']>0, plan
            # sign_boundary may lower at byte width before widening; the
            # independent oracle checks that its intervening wrap survives.
            if plan['name'] in ('ascending','descending','wrapped','sign_boundary'):
                assert code['finite_arithmetic']>0, plan
            if plan['name']=='pair':
                assert (code['predicate_branches']>0)==(choice>0), plan
        host = root/f'host-{tag}.c'
        binary = root/f'host-{tag}'
        host.write_text(CLIENT+'int main(void){return check_native()!=0;}\n')
        support.run(['cc','-std=c11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',
                     '-I'+str(generated),'-I'+str(ROOT/'backends/msx-neo8/runtime'),host,
                     generated/'vx_binding_frames.c','-o',binary])
        support.run([binary])
        if args.msx:
            project = root/f'project-{tag}'
            support.backend.package(generated,project,source,solver)
            (project/'user/frames.c').write_text('#include "vx_binding_frames.c"\n')
            (project/'user/main.c').write_text(CLIENT+'''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check_native();__asm halt __endasm;}
''')
            build = support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=400000000',
                                 'SDCCFLAGS=-mz80 --std-sdcc11 --sdcccall 0 --opt-code-speed --max-allocs-per-node 2000'])
            (root/f'build-{tag}.log').write_text(build.stdout+build.stderr)
            result = json.loads((project/'build/execution.json').read_text())
            assert result['observation']['last_result']==result['observation']['fault']==0, result
print('Finite native rules: periodic equality, interior intervals, computed cuts and shared branch specialization passed'+
      (' on host and Z80' if args.msx else ' (UBSan)'))
