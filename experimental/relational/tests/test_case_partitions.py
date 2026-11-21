#!/usr/bin/env python3
"""Independent result guards retain coherent inputs and values tested for nonzero."""
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
&^partition(flag:#u8,other:#u8,word:#u16){
    p=flag!=0;q=other>127;
    (p ? word+1 : word-1,q ? (#u8)7 : (#u8)9,flag,other,(#u8)p)}
&^nested(flag:#u8,other:#u8){
    p=flag!=0;value=other>127 ? flag : (#u8)0;
    (p ? (#u8)5 : (#u8)6,value,flag)}
'''
CLIENT = r'''
#include "vx_bind_complete.h"
#include "vx_bind_inactive.h"
#include "vx_bind_active.h"
#include "vx_value_partition_cases.h"
#include "vx_value_nested_cases.h"
#define CHECK(name) do { \
    if(vx_bind_##name##_frame.result.v0!=expected || \
       vx_bind_##name##_frame.result.v1!=(other>127 ? 7 : 9) || \
       vx_bind_##name##_frame.result.v2!=flag || \
       vx_bind_##name##_frame.result.v3!=other || \
       vx_bind_##name##_frame.result.v4!=(flag!=0)) return __LINE__; \
} while(0)
uint32_t check_cases(void) {
    uint16_t flag,other,word,expected;
    vx_nested_result nested;
    VX_BIND_complete_LOCAL;VX_BIND_inactive_LOCAL;VX_BIND_active_LOCAL;
    for(flag=0;flag<256;++flag)for(other=0;other<256;++other) {
#ifdef __SDCC
        /* Keep target duration bounded; the host exhausts both byte domains. */
        if(flag>1 && flag<254)continue;
#endif
        word=(uint16_t)(flag*257U+other);expected=(uint16_t)(word+(flag ? 1U : 65535U));
#if USE_CASES
        if(vx_partition_branch((uint8_t)flag)) {
            vx_bind_active_frame.a0=(uint8_t)flag;vx_bind_active_frame.a1=(uint8_t)other;
            vx_bind_active_frame.a2=word;vx_bind_active_run();CHECK(active);
        } else {
            vx_bind_inactive_frame.a1=(uint8_t)other;vx_bind_inactive_frame.a2=word;
            vx_bind_inactive_run();CHECK(inactive);
        }
#else
        vx_bind_complete_frame.a0=(uint8_t)flag;vx_bind_complete_frame.a1=(uint8_t)other;
        vx_bind_complete_frame.a2=word;vx_bind_complete_run();CHECK(complete);
#endif
        if(vx_nested_branch((uint8_t)flag))vx_nested_case1_value(&nested,(uint8_t)flag,(uint8_t)other);
        else vx_nested_case0_value(&nested);
        if(nested.v0!=(flag ? 5 : 6) || nested.v1!=(other>127 ? flag : 0) || nested.v2!=flag)return 99;
    }return 0;
}
'''
if args.output:
    args.output.mkdir(parents=True,exist_ok=True)
workspace=contextlib.nullcontext(args.output.resolve()) if args.output else tempfile.TemporaryDirectory(prefix='vexel-case-partitions-')
with workspace as temporary:
    root=Path(temporary);source=root/'cases.vx';source.write_text(SOURCE)
    for frame in (0,96):
        generated=root/f'generated-{frame}'
        report=json.loads(support.run([solver,source,'--program','--prefer',f'value_frame_bytes={frame}','--emit-c',generated]).stdout)
        projections={s['name']:s['value_projection'] for s in report['sources']}
        assert projections['partition']['branch_inputs']==[0],projections['partition']
        assert projections['partition']['case_inputs']==[[1,2],[0,1,2]],projections['partition']
        assert projections['nested']['case_inputs']==[[],[0,1]],projections['nested']
        bindings={b['name']:b for b in report['value_bindings']['bindings']}
        assert bindings['inactive']['inputs']==[1,2] and bindings['active']['inputs']==[0,1,2],bindings
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
print('Case partitions: independent guards, nonzero numeric values, eliminated inputs and wrapping results agree'+(' on host and Z80' if args.msx else ' (UBSan)'))
