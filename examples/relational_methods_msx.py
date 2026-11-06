#!/usr/bin/env python3
"""Execute shared-root receiver frames on the host and NEO-8 Z80 model."""
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
    parser.add_argument('--output',type=Path,default=Path('build/relational-methods-msx'));args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    machine=args.machine.resolve();source=REPO/'examples/relational_methods.vx';generated=directory/'projection';project=directory/'project'
    oracles={'shared':lambda n:6*(n+1)%256,
             'indexed':lambda n:(n+(3 if n%2==0 else 0))%256+256*((n+1+(3 if n%2 else 0))%256),
             'initialized':lambda n:n+300,'recursive':lambda n:(n+3)%256}
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
            invoke(n);assert read(0)==oracle(n),(name,n,read(0),oracle(n))
    (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    uint16_t n, expected;
    vx_last_result=0;
    for(n=0;n<256;++n) {
        vx_shared((uint8_t)n);
        if(vx_shared_read(0)!=((6*(n+1))&255)) {vx_last_result=1+n;__asm halt __endasm;}
        vx_indexed((uint8_t)n);
        expected=((n+((n&1) ? 0 : 3))&255)+256*((n+1+((n&1) ? 3 : 0))&255);
        if(vx_indexed_read(0)!=expected) {vx_last_result=257+n;__asm halt __endasm;}
        vx_initialized((uint8_t)n);
        if(vx_initialized_read(0)!=n+300) {vx_last_result=513+n;__asm halt __endasm;}
        vx_recursive((uint8_t)n);
        if(vx_recursive_read(0)!=((n+3)&255)) {vx_last_result=769+n;__asm halt __endasm;}
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
    print('1,024 host and 1,024 Z80 method activations passed')
    print('Evidence:',directory/'comparison.json')

if __name__=='__main__':main()
