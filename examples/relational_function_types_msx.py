#!/usr/bin/env python3
"""Execute type-only interface queries alongside real recursive Z80 calls."""
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
    'scalar':lambda n:[n+300],
    'aggregate':lambda n:[n,(n+1)&255],
    'recursive':lambda n:[n+n%8+1],
    'selected':lambda n:[n+300,(n+1)&255],
    'partialChoice':lambda n:[n],
    'equivalentChoice':lambda n:[(n+1)&255],
    'ignoredType':lambda n:[n],
}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('machine',type=Path)
    parser.add_argument('--output',type=Path,default=Path('build/relational-function-types-msx'));args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    machine=args.machine.resolve();source=REPO/'examples/relational_function_types.vx';runs=[]
    sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
    for budget in (0,128):
        generated=directory/('projection-'+str(budget));project=directory/('project-'+str(budget))
        sources=[part for name in ORACLES for part in ('--source',name)]
        support.run([machine,source,*sources,'--budget','4','--frame-ram-bytes',str(budget),'--emit-c',generated])
        report=support.backend.package(generated,project,source,machine)
        assert all(not accesses for accesses in report['source_accesses'].values()),report['source_accesses']
        code=(generated/'vx_bridges.c').read_text()
        assert 'TypeProbeInput' not in code and 'vx_invoke_external_probe_sink' not in code
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
        vx_scalar((uint8_t)n);
        if(vx_scalar_read(0)!=n+300) {vx_last_result=1+n;__asm halt __endasm;}
        vx_aggregate((uint8_t)n);
        if(vx_aggregate_read(0)!=n || vx_aggregate_read(1)!=(uint8_t)(n+1)) {vx_last_result=257+n;__asm halt __endasm;}
        vx_recursive((uint8_t)n);
        if(vx_recursive_read(0)!=n+n%8+1) {vx_last_result=513+n;__asm halt __endasm;}
        vx_selected((uint8_t)n);
        if(vx_selected_read(0)!=n+300 || vx_selected_read(1)!=(uint8_t)(n+1)) {vx_last_result=769+n;__asm halt __endasm;}
        vx_partialChoice((uint8_t)n);
        if(vx_partialChoice_read(0)!=n) {vx_last_result=1025+n;__asm halt __endasm;}
        vx_equivalentChoice((uint8_t)n);
        if(vx_equivalentChoice_read(0)!=(uint8_t)(n+1)) {vx_last_result=1281+n;__asm halt __endasm;}
        vx_ignoredType((uint8_t)n);
        if(vx_ignoredType_read(0)!=n) {vx_last_result=1537+n;__asm halt __endasm;}
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
    activations=len(ORACLES)*256*len(runs)
    evidence={'machine_sha256':sha(machine),'source_sha256':sha(source),'driver_sha256':sha(__file__),
              'host_activations':activations,'z80_activations':activations,'runs':runs}
    (directory/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print(f'{activations:,} host and {activations:,} Z80 activations passed with automatic and exclusive local storage')
    print('Evidence:',directory/'comparison.json')

if __name__=='__main__':main()
