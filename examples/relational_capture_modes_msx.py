#!/usr/bin/env python3
"""Execute capture input/output modes with automatic and static locals."""
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
    'readcapture':(lambda n:[3*n+300],['3*n+300']),
    'overwrite':(lambda n:[n+300],['n+300']),
    'loopwrite':(lambda n:[n+300],['n+300']),
    'typefact':(lambda n:[3],['3']),
    'nestedtype':(lambda n:[3],['3']),
    'loopread':(lambda n:[4*n],['4*n']),
    'mixed':(lambda n:[3*n+300,2*n,n+300],['3*n+300','2*n','n+300']),
    'nestedwrite':(lambda n:[n+301],['n+301']),
}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('machine',type=Path)
    parser.add_argument('--output',type=Path,default=Path('build/relational-capture-modes-msx'));args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    machine=args.machine.resolve();source=REPO/'examples/relational_capture_modes.vx';runs=[]
    sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
    for budget in (0,128):
        generated=directory/('projection-'+str(budget));project=directory/('project-'+str(budget))
        sources=[part for name in ORACLES for part in ('--source',name)]
        support.run([machine,source,*sources,'--budget','12','--frame-ram-bytes',str(budget),'--emit-c',generated])
        report=support.backend.package(generated,project,source,machine)
        assert all(not accesses for accesses in report['source_accesses'].values())
        assert 'CaptureProbeInput' not in (generated/'vx_bridges.c').read_text()
        functions=report['runtime_functions']
        assert any(f['captures'] and not f['input_captures'] and not f['output_captures'] for f in functions)
        assert any(f['input_captures'] and not f['output_captures'] for f in functions)
        assert any(not f['input_captures'] and f['output_captures'] for f in functions)
        if budget:assert report['frame_planning']['static_local_bytes']>0
        library=directory/('host-'+str(budget)+'.so')
        support.run(['cc','-O2','-std=c11','-shared','-fPIC','-I'+str(generated),'-I'+str(project/'runtime'),
                     generated/'vx_bridges.c',project/'runtime/vx_runtime.c','-o',library])
        dll=ctypes.CDLL(str(library));checks=[]
        for index,(name,(oracle,expressions)) in enumerate(ORACLES.items()):
            activate=getattr(dll,'vx_'+name);activate.argtypes=[ctypes.c_uint8]
            read=getattr(dll,'vx_'+name+'_read');read.argtypes=[ctypes.c_uint16];read.restype=ctypes.c_uint32
            for n in range(256):
                activate(n);expected=oracle(n)
                assert [read(i) for i in range(len(expected))]==expected,(name,n)
            condition=' || '.join(f'vx_{name}_read({i})!=({expr})' for i,expr in enumerate(expressions))
            checks.append(f'        vx_{name}((uint8_t)n);\n        if({condition}) {{vx_last_result={1+256*index}+n;__asm halt __endasm;}}')
        (project/'user/main.c').write_text('#include "vexel.h"\nvolatile uint32_t vx_last_result;\n'
            'void main(void) VX_ABI VX_RESIDENT {\n    uint16_t n;\n    vx_last_result=0;\n'
            '    for(n=0;n<256;++n) {\n'+'\n'.join(checks)+'\n    }\n    __asm halt __endasm;\n}\n')
        built=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=1000000000'])
        (directory/('build-'+str(budget)+'.log')).write_text(built.stdout+built.stderr)
        execution=json.loads((project/'build/execution.json').read_text())
        assert execution['observation']['last_result']==execution['observation']['fault']==0,execution
        runs.append({'budget':budget,'report':report,'execution':execution,
                     'execution_sha256':sha(project/'build/execution.json'),'project':str(project)})
    evidence={'machine_sha256':sha(machine),'source_sha256':sha(source),'driver_sha256':sha(__file__),
              'host_activations':4096,'z80_activations':4096,'runs':runs}
    (directory/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('4,096 host and 4,096 Z80 activations passed with automatic and exclusive local storage')
    print('Evidence:',directory/'comparison.json')

if __name__=='__main__':main()
