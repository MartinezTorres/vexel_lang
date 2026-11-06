#!/usr/bin/env python3
"""Exercise native absolute storage and reject collisions after real linking."""
import argparse
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('source_msx', REPO/'examples/relational_source_msx.py')
support = importlib.util.module_from_spec(spec)
spec.loader.exec_module(support)
sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('machine', type=Path)
    parser.add_argument('--output', type=Path, default=Path('build/relational-bindings-msx'))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    machine = args.machine.resolve()
    evidence = {'machine_sha256': sha(machine), 'driver_sha256': sha(__file__), 'candidates': []}
    for name, address, rejection in [('valid', 0xef7c, None), ('resident', 0xc000, 'overlaps linked RAM'),
                                      ('stack', 0xef7d, 'overlaps reserved stack')]:
        source = directory/(name+'.vx')
        source.write_text('[[msx_address('+str(address)+')]]!!device:#u24;\n'
                          '&^entry(n:#u8){device=(#u24)n+0x123400;device}\n')
        generated, project = directory/(name+'-projection'), directory/name
        support.run([machine, source, '--source', 'entry', '--budget', '4', '--emit-c', generated])
        report = support.backend.package(generated, project, source, machine)
        binding = report['boundary_storage'][0]['binding']
        assert binding == dict(space='address', address=address, bytes=4), binding
        plan = json.loads((project/'generated/plan.json').read_text())
        assert plan['storage_bindings'][0]['bytes'] == 4
        if rejection is None:
            library = directory/'host.so'
            support.run(['cc', '-std=c11', '-O2', '-shared', '-fPIC', '-I'+str(generated),
                         '-I'+str(project/'runtime'), generated/'vx_bridges.c',
                         project/'runtime/vx_runtime.c', '-o', library])
            dll = ctypes.CDLL(str(library))
            dll.vx_entry.argtypes = [ctypes.c_uint8]
            dll.vx_entry_read.argtypes, dll.vx_entry_read.restype = [ctypes.c_uint16], ctypes.c_uint32
            for n in range(256):
                dll.vx_entry(n)
                assert dll.vx_entry_read(0) == 0x123400+n, n
        (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    uint16_t n;
    vx_last_result=0;
    for(n=0;n<256;++n) {
        vx_entry((uint8_t)n);
        if(vx_entry_read(0)!=0x123400UL+(uint32_t)n) {
            vx_last_result=1+n;__asm halt __endasm;
        }
    }
    __asm halt __endasm;
}
''')
        result = subprocess.run(['make', '-C', str(project), '-j2', 'all' if rejection else 'simulate'],
                                text=True, capture_output=True, timeout=120)
        (directory/(name+'-build.log')).write_text(result.stdout+result.stderr)
        item = {'name': name, 'binding': binding, 'project': str(project), 'source_sha256': sha(source),
                'plan_sha256': sha(project/'generated/plan.json')}
        if rejection:
            assert result.returncode != 0 and rejection in result.stderr, result.stdout+result.stderr
            assert not (project/'build/resolution.json').exists()
            item['rejection'] = rejection
            item['linked_map_sha256'] = sha(project/'build/game.rom.areas.map')
        else:
            assert result.returncode == 0, result.stdout+result.stderr
            observed = json.loads((project/'build/execution.json').read_text())['observation']
            assert observed['last_result'] == observed['fault'] == 0, observed
            item['host_activations'] = item['z80_activations'] = 256
            item['rom_sha256'] = sha(project/'build/game.rom')
            item['execution_sha256'] = sha(project/'build/execution.json')
        evidence['candidates'].append(item)
    (directory/'comparison.json').write_text(json.dumps(evidence, indent=2)+'\n')
    print('256 host and 256 Z80 activations passed; two linked storage collisions rejected')
    print('Evidence:', directory/'comparison.json')


if __name__ == '__main__':
    main()
