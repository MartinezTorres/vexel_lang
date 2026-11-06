#!/usr/bin/env python3
"""Independent host and Z80 checks for persistent lexical module instances."""
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
    parser.add_argument('--output',type=Path,default=Path('build/relational-modules-msx'));args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    machine=args.machine.resolve();source=REPO/'examples/relational_modules.vx';generated=directory/'projection';project=directory/'project'
    support.run([machine,source,'--source','entry','--budget','6','--emit-c',generated])
    report=support.backend.package(generated,project,source,machine)
    assert len(report['boundary_storage'])==2,report
    bindings='#include "vexel.h"\nuint8_t initial_calls;void vx_external_initial(uint32_t *out) VX_ABI VX_RESIDENT {++initial_calls;out[0]=0;}\n'
    harness=directory/'bindings.c';harness.write_text(bindings);library=directory/'host.so'
    support.run(['cc','-O2','-std=c11','-shared','-fPIC','-I'+str(generated),'-I'+str(project/'runtime'),
                 generated/'vx_bridges.c',project/'runtime/vx_runtime.c',harness,'-o',library])
    dll=ctypes.CDLL(str(library));dll.vx_entry.argtypes=[ctypes.c_uint8]
    dll.vx_entry_read.argtypes=[ctypes.c_uint16];dll.vx_entry_read.restype=ctypes.c_uint32;total=0
    for n in range(256):
        first=(total+n)%256;total=(total+2*n)%256;dll.vx_entry(n)
        assert [dll.vx_entry_read(i) for i in range(3)]==[first,total,(n+1)%256],n
    assert ctypes.c_uint8.in_dll(dll,'initial_calls').value==2
    (project/'user/main.c').write_text(bindings+'''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    uint16_t n,total,first;
    initial_calls=0;vx_last_result=0;total=0;
    for(n=0;n<256;++n) {
        first=(total+n)&255;total=(total+2*n)&255;
        vx_entry((uint8_t)n);
        if(vx_entry_read(0)!=first || vx_entry_read(1)!=total || vx_entry_read(2)!=((n+1)&255)) {
            vx_last_result=1+n;__asm halt __endasm;
        }
    }
    if(initial_calls!=2) vx_last_result=257;
    __asm halt __endasm;
}
''')
    build=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=250000000']);(directory/'build.log').write_text(build.stdout+build.stderr)
    result=json.loads((project/'build/execution.json').read_text())
    assert result['observation']['last_result']==result['observation']['fault']==0,result
    sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
    evidence={'machine_sha256':sha(machine),'source_sha256':sha(source),'driver_sha256':sha(__file__),
              'host_activations':256,'z80_valid_activations':256,'report':report,'project':str(project),
              'execution_sha256':sha(project/'build/execution.json')}
    (directory/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('256 host and 256 Z80 module activations passed; exactly two persistent initializations')
    print('Evidence:',directory/'comparison.json')

if __name__=='__main__':main()
