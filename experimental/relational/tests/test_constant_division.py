#!/usr/bin/env python3
"""Native division: constants and proved nonzero variable domains on host/Z80."""
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
CASES = {
    'u8_3': (8, False, 3), 'u8_7': (8, False, 7),
    'u8_10': (8, False, 10), 'u8_255': (8, False, 255),
    'u8_wrap': (8, False, -3), 'i8_wrap': (8, True, 253),
    'i8_n3': (8, True, -3), 'i8_7': (8, True, 7),
    'i8_n1': (8, True, -1), 'i8_min': (8, True, -128),
    'u16_7': (16, False, 7), 'u16_257': (16, False, 257),
    'i16_n10': (16, True, -10),
    'u32_7': (32, False, 7), 'u32_65537': (32, False, 65537),
    'u32_max': (32, False, 0xffffffff),
    'i32_n3': (32, True, -3), 'i32_min': (32, True, -0x80000000),
    'u8_variable': (8, False, 'odd'),
    'u16_variable': (16, False, 'odd'),
    'u32_variable': (32, False, 'odd'),
    'i8_positive': (8, True, 'positive'),
    'i8_negative': (8, True, 'negative'),
    'i16_variable': (16, True, 'negative'),
    'i32_variable': (32, True, 'negative'),
}


def integer(value, bits, sign):
    value &= (1 << bits)-1
    return value-(1 << bits) if sign and value >= 1 << (bits-1) else value


def vectors(bits, sign, divisor):
    mask = (1 << bits)-1
    if isinstance(divisor, str):
        edges = sorted({v & mask for v in
                        (0, 1, 2, 3, 7, 15, 63, 127, 128, 255, 256,
                         mask, mask-1, mask//2, mask//2+1)})
        rng = random.Random(0xd171de+bits)
        pairs = [(a, b) for a in edges for b in edges]
        pairs += [(rng.randrange(mask+1), rng.randrange(mask+1)) for _ in range(48)]
        if bits == 8:
            pairs += [(n, (n*73+19) & 255) for n in range(256)]
        result = []
        for pattern, raw_divisor in pairs:
            n = integer(pattern, bits, sign)
            d = raw_divisor | 1 if not sign else (raw_divisor & ((1 << (bits-2))-1))+1
            if divisor == 'negative':
                d = -d
            q = (abs(n)//abs(d)) * (-1 if (n < 0) != (d < 0) else 1)
            r = n-q*d
            result.append([pattern, raw_divisor, integer(q, bits, sign) & 0xffffffff,
                           integer(r, bits, sign) & 0xffffffff])
        return result
    divisor = integer(divisor, bits, sign)
    if bits == 8:
        inputs = range(256)
    else:
        rng = random.Random(0xd171de+bits+divisor)
        inputs = {0, 1, mask, mask-1, (1 << (bits-1))-1, 1 << (bits-1),
                  (1 << (bits-1))+1, 127, 128, 255, 256, 65535, 65536}
        for shift in range(bits):
            for delta in (-1, 0, 1):
                inputs.add((abs(divisor)*(1 << shift)+delta) & mask)
        inputs.update(rng.randrange(mask+1) for _ in range(12))
        inputs = sorted(n & mask for n in inputs)
    result = []
    for pattern in inputs:
        n = integer(pattern, bits, sign)
        q = (abs(n)//abs(divisor)) * (-1 if (n < 0) != (divisor < 0) else 1)
        r = n-q*divisor
        result.append([pattern, integer(q, bits, sign) & 0xffffffff,
                       integer(r, bits, sign) & 0xffffffff])
    return result


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('solver', type=Path)
parser.add_argument('--msx', action='store_true')
parser.add_argument('--baseline', action='store_true')
parser.add_argument('--sharing', action='store_true', help='Exercise the optional shared quotient/remainder realization')
parser.add_argument('--prefer', action='append', default=[])
parser.add_argument('--case', choices=tuple(CASES))
parser.add_argument('--output', type=Path)
args = parser.parse_args()
solver = args.solver.resolve()
if args.output:
    args.output.mkdir(parents=True, exist_ok=True)
workspace = (contextlib.nullcontext(args.output.resolve()) if args.output else
             tempfile.TemporaryDirectory(prefix='vexel-constant-division-'))
with workspace as temporary:
    out = Path(temporary)
    results = []
    for name, (bits, sign, divisor) in CASES.items():
        if args.case and name != args.case:
            continue
        source = out/(name+'.vx')
        scalar = ('i' if sign else 'u')+str(bits)
        variable = isinstance(divisor, str)
        if variable:
            expression = '(#'+scalar+')(m|1)' if not sign else '(#'+scalar+')(m&'+str((1 << (bits-2))-1)+')+1'
            if divisor == 'negative':
                expression = '0-('+expression+')'
            source.write_text('&^entry(n:#'+scalar+',m:#u'+str(bits)+'){d:#'+scalar+'='+expression+';(n/d,n%d)}\n')
        else:
            source.write_text('&^entry(n:#'+scalar+'){(n/(#'+scalar+')('+str(divisor)+'),'
                              'n%(#'+scalar+')('+str(divisor)+'))}\n')
        rows = vectors(bits, sign, divisor)
        table = ',\n'.join('{'+','.join(str(v)+'UL' for v in row)+'}' for row in rows)
        argument = '('+('int' if sign else 'uint')+str(bits)+'_t)data[i][0]'
        if variable:
            argument += ',(uint'+str(bits)+'_t)data[i][1]'
        first_result = 2 if variable else 1
        common = '#include "vexel.h"\n'
        common += 'static const uint32_t data[]['+str(first_result+2)+']={\n'+table+'};\n'
        ordinary = common+'uint32_t check(void){unsigned i;for(i=0;i<'+str(len(rows))+';++i){\n'
        ordinary += 'vx_entry('+argument+');\n'
        ordinary += 'if(vx_entry_read(0)!=data[i]['+str(first_result)+'])return i+1;\n'
        ordinary += 'if(vx_entry_read(1)!=data[i]['+str(first_result+1)+'])return i+1001;\n}return 0;}\n'
        binding = common+'#include "vx_value_entry.h"\n'
        binding += 'uint32_t check(void){unsigned i;vx_entry_result r;for(i=0;i<'+str(len(rows))+';++i){\n'
        binding += 'vx_entry_value(&r,'+argument+');\n'
        binding += 'if((uint32_t)r.v0!=data[i]['+str(first_result)+'])return i+1;\n'
        binding += 'if((uint32_t)r.v1!=data[i]['+str(first_result+1)+'])return i+1001;\n}return 0;}\n'
        for budget in (0, 128):
            tag = name+'-'+str(budget)
            generated = out/('generated-'+tag)
            command = [solver, source, '--program', '--prefer', 'frame_ram_bytes='+str(budget),
                       '--prefer', 'value_frame_bytes='+str(budget), '--prefer',
                       'value_table_bytes='+('512' if budget else '0'), '--emit-c', generated]
            if args.sharing:
                command += ['--prefer','calculation_division_sharing=1','--prefer','value_division_sharing=1']
            for preference in args.prefer:command += ['--prefer',preference]
            if budget:
                command += ['--schedule', 'lifo', '--branch-order', 'reverse']
            report = json.loads(support.run(command).stdout)
            (out/('report-'+tag+'.json')).write_text(json.dumps(report, indent=2)+'\n')
            available = report['sources'][0]['value_projection']['available']
            assert available or args.baseline, (tag, report['sources'][0]['value_projection'])
            for mode, text in [('ordinary', ordinary)]+([('binding', binding)] if available else []):
                client = out/('client-'+tag+'-'+mode+'.c')
                client.write_text(text+'#include <stdio.h>\n'
                                  'int main(void){uint32_t r=check();printf("%lu\\n",(unsigned long)r);return r!=0;}\n')
                binary = out/('host-'+tag+'-'+mode)
                support.run(['cc', '-std=c11', '-O2', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                             '-I'+str(generated), '-I'+str(ROOT/'backends/msx-neo8/runtime'), client,
                             generated/'vx_bridges.c', ROOT/'backends/msx-neo8/runtime/vx_runtime.c', '-o', binary])
                support.run([binary])
            if args.msx:
                # Ordinary C exercises the arithmetic realization. Byte value
                # bindings additionally exercise finite ROM alternatives over
                # the complete input domain, including signed boundaries.
                modes = [('ordinary', ordinary)]
                if budget and available and (bits == 8 or variable):
                    modes.append(('binding', binding))
                for mode, text in modes:
                    # Keep the measured ordinary and binding binaries intact.
                    # A later mode must not replace the artifact whose timing
                    # and stack observation an optimization chooser retained.
                    project = out/('project-'+tag+'-'+mode)
                    support.backend.package(generated, project, source, solver)
                    (project/'user/main.c').write_text(text+'''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check();__asm halt __endasm;}
''')
                    built = support.run(['make', '-C', project, '-j1', 'simulate', 'SIMFLAGS=--max-ticks=400000000'])
                    (out/('build-'+tag+'-'+mode+'.log')).write_text(built.stdout+built.stderr)
                    observation = json.loads((project/'build/execution.json').read_text())['observation']
                    assert observation['fault'] == observation['last_result'] == 0, (tag, mode, observation)
                    results.append({'case': tag, 'mode': mode, 'observation': observation})
            print('PASS', tag, flush=True)
            (out/'results.json').write_text(json.dumps(results, indent=2)+'\n')
print('Defined division: constants, variable domains, signed truncation/remainder, minimum/-1 and finite tables agree'+
      (' on host and Z80' if args.msx else ' (UBSan)'))
