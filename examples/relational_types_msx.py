#!/usr/bin/env python3
"""Independent execution checks for revised types and partial schema proofs."""
import argparse
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('source_msx', REPO / 'examples/relational_source_msx.py')
source_msx = importlib.util.module_from_spec(spec)
spec.loader.exec_module(source_msx)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('machine', type=Path)
    parser.add_argument('--output', type=Path, default=Path('build/relational-types-msx'))
    args = parser.parse_args()
    machine = args.machine.resolve()
    source = REPO / 'examples/relational_types.vx'
    args.output.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    generated, project = directory / 'projection', directory / 'project'
    source_msx.run([machine, source, '--source', 'next', '--source', 'partial', '--source', 'backtrack', '--budget', '4', '--emit-c', generated])
    report = source_msx.backend.package(generated, project, source, machine)
    assert report['runtime_functions'][0]['result_type'] == '#Pair(a:#Box(x:#u16),b:#Box(x:#u16))', report
    library = directory / 'execution.so'
    source_msx.run(['cc', '-std=c11', '-O2', '-shared', '-fPIC', '-I'+str(project/'generated'),
                    '-I'+str(project/'runtime'), project/'generated/vx_bridges.c',
                    project/'runtime/vx_runtime.c', '-o', library])
    dll = ctypes.CDLL(str(library))
    for name, increment in [('next', 256), ('partial', 300), ('backtrack', 16)]:
        invoke, read = getattr(dll, 'vx_'+name), getattr(dll, 'vx_'+name+'_read')
        invoke.argtypes = [ctypes.c_uint8]
        read.argtypes, read.restype = [ctypes.c_uint16], ctypes.c_uint32
        for n in range(256):
            invoke(n)
            assert read(0) == n+increment, (name, n, read(0))
    (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    uint16_t n;
    vx_last_result=0;
    for(n=0;n<256;++n) {
        vx_next((uint8_t)n);
        if(vx_next_read(0)!=n+256) {vx_last_result=1+n;__asm halt __endasm;}
        vx_partial((uint8_t)n);
        if(vx_partial_read(0)!=n+300) {vx_last_result=257+n;__asm halt __endasm;}
        vx_backtrack((uint8_t)n);
        if(vx_backtrack_read(0)!=n+16) {vx_last_result=513+n;__asm halt __endasm;}
    }
    __asm halt __endasm;
}
''')
    execution = source_msx.run(['make', '-C', project, '-j2', 'simulate'])
    (directory/'build.log').write_text(execution.stdout+execution.stderr)
    result = json.loads((project/'build/execution.json').read_text())
    assert result['observation']['last_result'] == result['observation']['fault'] == 0, result
    sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
    evidence = {'format': 'vexel-relational-types-msx-1', 'machine_sha256': sha(machine),
                'source_sha256': sha(source), 'driver_sha256': sha(__file__), 'host_cases': 768,
                'z80_cases': 768, 'project': str(project), 'report': report,
                'execution_sha256': sha(project/'build/execution.json')}
    (directory/'comparison.json').write_text(json.dumps(evidence, indent=2)+'\n')
    print('768 host and 768 Z80 activations passed; revised record frames, partial schema proofs and type backtracking')
    print('Evidence:', directory/'comparison.json')


if __name__ == '__main__':
    main()
