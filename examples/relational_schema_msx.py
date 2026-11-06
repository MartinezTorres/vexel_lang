#!/usr/bin/env python3
"""Compare record checks on the host and Z80, including expected trap ROMs."""
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
    parser.add_argument('--output', type=Path, default=Path('build/relational-schema-msx'))
    args = parser.parse_args()
    machine = args.machine.resolve()
    source = REPO/'examples/relational_schema.vx'
    args.output.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    generated, project = directory/'projection', directory/'project'
    source_msx.run([machine, source, '--source', 'reset', '--source', 'step', '--source', 'check',
                    '--source', 'construct', '--budget', '4', '--emit-c', generated])
    report = source_msx.backend.package(generated, project, source, machine)
    harness = directory/'host.c'
    harness.write_text('''#include "vexel.h"
#include <setjmp.h>
static jmp_buf trapped;
void vx_trap(uint8_t reason) {longjmp(trapped,reason);}
uint32_t checked(uint8_t operation,uint8_t n) {
    int reason=setjmp(trapped);
    if(reason) return 65536UL+(uint32_t)reason;
    if(operation==0) {vx_reset(n);return 0;}
    if(operation==1) {vx_step(n);return vx_step_read(0);}
    if(operation==2) {vx_check(n);return vx_check_read(0);}
    vx_construct(n);return vx_construct_read(0);
}
''')
    library = directory/'execution.so'
    source_msx.run(['cc', '-std=c11', '-O2', '-shared', '-fPIC', '-I'+str(project/'generated'),
                    '-I'+str(project/'runtime'), project/'generated/vx_bridges.c', harness, '-o', library])
    dll = ctypes.CDLL(str(library))
    dll.checked.argtypes, dll.checked.restype = [ctypes.c_uint8, ctypes.c_uint8], ctypes.c_uint32
    for n in range(256):
        assert dll.checked(0, n) == 0
        assert dll.checked(1, n) == (n+1)%256+(2*n+1)%256, n
        assert dll.checked(2, 3) == (n+2)%256, n
        assert dll.checked(2, 2) == 65536+19, n
        assert dll.checked(2, 3) == (n+2)%256, n  # Invalid writes do not commit.
        assert dll.checked(3, n) == (7 if n==3 else 65536+19), n
    (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    uint16_t n,expected;
    vx_last_result=0;
    for(n=0;n<256;++n) {
        vx_reset((uint8_t)n);vx_step((uint8_t)n);
        expected=(uint16_t)(uint8_t)(n+1)+(uint16_t)(uint8_t)(2*n+1);
        if(vx_step_read(0)!=expected) {vx_last_result=1+n;__asm halt __endasm;}
        vx_check(3);
        if(vx_check_read(0)!=(uint8_t)(n+2)) {vx_last_result=257+n;__asm halt __endasm;}
        vx_construct(3);
        if(vx_construct_read(0)!=7) {vx_last_result=513+n;__asm halt __endasm;}
    }
    __asm halt __endasm;
}
''')
    execution = source_msx.run(['make', '-C', project, '-j2', 'simulate'])
    (directory/'build.log').write_text(execution.stdout+execution.stderr)
    result = json.loads((project/'build/execution.json').read_text())
    assert result['observation']['last_result'] == result['observation']['fault'] == 0, result
    traps = []
    for name, invocation in [('bad-update', 'vx_reset(0);vx_check(2);'), ('bad-constructor', 'vx_construct(2);')]:
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
        assert observation['fault'] == 19 and int.from_bytes(data[at:at+4], 'little') == 0, observation
        evidence = {'name': name, 'expected_fault': 19, 'observation': observation, 'project': str(failed),
                    'rom_sha256': sha(rom), 'runner_sha256': sha(failed/'build/neo8-runner'),
                    'plan_sha256': sha(failed/'generated/plan.json'), 'stack_proof': 'unproven'}
        (failed/'build/expected-trap.json').write_text(json.dumps(evidence, indent=2)+'\n')
        traps.append(evidence)
    evidence = {'format': 'vexel-relational-schema-msx-1', 'machine_sha256': sha(machine),
                'source_sha256': sha(source), 'driver_sha256': sha(__file__), 'host_activations': 1536,
                'z80_valid_activations': 1024, 'expected_traps': traps, 'project': str(project),
                'report': report, 'execution_sha256': sha(project/'build/execution.json')}
    (directory/'comparison.json').write_text(json.dumps(evidence, indent=2)+'\n')
    print('1,536 host and 1,024 valid Z80 activations passed, plus two Z80 invariant traps')
    print('Evidence:', directory/'comparison.json')


if __name__ == '__main__':
    main()
