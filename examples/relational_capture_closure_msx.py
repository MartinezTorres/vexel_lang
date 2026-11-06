#!/usr/bin/env python3
"""Check selected native captures and static interface facts on host and Z80."""
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
ORACLES={
    'receiver':lambda n:[n+600],
    'selected':lambda n:[1],
    'initialized':lambda n:[n+300],
    'schema':lambda n:[3],
    'guarded':lambda n:[(n+29)&255,(n+17)&255],
    'branch':lambda n:[n],
}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('machine',type=Path)
    parser.add_argument('--output',type=Path,default=Path('build/relational-capture-closure-msx'));args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    machine=args.machine.resolve();source=REPO/'examples/relational_capture_closure.vx';runs=[]
    sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
    for budget in (0,128):
        generated=directory/('projection-'+str(budget));project=directory/('project-'+str(budget))
        sources=[part for name in ORACLES for part in ('--source',name)]
        support.run([machine,source,*sources,'--budget','4','--frame-ram-bytes',str(budget),'--emit-c',generated])
        report=support.backend.package(generated,project,source,machine)
        assert all(not accesses for accesses in report['source_accesses'].values())
        assert 'CaptureProbeInput' not in (generated/'vx_bridges.c').read_text()
        if budget:assert report['frame_planning']['static_local_bytes']>0
        library=directory/('host-'+str(budget)+'.so')
        support.run(['cc','-O2','-std=c11','-shared','-fPIC','-I'+str(generated),'-I'+str(project/'runtime'),
                     generated/'vx_bridges.c',project/'runtime/vx_runtime.c','-o',library])
        dll=ctypes.CDLL(str(library))
        for name,oracle in ORACLES.items():
            activate=getattr(dll,'vx_'+name);activate.argtypes=[ctypes.c_uint8]
            read=getattr(dll,'vx_'+name+'_read');read.argtypes=[ctypes.c_uint16];read.restype=ctypes.c_uint32
            for n in range(256):
                activate(n);expected=oracle(n)
                assert [read(i) for i in range(len(expected))]==expected,(name,n)
        (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    uint16_t n;
    vx_last_result=0;
    for(n=0;n<256;++n) {
        vx_selected((uint8_t)n);
        if(vx_selected_read(0)!=1) {vx_last_result=1+n;__asm halt __endasm;}
        vx_initialized((uint8_t)n);
        if(vx_initialized_read(0)!=n+300) {vx_last_result=257+n;__asm halt __endasm;}
        vx_schema((uint8_t)n);
        if(vx_schema_read(0)!=3) {vx_last_result=513+n;__asm halt __endasm;}
        vx_guarded((uint8_t)n);
        if(vx_guarded_read(0)!=(uint8_t)(n+29) || vx_guarded_read(1)!=(uint8_t)(n+17)) {vx_last_result=769+n;__asm halt __endasm;}
        vx_branch((uint8_t)n);
        if(vx_branch_read(0)!=n) {vx_last_result=1025+n;__asm halt __endasm;}
        vx_receiver((uint8_t)n);
        if(vx_receiver_read(0)!=n+600) {vx_last_result=1281+n;__asm halt __endasm;}
    }
    __asm halt __endasm;
}
''')
        built=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=1000000000'])
        (directory/('build-'+str(budget)+'.log')).write_text(built.stdout+built.stderr)
        execution=json.loads((project/'build/execution.json').read_text())
        assert execution['observation']['last_result']==execution['observation']['fault']==0,execution
        runs.append({'budget':budget,'report':report,'execution':execution,
                     'execution_sha256':sha(project/'build/execution.json'),'project':str(project)})
    evidence={'machine_sha256':sha(machine),'source_sha256':sha(source),'driver_sha256':sha(__file__),
              'host_activations':3072,'z80_activations':3072,'runs':runs}
    (directory/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('3,072 host and 3,072 Z80 activations passed with automatic and exclusive local storage')
    print('Evidence:',directory/'comparison.json')

if __name__=='__main__':main()
