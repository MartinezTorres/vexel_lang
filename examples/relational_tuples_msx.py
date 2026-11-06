#!/usr/bin/env python3
"""Tuple snapshots, captured swaps, output-only initialization and completion."""
import argparse
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile

REPO=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('source_msx',REPO/'examples/relational_source_msx.py')
support=importlib.util.module_from_spec(spec);spec.loader.exec_module(support)


def main():
    parser=argparse.ArgumentParser();parser.add_argument('machine',type=Path)
    parser.add_argument('--output',type=Path,default=Path('build/relational-tuples-msx'));args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    machine=args.machine.resolve();source=REPO/'examples/relational_tuples.vx';generated=directory/'projection';project=directory/'project'
    oracles={'swapped':lambda n:[(n+1)%256,n],
             'packed':lambda n:[n,(n+300)%256,(n+300)//256],
             'initialized':lambda n:[n,n+300],
             'completed':lambda n:[n,n+(700 if n<128 else 300)]}
    support.run([machine,source,*[arg for name in oracles for arg in ['--source',name]],'--budget','8','--emit-c',generated])
    report=support.backend.package(generated,project,source,machine)
    library=directory/'host.so'
    support.run(['cc','-O2','-std=c11','-shared','-fPIC','-I'+str(generated),'-I'+str(project/'runtime'),
                 generated/'vx_bridges.c',project/'runtime/vx_runtime.c','-o',library])
    dll=ctypes.CDLL(str(library))
    for name,oracle in oracles.items():
        invoke,read=getattr(dll,'vx_'+name),getattr(dll,'vx_'+name+'_read')
        invoke.argtypes=[ctypes.c_uint8];read.argtypes=[ctypes.c_uint16];read.restype=ctypes.c_uint32
        for n in range(256):
            invoke(n);expected=oracle(n)
            assert [read(i) for i in range(len(expected))]==expected,(name,n)
    (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    uint16_t n;
    vx_last_result=0;
    for(n=0;n<256;++n) {
        vx_swapped((uint8_t)n);
        if(vx_swapped_read(0)!=((n+1)&255) || vx_swapped_read(1)!=n) {vx_last_result=1+n;__asm halt __endasm;}
        vx_packed((uint8_t)n);
        if(vx_packed_read(0)!=n || vx_packed_read(1)!=((n+300)&255) || vx_packed_read(2)!=(n+300)/256) {vx_last_result=257+n;__asm halt __endasm;}
        vx_initialized((uint8_t)n);
        if(vx_initialized_read(0)!=n || vx_initialized_read(1)!=n+300) {vx_last_result=513+n;__asm halt __endasm;}
        vx_completed((uint8_t)n);
        if(vx_completed_read(0)!=n || vx_completed_read(1)!=n+(n<128 ? 700 : 300)) {vx_last_result=769+n;__asm halt __endasm;}
    }
    __asm halt __endasm;
}
''')
    build=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=200000000']);(directory/'build.log').write_text(build.stdout+build.stderr)
    result=json.loads((project/'build/execution.json').read_text())
    assert result['observation']['last_result']==result['observation']['fault']==0,result
    sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
    evidence={'machine_sha256':sha(machine),'source_sha256':sha(source),'driver_sha256':sha(__file__),
              'host_activations':1024,'z80_valid_activations':1024,'report':report,'project':str(project),
              'execution_sha256':sha(project/'build/execution.json')}
    (directory/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('1,024 host and 1,024 Z80 tuple activations passed')
    print('Evidence:',directory/'comparison.json')


if __name__=='__main__':main()
