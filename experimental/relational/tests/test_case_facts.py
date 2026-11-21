#!/usr/bin/env python3
"""Case interfaces carry invariant results and established predicates with exact input dependencies."""
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

[[c_binding(complete,full,1,exclusive)]]
[[c_binding(inactive,case0,1,exclusive)]]
[[c_binding(active,case1,1,exclusive)]]
&^splice(flag:#u8,left:#u16,right:#u16,stamp:#u8){
    p=flag!=0;(p ? left+1 : right-1,stamp,p,p ? (#u8)7 : (#u8)9)}
[[c_binding(geo_inactive,case0,1,exclusive)]]
[[c_binding(geo_active,case1,1,exclusive)]]
&^geometry(option:#u8,vertical:#u8){
    (vertical!=0 || option==0 ? (#u8)5 : (#u8)35,2+(vertical!=0 ? option : (#u8)3)*7)}
&^intersection(option:#u8,vertical:#u8){
    (vertical!=0 && option==0 ? (#u8)5 : (#u8)35,2+(vertical!=0 ? option : (#u8)3)*7)}
&^negated(option:#u8,vertical:#u8){
    (!(vertical!=0 || option==0) ? (#u8)5 : (#u8)35,2+(vertical!=0 ? option : (#u8)3)*7)}
'''
CLIENT = r'''

#include "vx_bind_complete.h"
#include "vx_bind_inactive.h"
#include "vx_bind_active.h"
#include "vx_value_splice_cases.h"
#include "vx_bind_geo_inactive.h"
#include "vx_bind_geo_active.h"
#include "vx_value_intersection_cases.h"
#include "vx_value_negated_cases.h"
static uint32_t check_geometry(void) {
    uint16_t option,k;uint8_t vertical,x,y;
    static const uint8_t directions[]={0,1,255};
    vx_intersection_result intersection;vx_negated_result negated;
    VX_BIND_geo_inactive_LOCAL;VX_BIND_geo_active_LOCAL;
    for(option=0;option<256;++option)for(k=0;k<3;++k) {
        vertical=directions[k];x=vertical || option==0 ? 5 : 35;
        y=(uint8_t)(2+(vertical ? option : 3)*7);
        if(vertical || option==0) {
            vx_bind_geo_active_frame.a0=(uint8_t)option;vx_bind_geo_active_frame.a1=vertical;vx_bind_geo_active_run();
            if(vx_bind_geo_active_frame.result.v0!=x || vx_bind_geo_active_frame.result.v1!=y)return 100;
        } else {
            vx_bind_geo_inactive_run();
            if(vx_bind_geo_inactive_frame.result.v0!=x || vx_bind_geo_inactive_frame.result.v1!=y)return 101;
        }
        if(vx_intersection_branch((uint8_t)option,vertical))vx_intersection_case1_value(&intersection);
        else vx_intersection_case0_value(&intersection,(uint8_t)option,vertical);
        if(intersection.v0!=(vertical && option==0 ? 5 : 35) || intersection.v1!=y)return 102;
        if(vx_negated_branch((uint8_t)option,vertical))vx_negated_case1_value(&negated);
        else vx_negated_case0_value(&negated,(uint8_t)option,vertical);
        if(negated.v0!=(!(vertical || option==0) ? 5 : 35) || negated.v1!=y)return 103;
    }return 0;
}
#define CHECK(name) do { \
    if(vx_bind_##name##_frame.result.v0!=expected || \
       vx_bind_##name##_frame.result.v1!=stamp || \
       vx_bind_##name##_frame.result.v2!=(flag!=0) || \
       vx_bind_##name##_frame.result.v3!=(flag ? 7 : 9))return __LINE__; \
} while(0)
uint32_t check_cases(void) {
    uint16_t flag,k,left,right,expected;uint8_t stamp;
    static const uint16_t words[]={0,1,255,256,32767,32768,65534,65535};
    VX_BIND_complete_LOCAL;VX_BIND_inactive_LOCAL;VX_BIND_active_LOCAL;
    for(flag=0;flag<256;++flag)for(k=0;k<sizeof(words)/sizeof(words[0]);++k) {
        left=words[k];right=(uint16_t)(65535U-left);stamp=(uint8_t)(flag+k);
        expected=flag ? (uint16_t)(left+1U) : (uint16_t)(right-1U);
#if USE_CASES
        if(vx_splice_branch((uint8_t)flag)) {
            vx_bind_active_frame.a1=left;vx_bind_active_frame.a3=stamp;vx_bind_active_run();CHECK(active);
        } else {
            vx_bind_inactive_frame.a2=right;vx_bind_inactive_frame.a3=stamp;vx_bind_inactive_run();CHECK(inactive);
        }
#else
        vx_bind_complete_frame.a0=(uint8_t)flag;vx_bind_complete_frame.a1=left;
        vx_bind_complete_frame.a2=right;vx_bind_complete_frame.a3=stamp;vx_bind_complete_run();CHECK(complete);
#endif
#ifndef __SDCC
        {vx_splice_result value;
         if(flag)vx_splice_case1_value(&value,left,stamp);else vx_splice_case0_value(&value,right,stamp);
         if(value.v0!=expected || value.v1!=stamp || value.v2!=(flag!=0) || value.v3!=(flag?7:9))return 99;}
#endif
    }return check_geometry();
}
'''
if args.output:
    args.output.mkdir(parents=True,exist_ok=True)
workspace=contextlib.nullcontext(args.output.resolve()) if args.output else tempfile.TemporaryDirectory(prefix='vexel-case-facts-')
with workspace as temporary:
    root=Path(temporary);source=root/'cases.vx';source.write_text(SOURCE)
    for frame in (0,96):
        generated=root/f'generated-{frame}'
        report=json.loads(support.run([solver,source,'--program','--prefer',f'value_frame_bytes={frame}','--emit-c',generated]).stdout)
        source_plan=next(s for s in report['sources'] if s['name']=='splice')
        projection=source_plan['value_projection']
        assert projection['branch_inputs']==[0] and projection['case_inputs']==[[2,3],[1,3]], projection
        projections={s['name']:s['value_projection'] for s in report['sources']}
        assert projections['geometry']['case_inputs']==[[],[0,1]], projections['geometry']
        for name in ('intersection','negated'):
            assert projections[name]['case_inputs']==[[0,1],[]], projections[name]
        bindings={b['name']:b for b in report['value_bindings']['bindings']}
        assert bindings['inactive']['inputs']==[2,3] and bindings['active']['inputs']==[1,3], bindings
        assert bindings['geo_inactive']['inputs']==[] and bindings['geo_active']['inputs']==[0,1], bindings
        for mode in (0,1):
            tag=f'{frame}-{mode}';host=root/f'host-{tag}.c';binary=root/f'host-{tag}'
            host.write_text(f'#define USE_CASES {mode}\n'+CLIENT+'int main(void){return check_cases()!=0;}\n')
            support.run(['cc','-std=c11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',
                         '-I'+str(generated),'-I'+str(ROOT/'backends/msx-neo8/runtime'),host,
                         generated/'vx_binding_frames.c','-o',binary]);support.run([binary])
            if args.msx:
                project=root/f'project-{tag}';support.backend.package(generated,project,source,solver)
                (project/'user/frames.c').write_text('#include "vx_binding_frames.c"\n')
                (project/'user/main.c').write_text(f'#define USE_CASES {mode}\n'+CLIENT+'''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check_cases();__asm halt __endasm;}
''')
                built=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=100000000',
                                  'SDCCFLAGS=-mz80 --std-sdcc11 --sdcccall 0 --max-allocs-per-node 2000'])
                (root/f'build-{tag}.log').write_text(built.stdout+built.stderr)
                result=json.loads((project/'build/execution.json').read_text())
                assert result['observation']['last_result']==result['observation']['fault']==0,result
print('Case facts: invariant outputs, established predicates, minimal input publication and wrapping values agree'+(' on host and Z80' if args.msx else ' (UBSan)'))
