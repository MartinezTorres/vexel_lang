#!/usr/bin/env python3
"""Established branch bounds specialize neighboring pure outputs without escaping the branch."""
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
parser.add_argument('--baseline', action='store_true')
args = parser.parse_args()
solver = args.solver.resolve()
SOURCE = '''

[[c_binding(ordered,full,1,exclusive)]]
&^ordered(x:#u8,a:#u16,b:#u16){
    (x<100 ? a+1 : a+2,x<100 ? b+3 : b+4,
     (#u8)(x<200),(#u8)(x>=100),(#u8)(x!=255),(#u8)(x==0),
     (#u8)((#u8)(x+200)<100))}
[[c_binding(reversed,full,1,exclusive)]]
&^reversed(x:#u8,a:#u16,b:#u16){
    (100>x ? a+1 : a+2,100>x ? b+3 : b+4,
     (#u8)(200>x),(#u8)(100<=x),(#u8)(255!=x),(#u8)(0==x),
     (#u8)((#u8)(x+200)<100))}
[[c_binding(signed_order,full,1,exclusive)]]
&^signed_order(x:#i8,a:#u16,b:#u16){
    (x< -10 ? a+1 : a+2,x< -10 ? b+3 : b+4,
     (#u8)(x<0),(#u8)(x>= -50),(#u8)(x!=127),(#u8)(x== -128),
     (#u8)((#u8)x<100))}
'''
CLIENT = r'''

#include "vx_bind_ordered.h"
#include "vx_bind_reversed.h"
#include "vx_bind_signed_order.h"
#define CHECK(name, input, pred, c0, c1, c2, c3, c4) do { \
    vx_bind_##name##_frame.a0=(input); \
    vx_bind_##name##_frame.a1=a;vx_bind_##name##_frame.a2=b; \
    vx_bind_##name##_run(); \
    if(vx_bind_##name##_frame.result.v0!=(uint16_t)(a+((pred)?1:2)) || \
       vx_bind_##name##_frame.result.v1!=(uint16_t)(b+((pred)?3:4)) || \
       vx_bind_##name##_frame.result.v2!=(c0) || \
       vx_bind_##name##_frame.result.v3!=(c1) || \
       vx_bind_##name##_frame.result.v4!=(c2) || \
       vx_bind_##name##_frame.result.v5!=(c3) || \
       vx_bind_##name##_frame.result.v6!=(c4)) return __LINE__; \
} while(0)
uint32_t check_facts(void) {
    uint16_t x,a,b;int16_t s;
    VX_BIND_ordered_LOCAL;VX_BIND_reversed_LOCAL;VX_BIND_signed_order_LOCAL;
    for(x=0;x<256;++x) {
        a=x*257U;b=(uint16_t)(65535U-a);s=x<128 ? (int16_t)x : (int16_t)x-256;
        CHECK(ordered,(uint8_t)x,x<100,x<200,x>=100,x!=255,x==0,(uint8_t)(x+200)<100);
        CHECK(reversed,(uint8_t)x,x<100,x<200,x>=100,x!=255,x==0,(uint8_t)(x+200)<100);
        CHECK(signed_order,(int8_t)s,s< -10,s<0,s>= -50,s!=127,s== -128,x<100);
    }return 0;
}
'''
if args.output:
    args.output.mkdir(parents=True, exist_ok=True)
workspace = contextlib.nullcontext(args.output.resolve()) if args.output else tempfile.TemporaryDirectory(prefix='vexel-finite-native-')
with workspace as temporary:
    root = Path(temporary)
    source = root/'facts.vx'
    source.write_text(SOURCE)
    for frame,choice in ((frame,choice) for frame in (0,64) for choice in (0,8)):
        tag = f'{frame}-{choice}'
        generated = root/f'generated-{tag}'
        report = json.loads(support.run([solver,source,'--program','--prefer',f'value_frame_bytes={frame}',
                                         '--prefer',f'value_choice_branches={choice}','--prefer','value_table_bytes=0','--emit-c',generated]).stdout)
        assert report['status']=='resolved', report
        if not args.baseline:
            text=(generated/'vx_value_ordered.h').read_text()
            # The looser threshold is only required on the false arm of
            # x<100. It must not be emitted eagerly before that branch.
            branch=text.index('if (')
            assert text.index('vxo->v2 =')>branch, text
            assert 'vxo->v2 = ((uint8_t)(1));' in text[branch:text.index('} else {',branch)], text
        host = root/f'host-{tag}.c'
        binary = root/f'host-{tag}'
        host.write_text(CLIENT+'int main(void){return check_facts()!=0;}\n')
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
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check_facts();__asm halt __endasm;}
''')
            build = support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=400000000',
                                 'SDCCFLAGS=-mz80 --std-sdcc11 --sdcccall 0 --opt-code-speed --max-allocs-per-node 2000'])
            (root/f'build-{tag}.log').write_text(build.stdout+build.stderr)
            result = json.loads((project/'build/execution.json').read_text())
            assert result['observation']['last_result']==result['observation']['fault']==0, result
print('Branch facts: all byte inputs, reversed and signed comparisons, wrapping casts, automatic/fixed storage passed'+(' on host and Z80' if args.msx else ' (UBSan)'))
