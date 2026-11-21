#!/usr/bin/env python3
"""Proved 32-bit identities through the host and NEO8 Z80 backends."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import tempfile
import relational_source_msx as support

ROOT=Path(__file__).resolve().parents[1]
EDGES=(0,1,127,128,255,256,65535,65536,0x7fffffff,0x80000000,0xffffffff)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('solver',type=Path)
    parser.add_argument('--output',type=Path,default=Path('build/modular-control-msx'))
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    solver=args.solver.resolve();source=ROOT/'examples/modular_control.vx'
    generated=directory/'generated';project=directory/'project'
    resolved=support.run([solver,source,'--program','--emit-c',generated])
    report=json.loads(resolved.stdout);assert report['status']=='resolved',report
    support.backend.package(generated,project,source,solver)
    library=directory/'host.so'
    support.run(['cc','-std=c11','-O2','-shared','-fPIC','-fsanitize=undefined','-fno-sanitize-recover=all',
                 '-I'+str(generated),'-I'+str(project/'runtime'),generated/'vx_bridges.c',
                 project/'runtime/vx_runtime.c','-o',library])
    dll=ctypes.CDLL(str(library));dll.vx_algebra.argtypes=[ctypes.c_uint32]*3
    dll.vx_algebra_read.argtypes=[ctypes.c_uint16];dll.vx_algebra_read.restype=ctypes.c_uint32
    cases=[(a,b,c) for a in EDGES for b in EDGES for c in EDGES]
    for a,b,c in cases:
        dll.vx_algebra(a,b,c)
        assert dll.vx_algebra_read(0)==(a+1)&0xffffffff,(a,b,c)
    values=','.join(str(n)+'UL' for n in EDGES)
    (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
static const uint32_t edges[]={'''+values+'''};
void main(void) VX_ABI VX_RESIDENT {
    uint8_t a,b,c;
    vx_last_result=0;
    for(a=0;a<sizeof(edges)/sizeof(edges[0]);++a)
        for(b=0;b<sizeof(edges)/sizeof(edges[0]);++b)
            for(c=0;c<sizeof(edges)/sizeof(edges[0]);++c) {
                vx_algebra(edges[a],edges[b],edges[c]);
                if(vx_algebra_read(0)!=(uint32_t)(edges[a]+1UL)) {
                    vx_last_result=1;
                    __asm halt __endasm;
                }
            }
    __asm halt __endasm;
}
''')
    built=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=100000000',
                      'SDCCFLAGS=-mz80 --std-sdcc11 --sdcccall 0 --opt-code-speed --max-allocs-per-node 2000'])
    (directory/'build.log').write_text(built.stdout+built.stderr)
    execution=json.loads((project/'build/execution.json').read_text())
    assert execution['observation']['last_result']==execution['observation']['fault']==0,execution
    sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
    evidence={'solver_sha256':sha(solver),'source_sha256':sha(source),'driver_sha256':sha(__file__),
              'host_cases':len(cases),'z80_cases':len(cases),'report':report,
              'execution_sha256':sha(project/'build/execution.json')}
    (directory/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('Modular identities:',len(cases),'host and Z80 boundary cases passed')
    print('Evidence:',directory/'comparison.json')

if __name__=='__main__':main()
