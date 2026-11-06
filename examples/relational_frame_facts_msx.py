#!/usr/bin/env python3
"""Execute native slots with compile-time return and captured record fields."""
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
    parser.add_argument('--output',type=Path,default=Path('build/relational-frame-facts-msx'));args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    machine=args.machine.resolve();source=REPO/'examples/relational_frame_facts.vx';generated=directory/'projection';project=directory/'project'
    oracles={'returned':lambda n:(n+1+b'AZ'[n%2]+18)%256,
             'captured':lambda n:(2*n+89)%256,
             'recursive':lambda n:(n%8)*(n%8+1)//2+b'ABC'[n%3],
             'shared':lambda n:sum(b'ABC'[i%3] for i in range(1,n%8+1))}
    support.run([machine,source,*[arg for name in oracles for arg in ['--source',name]],'--budget','2','--emit-c',generated])
    report=support.backend.package(generated,project,source,machine);library=directory/'host.so'
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
    uint16_t n,k,expected;uint8_t j;
    vx_last_result=0;
    for(n=0;n<256;++n) {
        expected=(n+1+((n&1) ? 90 : 65)+18)&255;
        vx_returned((uint8_t)n);
        if(vx_returned_read(0)!=expected) {vx_last_result=1+n;__asm halt __endasm;}
        vx_captured((uint8_t)n);
        if(vx_captured_read(0)!=((2*n+89)&255)) {vx_last_result=257+n;__asm halt __endasm;}
        k=n;while(k>=3) k-=3;expected=65+k;
        for(k=1;k<=(n&7);++k) expected+=k;
        vx_recursive((uint8_t)n);
        if(vx_recursive_read(0)!=expected) {vx_last_result=513+n;__asm halt __endasm;}
        expected=0;j=1;for(k=1;k<=(n&7);++k) {expected+=65+j;if(++j==3) j=0;}
        vx_shared((uint8_t)n);
        if(vx_shared_read(0)!=expected) {vx_last_result=769+n;__asm halt __endasm;}
    }
    __asm halt __endasm;
}
''')
    built=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=500000000'])
    (directory/'build.log').write_text(built.stdout+built.stderr)
    execution=json.loads((project/'build/execution.json').read_text())
    assert execution['observation']['last_result']==execution['observation']['fault']==0,execution
    sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
    evidence={'machine_sha256':sha(machine),'source_sha256':sha(source),'driver_sha256':sha(__file__),
              'host_activations':1024,'z80_activations':1024,'report':report,'project':str(project),
              'execution_sha256':sha(project/'build/execution.json')}
    (directory/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('1,024 host and 1,024 Z80 activations passed with static result and capture facts')
    print('Evidence:',directory/'comparison.json')

if __name__=='__main__':main()
