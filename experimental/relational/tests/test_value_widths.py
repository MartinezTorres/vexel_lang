#!/usr/bin/env python3
"""Observe modular low-bit realizations and their wide-consumer barriers."""
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
MASK32 = 0xffffffff


def byte_results(a, b):
    low = ((a+17)*(b-3)) & 255
    signed = low-256 if low & 128 else low
    return [low, signed & MASK32, (low << 7) & 255, 0]


def word_results(a, b):
    product = (a+b)*(a-b)
    return [product & 255, product & 65535, (product << 7) & 65535, 0]


def shared_results(a, b):
    product = (a+b)*(a-b)
    return [product & MASK32, product & 255, product & 65535]


def barrier_results(a, b):
    product = ((a+b)*(a-b)) & MASK32
    return [(product >> 17) & 255, (product // 65536) & 255,
            product & 65535, int(product >= 0x80000000)]


def wrapped_results(a, b):
    word = ((a+b) & 255)+(b & 255)
    return [word, word*word & 65535, word*word & 255]


def range_results(a, b):
    x, y = a & 15, b & 7
    return [x+y, x*y, (x+y)*9, (x+3)//7, (x+3)%7,
            (x+3)<<3, (x+3)<<8, (x+3)>>8]


def signed_range_results(a, b):
    x, y = (a & 31)-16, (b & 7)+1
    quotient = (abs(x)//3)*(-1 if x < 0 else 1)
    return [v & MASK32 for v in (x*y, quotient, x-quotient*3,
                                x<<2, x>>3, x-y)]


def variable_shift_results(a, b):
    signed_byte = (a & 255)-(256 if a & 128 else 0)
    signed_word = a-(1 << 32) if a & (1 << 31) else a
    return [((a & 255) << (b & 7)) & 255, (a & 255) >> (b & 7),
            (signed_byte >> (b & 7)) & MASK32,
            ((a & 65535) << (b & 15)) & 65535,
            (a << (b & 31)) & MASK32, a >> (b & 31),
            (signed_word >> (b & 31)) & MASK32,
            (((a+7) & MASK32) << (b & 31)) & 255]


def shift_chain_results(a, b):
    n, amount = a & 255, b & 7
    x = (n << 1) & 255
    return [x >> 1, ((x+7) & 255) >> 1,
            ((n << amount) & 255) >> amount,
            ((x*x) & 255) >> amount, (n ^ 128) >> amount,
            ((((((n | 3) & 127) ^ 5) << 1) & 255) >> 1)//3 % 5]


def masked_shift_results(a, b):
    x = ((a & 255) << 1) & 255
    signed = x-256 if x & 128 else x
    return [x >> 1, (x >> 1) & 7, (x >> 1) & 63, x >> 2,
            x >> 7, (signed >> 1) & 63, (x//4) & 31,
            (x >> 1) | 128, (((x+1) & 255) >> 3) & 5]


def stored_bit_results(a, b):
    n, d = a & 31, (b & 63)+1
    q, r = n//d, n%d
    return [q, r, d & r, d | r, d ^ r, ((d << 1) & 255) ^ r,
            r | (d >> 1), ((r ^ (n | 128))-256) & MASK32]


def rotation_results(a, b):
    n = a & 255
    signed = n-256 if n & 128 else n
    return [((n >> k) | (n << (8-k))) & 255 for k in range(1, 8)]+[((signed >> 1) | (signed << 7)) & 255]


def stored_rotation_results(a, b):
    x, y = (a & 255)//7, (b & 255)%13
    def rotate(value, count):
        return ((value >> count) | (value << (8-count))) & 255
    q, left = x >> 2, (x << 6) & 255
    return [rotate(x, 1), rotate(x, 2), rotate(y, 5),
            rotate((x+y) & 255, 3), q, left, q | left]


FIXTURES = {
    'byte': ('''(a:#u32,b:#u32) {
        p:#u32=(a+17)*(b-3);
        ((#u32)(#u8)p,(#u32)(#i8)p,(#u32)(#u8)(p<<7),(#u32)(#u8)(p<<17))
    }''', byte_results),
    'word': ('''(a:#u32,b:#u32) {
        p:#u32=(a+b)*(a-b);
        ((#u32)(#u8)p,(#u32)(#u16)p,(#u32)(#u16)(p<<7),(#u32)(#u16)(p<<24))
    }''', word_results),
    'shared': ('''(a:#u32,b:#u32) {
        p:#u32=(a+b)*(a-b);(p,(#u32)(#u8)p,(#u32)(#u16)p)
    }''', shared_results),
    'barriers': ('''(a:#u32,b:#u32) {
        p:#u32=(a+b)*(a-b);
        ((#u32)(#u8)(p>>17),(#u32)(#u8)(p/65536),(#u32)(#u16)(p%65536),
         (#i32)p<0?(#u32)1:(#u32)0)
    }''', barrier_results),
    'wrap': ('''(a:#u32,b:#u32) {
        s:#u8=(#u8)(a+b);w:#u16=(#u16)s+(#u16)(#u8)b;
        ((#u32)w,(#u32)(#u16)(w*w),(#u32)(#u8)(w*w))
    }''', wrapped_results),
    # Every output is observed at its complete declared width. These are
    # value-range proofs, independent of the low-bit demand cases above.
    'range': ('''(a:#u32,b:#u32) {
        x:#u32=a&15;y:#u32=b&7;
        (x+y,x*y,(x+y)*9,(x+3)/7,(x+3)%7,(x+3)<<3,(x+3)<<8,(x+3)>>8)
    }''', range_results),
    'range_signed': ('''(a:#u32,b:#u32) {
        x:#i32=(#i32)(a&31)-16;y:#i32=(#i32)(b&7)+1;
        ((#u32)(x*y),(#u32)(x/3),(#u32)(x%(-3)),
         (#u32)(x<<2),(#u32)(x>>3),(#u32)(x-y))
    }''', signed_range_results),
    'variable_shift': ('''(a:#u32,b:#u32) {
        ((#u32)((#u8)a<<(b&7)),(#u32)((#u8)a>>(b&7)),
         (#u32)((#i8)a>>(b&7)),(#u32)((#u16)a<<(b&15)),
         a<<(b&31),a>>(b&31),(#u32)((#i32)a>>(b&31)),
         (#u32)(#u8)((a+7)<<(b&31)))
    }''', variable_shift_results),
    # An unsigned byte right shift of an inlined wrapped expression exposed
    # an SDCC signed-promotion error at input 64. Cover every byte and count.
    'shift_chain': ('''(a:#u32,b:#u32) {
        n:#u8=(#u8)a;x:#u8=n<<1;z:#u8=((n|3)&127)^5;
        ((#u32)(x>>1),(#u32)((x+7)>>1),
         (#u32)((#u8)(n<<(b&7))>>(b&7)),
         (#u32)((x*x)>>(b&7)),(#u32)((n^128)>>(b&7)),
         (#u32)(((#u8)(z<<1)>>1)/3%5))
    }''', shift_chain_results),
    # A consumer mask may absorb the unsigned-shift correction, but a full
    # consumer of the same producer and signed shifts keep their own meaning.
    'masked_shifts': ('''(a:#u32,b:#u32) {
        n:#u8=(#u8)a;x:#u8=n<<1;r:#u8=x>>1;
        ((#u32)r,(#u32)(r&7),(#u32)((#u8)(x>>1)&63),
         (#u32)((#u8)(x>>2)&255),(#u32)((#u8)(x>>7)&1),
         (#u32)((#u8)((#i8)x>>1)&63),(#u32)((#u8)(x/4)&31),
         (#u32)((#u8)(x>>1)|128),(#u32)((#u8)((x+1)>>3)&5))
    }''', masked_shift_results),
    # SDCC can forward a just-published byte through both operands of a
    # bitwise operation. Input (1,1) exposed d&r becoming r&r in fixed RAM.
    'stored_bits': ('''(a:#u32,b:#u32) {
        n:#u8=(#u8)(a&31);d:#u8=(#u8)(b&63)+1;q:#u8=n/d;r:#u8=n%d;
        ((#u32)q,(#u32)r,(#u32)(d&r),(#u32)(d|r),(#u32)(d^r),
         (#u32)((#u8)(d<<1)^r),(#u32)(r|(d>>1)),
         (#u32)(#i8)(r^(n|128)))
    }''', stored_bit_results),
    'stored_rotations': ('''(a:#u32,b:#u32) {
        x:#u8=(#u8)a/7;y:#u8=(#u8)b%13;
        total:#u8=x+y;q:#u8=x>>2;left:#u8=x<<6;
        ((#u32)((x>>1)|(x<<7)),(#u32)((x>>2)|(x<<6)),
         (#u32)((y>>5)|(y<<3)),(#u32)((total>>3)|(total<<5)),
         (#u32)q,(#u32)left,(#u32)(q|left))
    }''', stored_rotation_results),
    'rotations': ('''(a:#u32,b:#u32) {
        n:#u8=(#u8)a;
        ((#u32)((n>>1)|(n<<7)),(#u32)((n>>2)|(n<<6)),
         (#u32)((n>>3)|(n<<5)),(#u32)((n>>4)|(n<<4)),
         (#u32)((n>>5)|(n<<3)),(#u32)((n>>6)|(n<<2)),
         (#u32)((n>>7)|(n<<1)),(#u32)(#u8)(((#i8)n>>1)|((#i8)n<<7)))
    }''', rotation_results),
}

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('solver', type=Path)
parser.add_argument('--output', type=Path)
parser.add_argument('--msx', action='store_true')
parser.add_argument('--baseline', action='store_true', help='Check values without requiring narrower code')
parser.add_argument('--case', choices=tuple(FIXTURES))
parser.add_argument('--prefer', action='append', default=[])
args = parser.parse_args()
solver = args.solver.resolve()
if args.output:
    args.output.mkdir(parents=True, exist_ok=True)
workspace = (contextlib.nullcontext(args.output.resolve()) if args.output else
             tempfile.TemporaryDirectory(prefix='vexel-value-widths-'))
edges = [0, 1, 127, 128, 255, 256, 32767, 32768, 65535, 65536, 0x7fffffff, 0x80000000, MASK32]
rng = random.Random(0xB17D3)
vectors = ([(n, n) for n in edges]+[(n, MASK32-n) for n in edges]+
           [(rng.randrange(1 << 32), rng.randrange(1 << 32)) for _ in range(6)])

with workspace as temporary:
    out = Path(temporary)
    results = []
    for name, (body, oracle) in FIXTURES.items():
        if args.case and name != args.case:
            continue
        source = out/(name+'.vx')
        source.write_text('[[c_binding(entry,full,1,exclusive)]] &^entry'+body+'\n')
        for budget in (0, 128):
            tag = name+'-'+str(budget)
            generated = out/('generated-'+tag)
            report = json.loads(support.run([
                solver, source, '--program', '--prefer', 'value_table_bytes=0',
                '--prefer', 'value_choice_branches=0', '--prefer', 'value_frame_bytes='+str(budget),
                '--prefer', 'frame_ram_bytes='+str(budget),
                *[part for preference in args.prefer for part in ('--prefer', preference)],
                '--emit-c', generated]).stdout)
            (out/('report-'+tag+'.json')).write_text(json.dumps(report, indent=2)+'\n')
            if not args.baseline:
                binding = report['value_bindings']['bindings'][0]
                if name not in ('masked_shifts', 'stored_bits', 'rotations', 'stored_rotations'):
                    assert bool(binding['codegen']['narrowed_operations']) == (name in ('byte', 'word', 'wrap', 'range', 'range_signed', 'variable_shift', 'shift_chain')), binding
                if name == 'byte':
                    entry = next(f for f in report['frame_planning']['functions'] if f['name'] == 'vx_entry')
                    assert entry['narrowed_operations'] > 0, entry
            selected_vectors = ([(n, n & 7) for n in range(256)]
                                if name in ('shift_chain', 'masked_shifts', 'rotations') else vectors)
            if name == 'stored_rotations':
                selected_vectors = [(n, (n*73+19) & 255) for n in range(256)]
            rows = [list(v)+oracle(*v) for v in selected_vectors]
            count = len(rows[0])-2
            table = ',\n'.join('{'+','.join(str(n)+'UL' for n in row)+'}' for row in rows)
            text = '#include "vexel.h"\n#include "vx_bind_entry.h"\n'
            table_type = 'uint8_t' if name in ('shift_chain', 'masked_shifts', 'rotations', 'stored_rotations') else 'uint32_t'
            text += f'static const {table_type} data[][{count+2}]={{\n{table}}};\n'
            text += f'''uint32_t check(void) {{
    unsigned i; VX_BIND_entry_LOCAL;
    for(i=0;i<{len(rows)};++i) {{
        vx_bind_entry_frame.a0=data[i][0];vx_bind_entry_frame.a1=data[i][1];
        vx_bind_entry_run();vx_entry(data[i][0],data[i][1]);
'''
            for j in range(count):
                text += f'        if(vx_bind_entry_frame.result.v{j}!=data[i][{j+2}])return {j*1000+1}UL+i;\n'
                text += f'        if(vx_entry_read({j})!=data[i][{j+2}])return {j*1000+10001}UL+i;\n'
            text += '    }return 0;\n}\n'
            client = out/('client-'+tag+'.c')
            client.write_text(text+'''#include <stdio.h>
int main(void){uint32_t r=check();printf("%lu\\n",(unsigned long)r);return r!=0;}
''')
            binary = out/('host-'+tag)
            support.run(['cc', '-std=c11', '-O2', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                         '-I'+str(generated), '-I'+str(ROOT/'backends/msx-neo8/runtime'), client,
                         generated/'vx_binding_frames.c', generated/'vx_bridges.c',
                         ROOT/'backends/msx-neo8/runtime/vx_runtime.c', '-o', binary])
            support.run([binary])
            if args.msx:
                project = out/('project-'+tag)
                support.backend.package(generated, project, source, solver)
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
print('Demanded arithmetic widths: native low-bit operations, full consumers, shift/division barriers and signed extension passed'+
      (' on host and Z80' if args.msx else ' (UBSan)'))
