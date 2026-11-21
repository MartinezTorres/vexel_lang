#!/usr/bin/env python3
"""Check loop initialization and parallel frame updates on host and Z80."""
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
    'partialGuard':lambda n:[2 if n==1 else 0],
    'nestedPartialGuard':lambda n:[2 if n==1 else 0],
    'nativePartialGuard':lambda n:[2 if n==1 else 0],
    'countedWrite':lambda n:[n],
    'countedNested':lambda n:[(n+2)&255],
    'countedContinue':lambda n:[n],
    'countedReads':lambda n:[(2*n+1)&255],
    'countedGuarded':lambda n:[max((n&7)-1,0)],
    'first':lambda n:[n+3],
    'guarded':lambda n:[n+3 if n<128 else 0],
    'nested':lambda n:[n+4],
    'captured':lambda n:[n+300],
    'exits':lambda n:[7 if n<128 else n+300],
    'aggregate':lambda n:[(n-131)&0xffffffff,(-3)&0xffffffff],
}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('machine',type=Path)
    parser.add_argument('--output',type=Path,default=Path('build/relational-loop-init-msx'));args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    machine=args.machine.resolve();source=REPO/'examples/relational_loop_initialization.vx';runs=[]
    sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
    for budget in (0,128):
        generated=directory/('projection-'+str(budget));project=directory/('project-'+str(budget))
        sources=[part for name in ORACLES for part in ('--source',name)]
        # The nested readiness trace needs smaller groups to fit an 8 KiB bank.
        support.run([machine,source,*sources,'--budget','4','--frame-ram-bytes',str(budget),
                     '--prefer','bank_chunk_operations=16','--emit-c',generated])
        report=support.backend.package(generated,project,source,machine)
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
        vx_first((uint8_t)n);
        if(vx_first_read(0)!=n+3) {vx_last_result=1+n;__asm halt __endasm;}
        vx_guarded((uint8_t)n);
        if(vx_guarded_read(0)!=(n<128 ? n+3 : 0)) {vx_last_result=257+n;__asm halt __endasm;}
        vx_nested((uint8_t)n);
        if(vx_nested_read(0)!=n+4) {vx_last_result=513+n;__asm halt __endasm;}
        vx_captured((uint8_t)n);
        if(vx_captured_read(0)!=n+300) {vx_last_result=769+n;__asm halt __endasm;}
        vx_exits((uint8_t)n);
        if(vx_exits_read(0)!=(n<128 ? 7 : n+300)) {vx_last_result=1025+n;__asm halt __endasm;}
        vx_aggregate((uint8_t)n);
        if(vx_aggregate_read(0)!=(uint32_t)(int32_t)((int16_t)n-131) ||
           vx_aggregate_read(1)!=(uint32_t)(int32_t)-3) {vx_last_result=1281+n;__asm halt __endasm;}
        vx_partialGuard((uint8_t)n);
        if(vx_partialGuard_read(0)!=(n==1 ? 2 : 0)) {vx_last_result=2817+n;__asm halt __endasm;}
        vx_nestedPartialGuard((uint8_t)n);
        if(vx_nestedPartialGuard_read(0)!=(n==1 ? 2 : 0)) {vx_last_result=3073+n;__asm halt __endasm;}
        vx_nativePartialGuard((uint8_t)n);
        if(vx_nativePartialGuard_read(0)!=(n==1 ? 2 : 0)) {vx_last_result=3329+n;__asm halt __endasm;}
        vx_countedWrite((uint8_t)n);
        if(vx_countedWrite_read(0)!=n) {vx_last_result=1537+n;__asm halt __endasm;}
        vx_countedNested((uint8_t)n);
        if(vx_countedNested_read(0)!=(uint8_t)(n+2)) {vx_last_result=1793+n;__asm halt __endasm;}
        vx_countedContinue((uint8_t)n);
        if(vx_countedContinue_read(0)!=n) {vx_last_result=2049+n;__asm halt __endasm;}
        vx_countedReads((uint8_t)n);
        if(vx_countedReads_read(0)!=(uint8_t)(2*n+1)) {vx_last_result=2305+n;__asm halt __endasm;}
        vx_countedGuarded((uint8_t)n);
        if(vx_countedGuarded_read(0)!=((n&7)==0 ? 0 : (n&7)-1)) {vx_last_result=2561+n;__asm halt __endasm;}

    }
    __asm halt __endasm;
}
''')
        # This multi-entry project can wait behind other serialized SDCC
        # builds. Its compiler invocations retain the 180-second guard.
        built=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=1000000000'],timeout=1200)
        (directory/('build-'+str(budget)+'.log')).write_text(built.stdout+built.stderr)
        execution=json.loads((project/'build/execution.json').read_text())
        assert execution['observation']['last_result']==execution['observation']['fault']==0,execution
        runs.append({'budget':budget,'report':report,'execution':execution,
                     'execution_sha256':sha(project/'build/execution.json'),'project':str(project)})
    activations=256*len(ORACLES)*len(runs)
    evidence={'machine_sha256':sha(machine),'source_sha256':sha(source),'driver_sha256':sha(__file__),
              'host_activations':activations,'z80_activations':activations,'runs':runs}
    (directory/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print(f'{activations:,} host and {activations:,} Z80 activations passed with automatic and exclusive local storage')
    print('Evidence:',directory/'comparison.json')

if __name__=='__main__':main()
