#!/usr/bin/env python3
"""Check scalar operators, logical control and discarded traps on the Z80."""
import argparse
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile

REPO = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


source_msx = module('source_msx', REPO/'examples/relational_source_msx.py')
simulator = module('simulator', REPO/'backends/msx-neo8/tools/simulate.py')
sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('machine', type=Path)
    parser.add_argument('--output', type=Path, default=Path('build/relational-operators-msx'))
    args = parser.parse_args()
    machine = args.machine.resolve()
    source = REPO/'examples/relational_operators.vx'
    args.output.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    generated, project = directory/'projection', directory/'project'
    source_msx.run([machine, source, '--source', 'bits', '--source', 'logical', '--source', 'shift_case',
                    '--source', 'divide_case', '--budget', '16', '--emit-c', generated])
    report = source_msx.backend.package(generated, project, source, machine)
    harness = directory/'host.c'
    harness.write_text('''#include "vexel.h"
#include <setjmp.h>
static jmp_buf trapped;
void vx_trap(uint8_t reason) {longjmp(trapped,reason);}
uint32_t checked(uint8_t operation,uint8_t n) {
    int reason=setjmp(trapped);
    if(reason) return 65536UL+(uint32_t)reason;
    if(operation==0) {vx_bits(n);return 0;}
    if(operation==1) {vx_logical(n);return 0;}
    if(operation==2) {vx_shift_case(n);return vx_shift_case_read(0);}
    vx_divide_case(n);return vx_divide_case_read(0);
}
''')
    runtime = directory/'runtime.c'
    runtime.write_text((project/'runtime/vx_runtime.c').read_text().replace(
        'void vx_trap(uint8_t reason)', 'void vx_unused_default_trap(uint8_t reason)', 1))
    library = directory/'execution.so'
    source_msx.run(['cc', '-std=c11', '-O2', '-shared', '-fPIC', '-I'+str(project/'generated'),
                    '-I'+str(project/'runtime'), project/'generated/vx_bridges.c', runtime, harness, '-o', library])
    dll = ctypes.CDLL(str(library))
    dll.checked.argtypes, dll.checked.restype = [ctypes.c_uint8, ctypes.c_uint8], ctypes.c_uint32
    for name in ('bits', 'logical'):
        read = getattr(dll, 'vx_'+name+'_read')
        read.argtypes, read.restype = [ctypes.c_uint16], ctypes.c_uint32
    table = [((((((n|3)&127)^5)<<1)&255)>>1)//3%5 for n in range(256)]
    for n in range(256):
        assert dll.checked(0, n) == 0
        assert [dll.vx_bits_read(i) for i in range(4)] == [255-n, table[n], n, (0x12345600+n)^0xff00ff00], n
        assert dll.checked(1, n) == 0
        assert [dll.vx_logical_read(i) for i in range(2)] == [1 if n<64 else 5 if n<128 else 6, 1], n
        assert dll.checked(2, n) == (7 if n<8 else 65536+2), n
        assert dll.checked(3, n) == (7 if n else 65536+1), n
    (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
static const uint8_t expected_bits[] = {'''+','.join(map(str, table))+'''};
void main(void) VX_ABI VX_RESIDENT {
    uint16_t n;
    vx_last_result=0;
    for(n=0;n<256;++n) {
        vx_bits((uint8_t)n);
        if(vx_bits_read(0)!=255-n || vx_bits_read(1)!=expected_bits[n] || vx_bits_read(2)!=n ||
           vx_bits_read(3)!=(0xed34a900UL+(uint32_t)n)) {
            vx_last_result=1+n;__asm halt __endasm;
        }
        vx_logical((uint8_t)n);
        if(vx_logical_read(0)!=(n<64?1:n<128?5:6) || vx_logical_read(1)!=1) {
            vx_last_result=257+n;__asm halt __endasm;
        }
        vx_divide_case((uint8_t)(n?n:1));
        if(vx_divide_case_read(0)!=7) {vx_last_result=513+n;__asm halt __endasm;}
    }
    for(n=0;n<8;++n) {
        vx_shift_case((uint8_t)n);
        if(vx_shift_case_read(0)!=7) {vx_last_result=769+n;__asm halt __endasm;}
    }
    __asm halt __endasm;
}
''')
    # Several deliberately discarded operations still perform trap checks.
    # Self-contained 32-bit divide helpers make this a long Z80 workload.
    execution = source_msx.run(['make', '-C', project, '-j2', 'simulate', 'SIMFLAGS=--max-ticks=400000000'])
    (directory/'build.log').write_text(execution.stdout+execution.stderr)
    result = json.loads((project/'build/execution.json').read_text())
    assert result['observation']['last_result'] == result['observation']['fault'] == 0, result
    traps = []
    for name, invocation, reason in [('bad-shift', 'vx_shift_case(8);', 2), ('zero-divisor', 'vx_divide_case(0);', 1)]:
        failed = directory/name
        source_msx.backend.package(generated, failed, source, machine)
        (failed/'user/main.c').write_text('#include "vexel.h"\nvolatile uint32_t vx_last_result;\n'
            'void main(void) VX_ABI VX_RESIDENT {vx_last_result=0;'+invocation+
            'vx_last_result=1;__asm halt __endasm;}\n')
        built = source_msx.run(['make', '-C', failed, '-j2', 'all', 'build/neo8-runner'])
        (failed/'build.log').write_text(built.stdout+built.stderr)
        rom = failed/'build/game.rom'
        symbols = simulator.symbols(Path(str(rom)+'.symbols.map'))
        plan = json.loads((failed/'generated/plan.json').read_text())
        snapshot = failed/'build/ram.bin'
        observed = source_msx.run([failed/'build/neo8-runner', '--rom', rom, '--dump-ram', snapshot,
            '--max-ticks', '10000000', '--stack-floor', str(plan['resident_limit']), '--himem', '62336',
            '--irq-period', '0', '--shadow', str(symbols['_vx_segment_b']), '--fault', str(symbols['_vx_fault'])])
        observation = json.loads(observed.stdout)
        data = snapshot.read_bytes()
        at = symbols['_vx_last_result']-0xc000
        assert observation['fault'] == reason and int.from_bytes(data[at:at+4], 'little') == 0, observation
        evidence = {'name': name, 'expected_fault': reason, 'observation': observation, 'project': str(failed),
                    'rom_sha256': sha(rom), 'runner_sha256': sha(failed/'build/neo8-runner'),
                    'plan_sha256': sha(failed/'generated/plan.json'), 'stack_proof': 'unproven'}
        (failed/'build/expected-trap.json').write_text(json.dumps(evidence, indent=2)+'\n')
        traps.append(evidence)
    evidence = {'format': 'vexel-relational-operators-msx-1', 'machine_sha256': sha(machine),
                'source_sha256': sha(source), 'driver_sha256': sha(__file__), 'host_activations': 1024,
                'z80_valid_activations': 776, 'expected_traps': traps, 'project': str(project),
                'report': report, 'execution_sha256': sha(project/'build/execution.json')}
    (directory/'comparison.json').write_text(json.dumps(evidence, indent=2)+'\n')
    print('1,024 host and 776 valid Z80 activations passed, plus shift and division traps')
    print('Evidence:', directory/'comparison.json')


if __name__ == '__main__':
    main()
