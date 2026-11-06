#!/usr/bin/env python3
"""Compare scalar and lifted receiver methods on host C and the NEO-8 Z80."""
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
    parser.add_argument('--output',type=Path,default=Path('build/relational-broadcast-msx'));args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    machine=args.machine.resolve();source=REPO/'examples/relational_broadcast.vx';generated=directory/'projection';project=directory/'project'
    oracles={'scalar':lambda n:(2*n+1)%256,
             'logical':lambda n:(n+7)%256+256*7,
             'indexed':lambda n:(n+(4 if n%2==0 else 0))%256+256*((n+1+(4 if n%2 else 0))%256),
             'lifted':lambda n:(n+4)%256+256*((n+6)%256)}
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
        vx_scalar((uint8_t)n);
        if(vx_scalar_read(0)!=((2*n+1)&255)) {vx_last_result=1+n;__asm halt __endasm;}
        vx_logical((uint8_t)n);
        if(vx_logical_read(0)!=((n+7)&255)+256*7) {vx_last_result=257+n;__asm halt __endasm;}
        vx_indexed((uint8_t)n);
        expected=((n+((n&1) ? 0 : 4))&255)+256*((n+1+((n&1) ? 4 : 0))&255);
        if(vx_indexed_read(0)!=expected) {vx_last_result=513+n;__asm halt __endasm;}
        vx_lifted((uint8_t)n);
        if(vx_lifted_read(0)!=((n+4)&255)+256*((n+6)&255)) {vx_last_result=769+n;__asm halt __endasm;}
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
    print('1,024 host and 1,024 Z80 operator/broadcast activations passed')
    print('Evidence:',directory/'comparison.json')

if __name__=='__main__':main()
