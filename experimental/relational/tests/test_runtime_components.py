#!/usr/bin/env python3
"""Generated runtime demand composes across headers and handwritten C."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('solver', type=Path)
parser.add_argument('--msx', action='store_true')
args = parser.parse_args()


def run(command, **kwargs):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=240, **kwargs)
    assert result.returncode == 0, f'{command}\n{result.stdout}\n{result.stderr}'
    return result


SOURCE = '''
&^plain(x:#u8){x+1}
[[c_binding(sum,region,1,exclusive)]] &^sum(a:#u8,b:#u8){a+b}
[[c_binding(diff,region,1,exclusive)]] &^diff(a:#u8,b:#u8){a-b}
[[c_binding(word,region,1,exclusive)]] &^word(a:#u16,b:#u16){a+b}
[[c_binding(widened,region,1,exclusive)]] &^widened(a:#u8,b:#u8){(#u16)a+(#u16)b}
[[c_binding(less,region,1,exclusive)]] &^less(a:#u8,b:#u8){a<b}
[[c_binding(product,full,1,exclusive)]] &^product(a:#u8,b:#u8){a*b}
[[c_binding(word_product,full,1,exclusive)]] &^word_product(a:#u16,b:#u16){a*b}
'''
with tempfile.TemporaryDirectory(prefix='vexel-runtime-components-') as temporary:
    root = Path(temporary)
    source = root/'source.vx'
    source.write_text(SOURCE)
    generated = root/'generated'
    run([args.solver.resolve(), source, '--program', '--prefer', 'value_frame_bytes=128',
         '--prefer', 'region_predicate_functions=0', '--emit-c', generated])
    base = (generated/'vx_value_plain.h').read_text()
    assert '#include "vx_support_value_base.h"' in base and 'vx_value_runtime.h' not in base
    helpers = ('add8','sub8','add16','lt8','mul8','mul16','add8wide')
    for name, helper in [('sum', 'add8'), ('diff', 'sub8'), ('word', 'add16'), ('less', 'lt8'),
                         ('product','mul8'), ('word_product','mul16'), ('widened','add8wide')]:
        header = (generated/f'vx_bind_{name}.h').read_text()
        assert f'#include "vx_support_value_{helper}.h"' in header, header
        assert 'vx_value_runtime.h' not in header
        assert sum(f'#include "vx_support_value_{h}.h"' in header for h in helpers) == 1
    # Full runtime inclusion before or after selective generated headers remains
    # supported. Repeated/reversed subsets must not duplicate definitions.
    for mode in ('plain', 'subset', 'reverse', 'full-first', 'full-last'):
        headers = ['vx_value_plain.h']
        if mode != 'plain':
            headers += ['vx_bind_sum.h', 'vx_bind_diff.h']
        if mode in ('full-first', 'full-last'):
            headers.insert(0 if mode == 'full-first' else len(headers), 'vx_value_runtime.h')
        if mode == 'reverse':
            headers.reverse()
        text = ''.join(f'#include "{h}"\n' for h in headers+headers)
        text += '''
static uint8_t check(void) {
    vx_plain_result p; vx_plain_value(&p,255);
    if(p.v0!=0)return 1;
'''
        if mode != 'plain':
            text += '''
    vx_bind_sum_frame.a0=255;vx_bind_sum_frame.a1=2;vx_bind_sum_run();
    if(vx_bind_sum_frame.result.v0!=1)return 2;
    vx_bind_diff_frame.a0=0;vx_bind_diff_frame.a1=1;vx_bind_diff_run();
    if(vx_bind_diff_frame.result.v0!=255)return 3;
'''
        if mode in ('full-first', 'full-last'):
            text += '''
    if(VX_VALUE_ADD16(65535,2)!=1 || VX_VALUE_LT8(255,0))return 4;
    {uint8_t a=255,b=255;if(VX_VALUE_ADD8WIDE(a,b)!=510)return 5;}
'''
        text += 'return 0;}\n'
        client = root/f'{mode}.c'
        client.write_text(text+'int main(void){return check();}\n')
        binary = root/mode
        run(['cc', '-std=c11', '-O2', '-Wall', '-Werror', '-fsanitize=undefined',
             '-fno-sanitize-recover=all', '-I'+str(generated), '-I'+str(ROOT/'backends/msx-neo8/runtime'),
             client, generated/'vx_binding_frames.c', '-o', binary])
        run([binary])
        if args.msx:
            # The assembly is the observable code-size result: SDCC must emit
            # exactly the helpers demanded by this translation unit.
            asm = root/f'{mode}.asm'
            run(['python3', ROOT/'backends/msx-neo8/tools/sdcc_guard.py',
                 'sdcc', '-mz80', '--std-c11', '--opt-code-speed', '--max-allocs-per-node', '2000',
                 '-I'+str(generated), '-I'+str(ROOT/'backends/msx-neo8/runtime'), '-S', client, '-o', asm])
            assembly = asm.read_text()
            wanted = set() if mode == 'plain' else {'add8','sub8'}
            if mode in ('full-first', 'full-last'):
                wanted |= {'add16','lt8','mul8','mul16','add8wide'}
            emitted = {h for h in helpers if f'_vx_value_{h}:' in assembly}
            assert emitted == wanted, (mode, emitted, wanted)
print('Runtime components: independent include orders, complete C compatibility, and unused helper elimination passed')
