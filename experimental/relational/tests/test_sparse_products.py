#!/usr/bin/env python3
"""Sparse modular products preserve signed wrapping and computed operands."""
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
parser.add_argument('--baseline', action='store_true')
parser.add_argument('--output', type=Path)
args = parser.parse_args()
solver = args.solver.resolve()
byte_factors = (3, 5, 7, 9, 15, 17, 63, 65, 127, 129, 255)
word_factors = (3, 7, 17, 255, 257, 32767, 32769, 65529)
signed_factors = (-3, -7, -17, -255, -257, 32767)
wide_factors = (3, 7, 65537, 2147483647, 2147483649, 4294967293)
signed_wide_factors = (-3, -7, -65537, 2147483647, -2147483647)
samples = (0, 1, 3, 127, 128, 255, 256, 257, 32767, 32768, 32769, 65534, 65535)
source_text = ''
for name, kind, factors in [('bytes', 'u8', byte_factors), ('words', 'u16', word_factors),
                             ('signed_words', 'i16', signed_factors), ('wide', 'u32', wide_factors),
                             ('signed_wide', 'i32', signed_wide_factors)]:
    expressions = ','.join(f'x*(#{kind})({n})' for n in factors)
    source_text += f'[[c_binding({name},full,1,exclusive)]] &^{name}(x:#{kind}){{({expressions})}}\n'
source_text += '''
[[c_binding(computed,full,1,exclusive)]]
&^computed(a:#u8,b:#u8){x:#u8=a+b; (x*7,x*3,(#u16)x*(#u16)17,(#u16)x+(#u16)b)}
'''
names = ('bytes','words','signed_words','wide','signed_wide','computed')
client = ''.join(f'#include "vx_bind_{name}.h"\n' for name in names)
# Python supplies all word expectations; byte products use repeated addition.
rows = [[x]+[(x*f)&65535 for f in word_factors]+[(x*f)&65535 for f in signed_factors] for x in samples]
client += f'static const uint16_t words[][{len(rows[0])}]={{\n'
client += ',\n'.join('{'+','.join(str(x)+'U' for x in row)+'}' for row in rows)+'};\n'
wide_samples = (*samples, 65536, 0x12345678, 0x55555555, 0x7fffffff, 0x80000000, 0xaaaaaaa9, 0xfffffffe, 0xffffffff)
wide_rows = [[x]+[(x*f)&0xffffffff for f in (*wide_factors,*signed_wide_factors)] for x in wide_samples]
client += f'static const uint32_t wide_words[][{len(wide_rows[0])}]={{\n'
client += ',\n'.join('{'+','.join(str(x)+'UL' for x in row)+'}' for row in wide_rows)+'};\n'
client += 'static const uint8_t increments[]={'+','.join(map(str,byte_factors))+'};\n'
client += f'''
uint32_t check_sparse(void) {{
    uint16_t x,y,z,i;uint8_t expected[{len(byte_factors)}]={{0}};
    VX_BIND_bytes_LOCAL;VX_BIND_words_LOCAL;VX_BIND_signed_words_LOCAL;VX_BIND_computed_LOCAL;
    VX_BIND_wide_LOCAL;VX_BIND_signed_wide_LOCAL;
    for(x=0;x<256;++x) {{
        vx_bind_bytes_frame.a0=(uint8_t)x;vx_bind_bytes_run();
'''
client += '        if('+' || '.join(f'vx_bind_bytes_frame.result.v{i}!=expected[{i}]' for i in range(len(byte_factors)))+')return 1+x;\n'
client += '''
        for(i=0;i<sizeof(expected);++i)expected[i]=(uint8_t)(expected[i]+increments[i]);
        for(y=0;y<256;y+=17) {
            z=(x+y)&255;
            vx_bind_computed_frame.a0=(uint8_t)x;vx_bind_computed_frame.a1=(uint8_t)y;vx_bind_computed_run();
            if(vx_bind_computed_frame.result.v0!=(uint8_t)(z*7U) ||
               vx_bind_computed_frame.result.v1!=(uint8_t)(z*3U) ||
               vx_bind_computed_frame.result.v2!=z*17U ||
               vx_bind_computed_frame.result.v3!=z+y)return 1000UL+x*256UL+y;
        }
    }
    for(i=0;i<sizeof(words)/sizeof(words[0]);++i) {
        vx_bind_words_frame.a0=words[i][0];vx_bind_words_run();
        vx_bind_signed_words_frame.a0=VX_VALUE_I16(words[i][0]);vx_bind_signed_words_run();
'''
checks = [f'vx_bind_words_frame.result.v{j}!=words[i][{j+1}]' for j in range(len(word_factors))]
checks += [f'(uint16_t)vx_bind_signed_words_frame.result.v{j}!=words[i][{j+1+len(word_factors)}]' for j in range(len(signed_factors))]
client += '        if('+' || '.join(checks)+')return 100000UL+i;\n    }\n'
client += '''    for(i=0;i<sizeof(wide_words)/sizeof(wide_words[0]);++i) {
        vx_bind_wide_frame.a0=wide_words[i][0];vx_bind_wide_run();
        vx_bind_signed_wide_frame.a0=VX_VALUE_I32(wide_words[i][0]);vx_bind_signed_wide_run();
'''
checks = [f'vx_bind_wide_frame.result.v{j}!=wide_words[i][{j+1}]' for j in range(len(wide_factors))]
checks += [f'(uint32_t)vx_bind_signed_wide_frame.result.v{j}!=wide_words[i][{j+1+len(wide_factors)}]' for j in range(len(signed_wide_factors))]
client += '        if('+' || '.join(checks)+')return 200000UL+i;\n    }return 0;\n}\n'

if args.output:
    args.output.mkdir(parents=True, exist_ok=True)
workspace = contextlib.nullcontext(args.output.resolve()) if args.output else tempfile.TemporaryDirectory(prefix='vexel-sparse-products-')
with workspace as temporary:
    root = Path(temporary)
    source = root/'products.vx'
    source.write_text(source_text)
    measurements = {}
    for frame in (0, 128):
        generated = root/f'generated-{frame}'
        report = json.loads(support.run([solver,source,'--program','--prefer',f'value_frame_bytes={frame}',
                                         '--prefer','value_table_bytes=0','--emit-c',generated]).stdout)
        assert all(p['storage']==('fixed' if frame else 'automatic') for p in report['value_bindings']['bindings'])
        if not args.baseline:
            for name in names:
                header = (generated/f'vx_bind_{name}.h').read_text()
                assert 'VX_VALUE_MUL' not in header, header
                # Computed operands retain a single-evaluation barrier; dword
                # products retain their leaf helper to limit register spills.
                if name in ('computed', 'wide', 'signed_wide'):
                    assert 'vx_value_scale' in header, header
        host = root/f'host-{frame}.c'
        binary = root/f'host-{frame}'
        host.write_text(client+'int main(void){return check_sparse()!=0;}\n')
        support.run(['cc','-std=c11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',
                     '-I'+str(generated),'-I'+str(ROOT/'backends/msx-neo8/runtime'),host,
                     generated/'vx_binding_frames.c','-o',binary])
        support.run([binary])
        if args.msx:
            project = root/f'project-{frame}'
            support.backend.package(generated,project,source,solver)
            (project/'user/frames.c').write_text('#include "vx_binding_frames.c"\n')
            (project/'user/main.c').write_text(client+'''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check_sparse();__asm halt __endasm;}
''')
            build = support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=400000000',
                                 'SDCCFLAGS=-mz80 --std-sdcc11 --sdcccall 0 --opt-code-speed --max-allocs-per-node 2000'])
            (root/f'build-{frame}.log').write_text(build.stdout+build.stderr)
            result = json.loads((project/'build/execution.json').read_text())
            assert result['observation']['last_result']==result['observation']['fault']==0,result
            measurements[str(frame)] = result
    (root/'measurements.json').write_text(json.dumps(measurements,indent=2)+'\n')
print('Sparse products: all byte values, word/dword boundaries, signed modular differences and computed operands agree'+
      (' on host and Z80' if args.msx else ' (UBSan)'))
