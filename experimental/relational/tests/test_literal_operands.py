#!/usr/bin/env python3
"""Known literals preserve wrapping conversions and effectful region calls."""
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
[[c_binding(immediates,full,1,exclusive)]]
&^immediates(x:#u16,s:#i8,p:#u8){
 d:#u16=p!=0 ? (#u16)0 : (#u16)32;
 wide:#u32=((#u32)x<<16)|(#u32)x;
 (x+d,d+x,x-d,(#i16)s+(p!=0 ? (#i16)0 : (#i16)-128),
  (#u8)(x+(#u16)0),x*(p!=0 ? (#u16)1 : (#u16)3),
  wide+(p!=0 ? (#u32)0 : (#u32)65536),
  (#i16)s+(#i16)((#i8)255),
  x*(p!=0 ? (#u16)0 : (#u16)3),(#u8)x*(p!=0 ? (#u8)0 : (#u8)3),
  wide*(p!=0 ? (#u32)0 : (#u32)1))
}
&!accept(x:#u16)->#u16;
[[c_binding(immediate_region,region,1,exclusive)]]
&^immediate_region(x:#u16,p:#u8){
 d:#u16=p!=0 ? (#u16)0 : (#u16)32;
 y=accept(x+d);y*(p!=0 ? (#u16)0 : (#u16)1)
}
'''
CLIENT = r'''
#include "vx_bind_immediates.h"
#include "vx_bind_immediate_region.h"
static uint16_t calls;
VX_BIND_immediate_region_BOUNDARY_accept {
 ++calls;
 VX_BIND_immediate_region_DATA_accept.r0=(uint16_t)(VX_BIND_immediate_region_DATA_accept.a0+1);
}
static uint16_t check_one(uint16_t x,uint8_t p) {
 uint16_t d=p?0:32;
 int16_t s=(uint8_t)x<128 ? (int16_t)(uint8_t)x : (int16_t)(uint8_t)x-256;
 uint32_t wide=((uint32_t)x<<16)+(uint32_t)x;
 VX_BIND_immediates_LOCAL;VX_BIND_immediate_region_LOCAL;
 vx_bind_immediates_frame.a0=x;vx_bind_immediates_frame.a1=(int8_t)s;vx_bind_immediates_frame.a2=p;
 vx_bind_immediates_run();
 if(vx_bind_immediates_frame.result.v0!=(uint16_t)(x+d) ||
    vx_bind_immediates_frame.result.v1!=(uint16_t)(x+d) ||
    vx_bind_immediates_frame.result.v2!=(uint16_t)(x-d) ||
    vx_bind_immediates_frame.result.v3!=(int16_t)(s-(p?0:128)) ||
    vx_bind_immediates_frame.result.v4!=(uint8_t)x ||
    vx_bind_immediates_frame.result.v5!=(uint16_t)(p?x:(uint16_t)(x+x+x)) ||
    vx_bind_immediates_frame.result.v6!=(uint32_t)(wide+(p?0UL:65536UL)) ||
    vx_bind_immediates_frame.result.v7!=(int16_t)(s-1) ||
    vx_bind_immediates_frame.result.v8!=(uint16_t)(p?0:(uint16_t)(x+x+x)) ||
    vx_bind_immediates_frame.result.v9!=(uint8_t)(p?0:(uint16_t)(x+x+x)) ||
    vx_bind_immediates_frame.result.v10!=(uint32_t)(p?0UL:wide)) return __LINE__;
 calls=0;vx_bind_immediate_region_frame.a0=x;vx_bind_immediate_region_frame.a1=p;
 vx_bind_immediate_region_run();
 if(calls!=1 || vx_bind_immediate_region_frame.result.v0!=(uint16_t)(p?0:x+d+1))return __LINE__;
 return 0;
}
uint32_t check_facts(void) {
 uint32_t i;uint16_t result;
 const uint16_t edges[]={0,1,127,128,255,256,32767,32768,65535};
 for(i=0;i<256;++i) {
  result=check_one((uint16_t)((uint16_t)i|((uint16_t)i<<8)),0);if(result)return result;
  result=check_one((uint16_t)((uint16_t)i|((uint16_t)i<<8)),((uint8_t)i?(uint8_t)i:(uint8_t)255));if(result)return result;
 }
 for(i=0;i<sizeof(edges)/sizeof(edges[0]);++i){result=check_one(edges[i],0);if(result)return result;result=check_one(edges[i],255);if(result)return result;}
#ifndef __SDCC
 for(i=0;i<65536;++i){result=check_one((uint16_t)i,0);if(result)return result;result=check_one((uint16_t)i,1);if(result)return result;}
#endif
 return 0;
}
'''
if args.output:
    args.output.mkdir(parents=True, exist_ok=True)
workspace = contextlib.nullcontext(args.output.resolve()) if args.output else tempfile.TemporaryDirectory(prefix='vexel-literal-operands-')
with workspace as temporary:
    root = Path(temporary)
    source = root/'facts.vx'
    source.write_text(SOURCE)
    for frame,choice in ((frame,choice) for frame in (0,128) for choice in (0,8)):
        tag = f'{frame}-{choice}'
        generated = root/f'generated-{tag}'
        report = json.loads(support.run([solver,source,'--program','--prefer',f'value_frame_bytes={frame}',
                                         '--prefer',f'value_choice_branches={choice}','--prefer','value_table_bytes=0','--emit-c',generated]).stdout)
        assert report['status']=='resolved', report
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
print('Literal operands: signed/unsigned widths, every word input on host, branch facts, wrapping casts, zero products and effectful regions passed'+(' on host and Z80' if args.msx else ' (UBSan)'))
