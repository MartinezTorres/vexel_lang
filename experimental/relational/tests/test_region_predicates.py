#!/usr/bin/env python3
"""Pure predicate kernels borrow the current region fields without caching them."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('solver', type=Path)
parser.add_argument('--msx', action='store_true')
args = parser.parse_args()
solver = args.solver.resolve()


def run(command, **kwargs):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=240, **kwargs)
    assert result.returncode == 0, f'{command}\n{result.stdout}\n{result.stderr}'
    return result


SOURCE = '''
#Pair(a:#u8,b:#u8);
&!read()->#Pair;
&!advance();
&!publish(code:#u8);
[[c_binding(scan,region,1,exclusive)]] &^scan(){
    i:#u8=0;
    (i<2)!@{
        p=read();
        (#u8)(p.a-p.b+3)<30 ? {publish(1);} : {publish(2);};
        p.a<p.b ? {publish(3);} : {publish(4);};
        (#u8)(p.a+p.b)!=0 ? {publish(5);} : {publish(6);};
        (#i8)(p.a-p.b)<0 ? {publish(7);} : {publish(8);};
        ((#i8)p.a+(#i8)p.b)<0 ? {publish(9);} : {publish(10);};
        (#u8)(p.a-p.b)>=24 ? {publish(18);} : {publish(19);};
        // A word carry must never turn into a byte predicate.
        ((#u16)p.a+(#u16)p.b)>255 ? {publish(11);} : {publish(12);};
        // Only the taken arm evaluates this predicate.
        p.a!=0 ? {
            (#u8)((p.a-p.b)^128)<64 ? {publish(13);} : {publish(14);};
        } : {publish(15);};
        advance();i+=1;
    };
    (#u8)0
}
[[c_binding(wide,region,1,exclusive)]] &^wide(a:#u16,b:#u16){
    (a+b)!=0 ? {publish(16);} : {publish(17);};(#u8)0
}
'''
CLIENT = r'''
#include <stdint.h>
#include "vx_bind_scan.h"
#include "vx_bind_wide.h"
static uint8_t x,y,used,trace[16],expected[16];
static void emit(uint8_t code) {if(used<16)trace[used]=code;++used;}
static void advance(void) {x+=17;y^=91;}
VX_BIND_scan_BOUNDARY_read {VX_BIND_scan_DATA_read.r0=x;VX_BIND_scan_DATA_read.r1=y;}
VX_BIND_scan_BOUNDARY_advance {advance();}
VX_BIND_scan_BOUNDARY_publish {emit(VX_BIND_scan_DATA_publish.a0);}
VX_BIND_wide_BOUNDARY_publish {emit(VX_BIND_wide_DATA_publish.a0);}
static void reference(void) {
    uint8_t i;
    for(i=0;i<2;++i) {
        volatile uint8_t a=x,b=y;
        emit((uint8_t)(a-b+3)<30 ? 1 : 2);
        emit(a<b ? 3 : 4);
        emit((uint8_t)(a+b)!=0 ? 5 : 6);
        emit(((uint8_t)(a-b)&128)!=0 ? 7 : 8);
        emit(((uint8_t)(a+b)&128)!=0 ? 9 : 10);
        emit((uint8_t)(a-b)>=24 ? 18 : 19);
        emit((uint16_t)a+(uint16_t)b>255 ? 11 : 12);
        emit(a ? ((uint8_t)((uint8_t)(a-b)^128)<64 ? 13 : 14) : 15);
        advance();
    }
}
static uint32_t check(void) {
    uint16_t a,b;uint8_t i,old_x,old_y;
    static const uint16_t words[]={0,1,255,256,32767,32768,65535};
    VX_BIND_scan_LOCAL;
    VX_BIND_wide_LOCAL;
    for(a=0;a<256;++a)for(b=0;b<256;++b) {
#ifdef __SDCC_z80
        if(b!=0 && b!=1 && b!=3 && b!=27 && b!=30 && b!=63 && b!=64 &&
           b!=127 && b!=128 && b!=254 && b!=255)continue;
#endif
        x=(uint8_t)a;y=(uint8_t)b;used=0;reference();old_x=x;old_y=y;
        for(i=0;i<16;++i)expected[i]=trace[i];
        x=(uint8_t)a;y=(uint8_t)b;used=0;vx_bind_scan_run();
        if(used!=16 || x!=old_x || y!=old_y || vx_bind_scan_frame.result.v0!=0)return 1;
        for(i=0;i<16;++i)if(trace[i]!=expected[i])return ((uint32_t)a<<16)|((uint32_t)b<<8)|2;
    }
    for(a=0;a<sizeof(words)/sizeof(*words);++a)for(b=0;b<sizeof(words)/sizeof(*words);++b) {
        used=0;vx_bind_wide_frame.a0=words[a];vx_bind_wide_frame.a1=words[b];vx_bind_wide_run();
        if(used!=1 || trace[0]!=((uint16_t)(words[a]+words[b]) ? 16 : 17))return 3;
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='vexel-region-predicates-') as tmp:
    directory = Path(tmp)
    source = directory/'source.vx'
    source.write_text(SOURCE)
    sizes = {}
    for budget, enabled in ((128, 0), (128, 1), (0, 0), (0, 1)):
        generated = directory/f'g{budget}-{enabled}'
        plan = json.loads(run([solver, source, '--prefer', f'value_frame_bytes={budget}',
                              '--prefer', f'region_predicate_functions={enabled}', '--emit-c', generated]).stdout)
        bindings = {b['name']: b for b in plan['value_bindings']['bindings']}
        scan, wide = bindings['scan'], bindings['wide']
        assert scan['storage'] == ('fixed' if budget else 'automatic')
        assert bool(scan['codegen']['region_predicate_functions']) == bool(enabled)
        assert scan['codegen']['region_predicate_calls'] >= scan['codegen']['region_predicate_functions']
        assert wide['codegen']['region_predicate_functions'] == 0
        sizes[budget, enabled] = scan['bytes']
        client = directory/'client.c'
        client.write_text(CLIENT+'\n#include <stdlib.h>\nvoid vx_trap(uint8_t c){exit(c);}\nint main(void){return check()!=0;}\n')
        binary = directory/'host'
        run(['cc', '-std=c11', '-O2', '-fsanitize=undefined', '-fno-sanitize-recover=all',
             '-I'+str(generated), '-I'+str(ROOT/'backends/msx-neo8/runtime'), client,
             generated/'vx_binding_frames.c', '-o', binary])
        run([binary])
        if args.msx:
            spec = importlib.util.spec_from_file_location('project', ROOT/'experimental/relational/project_msx.py')
            backend = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(backend)
            project = directory/f'p{budget}-{enabled}'
            backend.package(generated, project, source, solver)
            (project/'user/main.c').write_text('#include "vx_binding_frames.c"\n'+CLIENT+'''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check();__asm halt __endasm;}
''')
            run(['make', '-C', project, '-j2', 'simulate', 'SIMFLAGS=--max-ticks=1200000000'])
            execution = json.loads((project/'build/execution.json').read_text())
            assert execution['observation']['last_result'] == execution['observation']['fault'] == 0, execution
    assert sizes[128, 0] == sizes[128, 1] == sizes[0, 0] == sizes[0, 1]
print('Region predicates: byte wrap, signed truth, word carries, fresh native replies and fixed/automatic frames passed'+
      (' (SDCC/Z80 too)' if args.msx else ' (UBSan)'))
