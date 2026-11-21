#!/usr/bin/env python3
"""Independent modular product oracles for narrow and constant C realizations."""
import argparse
import contextlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('solver', type=Path)
parser.add_argument('--msx', action='store_true')
parser.add_argument('--output', type=Path)
args = parser.parse_args()
solver = args.solver.resolve()


def run(command):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=240)
    assert result.returncode == 0, f'{command}\n{result.stdout}\n{result.stderr}'
    return result


SOURCE = '''
[[c_binding(byte_product,full,1,exclusive)]]
&^byte_product(a:#u8,b:#u8){(a*b,(#i8)a*(#i8)b)}
[[c_binding(word_product,full,1,exclusive)]]
&^word_product(a:#u16,b:#u16){(a*b,(#i16)a*(#i16)b)}
[[c_binding(powers,full,1,exclusive)]]
&^powers(a:#u8,b:#u16,c:#i16,d:#u32){
    (a*2,a*128,b*32768,c*-8,c*-32768,d*(#u32)2147483648,
     d*(#u32)4294967294)
}
'''
samples = (0,1,2,3,127,128,255,256,257,32767,32768,32769,65534,65535)
words = [(a,b,(a*b)&65535) for a in samples for b in samples]
powers = []
for x in samples:
    a = x&255
    c = x if x<32768 else x-65536
    d = (x<<16) | (65535-x)
    powers.append((a,x,x,d,(a*2)&255,(a*128)&255,(x*32768)&65535,
                   (c*-8)&65535,(c*-32768)&65535,(d*2147483648)&0xffffffff,
                   (d*-2)&0xffffffff))
client = '''
#include "vx_bind_byte_product.h"
#include "vx_bind_word_product.h"
#include "vx_bind_powers.h"
'''
for name, rows, kind in [('words',words,'uint16_t'), ('powers',powers,'uint32_t')]:
    client += f'static const {kind} {name}[][{len(rows[0])}]={{\n'
    client += ',\n'.join('{'+','.join(str(n)+'UL' for n in row)+'}' for row in rows)+'};\n'
client += '''
uint32_t check_products(void) {
    uint16_t a,b,i;uint8_t expected;
    VX_BIND_byte_product_LOCAL; VX_BIND_word_product_LOCAL; VX_BIND_powers_LOCAL;
    /* Repeated addition supplies an independent oracle for every byte pair. */
    for(a=0;a<256;++a) {
        expected=0;
        for(b=0;b<256;++b) {
            vx_bind_byte_product_frame.a0=(uint8_t)a;
            vx_bind_byte_product_frame.a1=(uint8_t)b;vx_bind_byte_product_run();
            if(vx_bind_byte_product_frame.result.v0!=expected ||
               (uint8_t)vx_bind_byte_product_frame.result.v1!=expected)
                return 1UL+((uint32_t)a<<8)+b;
            expected=(uint8_t)(expected+a);
        }
    }
    for(i=0;i<sizeof(words)/sizeof(words[0]);++i) {
        vx_bind_word_product_frame.a0=words[i][0];
        vx_bind_word_product_frame.a1=words[i][1];vx_bind_word_product_run();
        if(vx_bind_word_product_frame.result.v0!=words[i][2] ||
           (uint16_t)vx_bind_word_product_frame.result.v1!=words[i][2])return 100000UL+i;
    }
    for(i=0;i<sizeof(powers)/sizeof(powers[0]);++i) {
        vx_bind_powers_frame.a0=(uint8_t)powers[i][0];
        vx_bind_powers_frame.a1=(uint16_t)powers[i][1];
        vx_bind_powers_frame.a2=VX_VALUE_I16(powers[i][2]);
        vx_bind_powers_frame.a3=powers[i][3];vx_bind_powers_run();
'''
client += '        if('+' || '.join(
    f'(uint{8 if j<2 else 16 if j<5 else 32}_t)vx_bind_powers_frame.result.v{j}!=powers[i][{j+4}]'
    for j in range(7))+')return 200000UL+i;\n'
client += '    }\n    return 0;\n}\n'

if args.output:
    args.output.mkdir(parents=True, exist_ok=True)
workspace = contextlib.nullcontext(args.output.resolve()) if args.output else tempfile.TemporaryDirectory(prefix='vexel-products-')
with workspace as temporary:
    root = Path(temporary)
    source = root/'products.vx'
    source.write_text(SOURCE)
    measurements = {}
    for budget in (0,64):
        generated = root/f'generated-{budget}'
        report = json.loads(run([solver,source,'--program','--prefer',f'value_frame_bytes={budget}',
                                 '--emit-c',generated]).stdout)
        assert all(b['storage']==('fixed' if budget else 'automatic') for b in report['value_bindings']['bindings'])
        host = root/f'host-{budget}.c'
        host.write_text(client+'int main(void){return check_products()!=0;}\n')
        binary = root/f'host-{budget}'
        run(['cc','-std=c11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',
             '-I'+str(generated),'-I'+str(ROOT/'backends/msx-neo8/runtime'),host,
             generated/'vx_binding_frames.c','-o',binary])
        run([binary])
        if args.msx:
            spec = importlib.util.spec_from_file_location('project',ROOT/'experimental/relational/project_msx.py')
            backend = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(backend)
            project = root/f'project-{budget}'
            backend.package(generated,project,source,solver)
            (project/'user/frames.c').write_text('#include "vx_binding_frames.c"\n')
            (project/'user/main.c').write_text(client+'''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check_products();__asm halt __endasm;}
''')
            built = run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=500000000',
                         'SDCCFLAGS=-mz80 --std-sdcc11 --sdcccall 0 --opt-code-speed --max-allocs-per-node 2000'])
            (root/f'build-{budget}.log').write_text(built.stdout+built.stderr)
            execution = json.loads((project/'build/execution.json').read_text())
            assert execution['observation']['last_result']==execution['observation']['fault']==0, execution
            measurements[str(budget)] = execution
    (root/'measurements.json').write_text(json.dumps(measurements,indent=2)+'\n')
print('Products: every byte pair, word boundaries, signed wrapping and constant shifts agree'+
      (' on host and Z80' if args.msx else ' (UBSan)'))
