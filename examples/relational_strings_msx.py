#!/usr/bin/env python3
"""Known byte-string reads and mandatory bounds checks on host and Z80."""
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


support = module('source_msx', REPO/'examples/relational_source_msx.py')
simulator = module('simulator', REPO/'backends/msx-neo8/tools/simulate.py')
sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('machine', type=Path)
    parser.add_argument('--output', type=Path, default=Path('build/relational-strings-msx'))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    machine, source = args.machine.resolve(), REPO/'examples/relational_strings.vx'
    generated, project = directory/'projection', directory/'project'
    support.run([machine, source, '--source', 'read_byte', '--source', 'discard_byte',
                 '--budget', '2', '--emit-c', generated])
    report = support.backend.package(generated, project, source, machine)
    harness = directory/'host.c'
    harness.write_text('''#include "vexel.h"
#include <setjmp.h>
static jmp_buf trapped;
void vx_trap(uint8_t reason){longjmp(trapped,reason);}
uint32_t checked(uint8_t op,uint8_t n){
    int reason=setjmp(trapped);if(reason)return 65536UL+(uint32_t)reason;
    if(op){vx_discard_byte(n);return vx_discard_byte_read(0);}
    vx_read_byte(n);return vx_read_byte_read(0);
}
''')
    runtime = directory/'runtime.c'
    runtime.write_text((project/'runtime/vx_runtime.c').read_text().replace(
        'void vx_trap(uint8_t reason)', 'void vx_unused_trap(uint8_t reason)', 1))
    library = directory/'host.so'
    support.run(['cc', '-std=c11', '-O2', '-shared', '-fPIC', '-I'+str(generated),
                 '-I'+str(project/'runtime'), generated/'vx_bridges.c', runtime, harness, '-o', library])
    dll = ctypes.CDLL(str(library))
    dll.checked.argtypes, dll.checked.restype = [ctypes.c_uint8, ctypes.c_uint8], ctypes.c_uint32
    expected = [65, 0, 66]
    for n in range(256):
        assert dll.checked(0, n) == expected[n%3], n
        assert dll.checked(1, n) == (7 if n<3 else 65552), n
    (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    uint16_t n;uint8_t phase=0;
    vx_last_result=0;
    for(n=0;n<256;++n){
        vx_read_byte((uint8_t)n);
        if(vx_read_byte_read(0)!=(phase==0?65:phase==1?0:66)){
            vx_last_result=1+n;__asm halt __endasm;
        }
        ++phase;if(phase==3)phase=0;
    }
    for(n=0;n<3;++n){
        vx_discard_byte((uint8_t)n);
        if(vx_discard_byte_read(0)!=7){vx_last_result=257+n;__asm halt __endasm;}
    }
    __asm halt __endasm;
}
''')
    built = support.run(['make', '-C', project, '-j2', 'simulate', 'SIMFLAGS=--max-ticks=100000000'])
    (directory/'build.log').write_text(built.stdout+built.stderr)
    observation = json.loads((project/'build/execution.json').read_text())['observation']
    assert observation['last_result'] == observation['fault'] == 0, observation
    invalid = directory/'out-of-bounds'
    support.backend.package(generated, invalid, source, machine)
    (invalid/'user/main.c').write_text('#include "vexel.h"\nvolatile uint32_t vx_last_result;\n'
        'void main(void) VX_ABI VX_RESIDENT {vx_last_result=0;vx_discard_byte(3);'
        'vx_last_result=1;__asm halt __endasm;}\n')
    built = support.run(['make', '-C', invalid, '-j2', 'all', 'build/neo8-runner'])
    (invalid/'build.log').write_text(built.stdout+built.stderr)
    rom = invalid/'build/game.rom'
    symbols = simulator.symbols(Path(str(rom)+'.symbols.map'))
    plan = json.loads((invalid/'generated/plan.json').read_text())
    snapshot = invalid/'build/ram.bin'
    result = support.run([invalid/'build/neo8-runner','--rom',rom,'--dump-ram',snapshot,
        '--max-ticks','10000000','--stack-floor',str(plan['resident_limit']),'--himem','62336',
        '--irq-period','0','--shadow',str(symbols['_vx_segment_b']),'--fault',str(symbols['_vx_fault'])])
    trapped = json.loads(result.stdout)
    at = symbols['_vx_last_result']-0xc000
    assert trapped['fault'] == 16 and int.from_bytes(snapshot.read_bytes()[at:at+4],'little') == 0, trapped
    evidence = {'machine_sha256':sha(machine),'source_sha256':sha(source),'driver_sha256':sha(__file__),
                'host_activations':512,'z80_valid_activations':259,'project':str(project),'report':report,
                'execution_sha256':sha(project/'build/execution.json'),
                'expected_trap':{'reason':16,'observation':trapped,'rom_sha256':sha(rom),
                                 'runner_sha256':sha(invalid/'build/neo8-runner'),'project':str(invalid)}}
    (directory/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('512 host and 259 valid Z80 activations passed, plus discarded string bounds trap')
    print('Evidence:',directory/'comparison.json')


if __name__ == '__main__':
    main()
