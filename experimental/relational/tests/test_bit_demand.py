#!/usr/bin/env python3
"""Consumer bit demand crosses calls and loops without changing observations."""
import argparse
import contextlib
import importlib.util
import json
from pathlib import Path
import random
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('support', ROOT/'examples/relational_source_msx.py')
support = importlib.util.module_from_spec(spec)
spec.loader.exec_module(support)
MASK = 0xffffffff
MIX = '&~mix(a:#u32,b:#u32)->#u32{(a+17)*(b-3)}'
RELAY = '&~relay(a:#u32,b:#u32)->#u32{mix(a,b)}'


def product(a, b):
    return ((a+17)*(b-3)) & MASK


def iterate(a, b, count=3):
    for _ in range(count):
        a = product(a, b)
    return a


def recursive(a, b):
    for _ in range(b & 7):
        a = (a*3+17) & MASK
    return a & 255


def signed_remainder(a, b):
    value = product(a, b)
    value = value-MASK-1 if value & 0x80000000 else value
    return (-(abs(value) % 256) if value < 0 else value % 256) & MASK


FIXTURES = {
    'byte': (MIX+RELAY+'&^entry(a:#u32,b:#u32){(#u32)(#u8)relay(a,b)}',
             lambda a, b: [product(a, b) & 255], 8),
    'word': (MIX+RELAY+'&^entry(a:#u32,b:#u32){((#u32)(#u8)relay(a,b),(#u32)(#u16)relay(b,a))}',
             lambda a, b: [product(a, b) & 255, product(b, a) & 65535], 16),
    'wide': (MIX+RELAY+'&^entry(a:#u32,b:#u32){((#u32)(#u8)relay(a,b),relay(b,a))}',
             lambda a, b: [product(a, b) & 255, product(b, a)], 32),
    'right_shift': (MIX+RELAY+'&^entry(a:#u32,b:#u32){(#u32)(#u8)(relay(a,b)>>17)}',
                    lambda a, b: [(product(a, b) >> 17) & 255], 32),
    'division': (MIX+'&^entry(a:#u32,b:#u32){(#u32)(#u8)(mix(a,b)/65536)}',
                 lambda a, b: [(product(a, b) // 65536) & 255], 32),
    'masked': ('&~mix(a:#u32,b:#u32)->#u32{((a+17)*(b-3))&255}'
               '&^entry(a:#u32,b:#u32){mix(a,b)}',
               lambda a, b: [product(a, b) & 255], 8),
    'or_high': ('&~mix(a:#u32,b:#u32)->#u32{((a+17)*(b-3))|4294901760}'
                '&^entry(a:#u32,b:#u32){mix(a,b)}',
                lambda a, b: [product(a, b) | 0xffff0000], {'arguments': 16, 'results': 32}),
    'slice_shift': (MIX+'&^entry(a:#u32,b:#u32){(#u32)(#u8)(mix(a,b)>>8)}',
                    lambda a, b: [(product(a, b) >> 8) & 255], 16),
    'slice_division': (MIX+'&^entry(a:#u32,b:#u32){(#u32)(#u8)(mix(a,b)/256)}',
                       lambda a, b: [(product(a, b) // 256) & 255], 16),
    'slice_remainder': (MIX+'&^entry(a:#u32,b:#u32){(#u32)(#u8)(((#i32)mix(a,b))%256)}',
                        lambda a, b: [product(a, b) & 255], 8),
    'signed_remainder': (MIX+'&^entry(a:#u32,b:#u32){(#u32)(((#i32)mix(a,b))%256)}',
                         lambda a, b: [signed_remainder(a, b)], 32),
    'signed': ('&~mix(a:#i32,b:#i32)->#i32{(a+17)*(b-3)}'
               '&^entry(a:#u32,b:#u32){(#u32)(#u8)mix((#i32)a,(#i32)b)}',
               lambda a, b: [product(a, b) & 255], 8),
    'capture': ('&^entry(a:#u32,b:#u32){x:#u32=a;&~step(){x=(x+17)*(b-3);}step();(#u32)(#u8)x}',
                lambda a, b: [product(a, b) & 255], 8),
    'loop': ('&^entry(a:#u32,b:#u32){x:#u32=a;i:#u8=0;'
             '(i<3)!@{x=(x+17)*(b-3);i+=1;};(#u32)(#u8)x}',
             lambda a, b: [iterate(a, b) & 255], 8),
    'loop_calls': (MIX+RELAY+'&^entry(a:#u32,b:#u32){x:#u32=a;i:#u8=0;'
                   '(i<3)!@{x=relay(x,b);i+=1;};(#u32)(#u8)x}',
                   lambda a, b: [iterate(a, b) & 255], 8),
    'region': ('[[c_binding(steps,region,1,exclusive)]] '
               '&^entry(a:#u32,b:#u32){x:#u32=a;i:#u8=0;'
               '(i<3)!@{x=(x+17)*(b-3);i+=1;};(#u32)(#u8)x}',
               lambda a, b: [iterate(a, b) & 255], 8),
    'loop_wide': ('&^entry(a:#u32,b:#u32){x:#u32=a;i:#u8=0;'
                  '(i<3)!@{x=(x+17)*(b-3);i+=1;};((#u32)(#u8)x,x>>16)}',
                  lambda a, b: [iterate(a, b) & 255, iterate(a, b) >> 16], 32),
    'recursive': ('&~walk(n:#u8,x:#u32)->#u32{n==0?x:walk(n-1,x*3+17)}'
                  '&^entry(a:#u32,b:#u32){(#u32)(#u8)walk((#u8)(b&7),a)}',
                  lambda a, b: [recursive(a, b)], 8),
}

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('solver', type=Path)
parser.add_argument('--output', type=Path)
parser.add_argument('--msx', action='store_true')
parser.add_argument('--baseline', action='store_true')
parser.add_argument('--case', choices=tuple(FIXTURES))
args = parser.parse_args()
solver = args.solver.resolve()
if args.output:
    args.output.mkdir(parents=True, exist_ok=True)
workspace = (contextlib.nullcontext(args.output.resolve()) if args.output else
             tempfile.TemporaryDirectory(prefix='vexel-bit-demand-'))
edges = [0, 1, 3, 127, 128, 255, 256, 32767, 32768, 65535, 65536, 0x7fffffff, 0x80000000, MASK]
rng = random.Random(0xB17CA11)
vectors = ([(n, n) for n in edges]+[(n, MASK-n) for n in edges]+
           [(rng.randrange(1 << 32), rng.randrange(1 << 32)) for _ in range(8)])

with workspace as temporary:
    out = Path(temporary)
    results = []
    for name, (source_text, oracle, required) in FIXTURES.items():
        if args.case and args.case != name:
            continue
        source = out/(name+'.vx')
        source.write_text(source_text+'\n')
        for budget in (0, 128):
            tag = name+'-'+str(budget)
            generated = out/('generated-'+tag)
            command = [solver, source, '--program', '--prefer', 'frame_ram_bytes='+str(budget),
                       '--prefer', 'value_frame_bytes='+str(budget),
                       '--prefer', 'value_table_bytes=0', '--emit-c', generated]
            if budget:
                command += ['--schedule', 'lifo', '--branch-order', 'reverse']
            report = json.loads(support.run(command).stdout)
            (out/('report-'+tag+'.json')).write_text(json.dumps(report, indent=2)+'\n')
            if not args.baseline:
                entries = [(kind, item) for fn in report['runtime_functions']
                           for kind, group in fn['transport_types'].items() for item in group]
                entries += [('carried', item) for loop in report['interface_demand']['loops'] for item in loop['transport_types']]
                wide = [(kind, item) for kind, item in entries if item['logical'] in ('#u32', '#i32')]
                assert wide, tag
                for kind, item in wide:
                    expected = required[kind] if isinstance(required, dict) else required
                    assert item['storage'] == '#u'+str(expected), (tag, kind, item)
            rows = [list(v)+oracle(*v) for v in vectors]
            count = len(rows[0])-2
            table = ',\n'.join('{'+','.join(str(n)+'UL' for n in row)+'}' for row in rows)
            text = '#include "vexel.h"\n'
            if name == 'region':
                text += '#include "vx_bind_steps.h"\n'
            text += f'static const uint32_t data[][{count+2}]={{\n{table}}};\n'
            text += 'uint32_t check(void) {\n    unsigned i;\n'
            if name == 'region':
                text += '    VX_BIND_steps_LOCAL;\n'
            text += f'''    for(i=0;i<{len(rows)};++i) {{
        vx_entry(data[i][0],data[i][1]);
'''
            for j in range(count):
                text += f'        if(vx_entry_read({j})!=data[i][{j+2}])return {j*1000+1}UL+i;\n'
            if name == 'region':
                text += '''        vx_bind_steps_frame.a0=data[i][0];vx_bind_steps_frame.a1=data[i][1];
        vx_bind_steps_run();if(vx_bind_steps_frame.result.v0!=data[i][2])return 10001UL+i;
'''
            text += '    }return 0;\n}\n'
            client = out/('client-'+tag+'.c')
            client.write_text(text+'''#include <stdio.h>
int main(void){uint32_t r=check();printf("%lu\\n",(unsigned long)r);return r!=0;}
''')
            binary = out/('host-'+tag)
            support.run(['cc', '-std=c11', '-O2', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                         '-I'+str(generated), '-I'+str(ROOT/'backends/msx-neo8/runtime'), client,
                         generated/'vx_bridges.c', *([generated/'vx_binding_frames.c'] if name == 'region' else []),
                         ROOT/'backends/msx-neo8/runtime/vx_runtime.c', '-o', binary])
            support.run([binary])
            if args.msx:
                project = out/('project-'+tag)
                support.backend.package(generated, project, source, solver)
                if name == 'region':
                    (project/'user/frames.c').write_text('#include "vx_binding_frames.c"\n')
                (project/'user/main.c').write_text(text+'''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check();__asm halt __endasm;}
''')
                built = support.run(['make', '-C', project, '-j1', 'simulate', 'SIMFLAGS=--max-ticks=400000000'])
                (out/('build-'+tag+'.log')).write_text(built.stdout+built.stderr)
                observation = json.loads((project/'build/execution.json').read_text())['observation']
                assert observation['fault'] == observation['last_result'] == 0, (tag, observation)
                results.append({'case': tag, 'observation': observation})
            print('PASS', tag, flush=True)
            (out/'results.json').write_text(json.dumps(results, indent=2)+'\n')
print('Bit demand: caller unions, captures, recursive and loop fixed points, signed residues and wide barriers agree'+
      (' on host and Z80' if args.msx else ' (UBSan)'))
