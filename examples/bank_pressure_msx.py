#!/usr/bin/env python3
"""Exercise measured partition retries and run the selected editable project."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'experimental/relational'))
from realize_msx import realize


def command(args, log, cwd=None):
    result=subprocess.run([str(x) for x in args],cwd=cwd,capture_output=True,text=True)
    log.write_text(result.stdout+result.stderr)
    if result.returncode: raise RuntimeError('command failed; see '+str(log))


def expected(n):
    for _ in range(160):
        before=n;n=0
        for j in range(16):n^=((before+j)&255)<<(j%3)
        n&=255
    return n


def check(solver, project):
    if not project.exists():
        realize(solver,ROOT/'examples/bank_pressure.vx',project,
                ['--max-work','100000000','--max-nodes','2000000'],True)
    attempts=json.loads((project/'realization.json').read_text())
    if len(attempts)<2 or not attempts[-1].get('selected') or not any(a.get('oversized_modules') for a in attempts[:-1]):
        raise AssertionError('the workload did not exercise measured rejection and retry')
    harness='''#include "vexel.h"
volatile uint32_t vx_last_result;
#ifdef __SDCC
void main(void) VX_ABI VX_RESIDENT {
    vx_entry(127); vx_last_result=vx_entry_read(0);
    __asm halt __endasm;
}
#endif
'''
    (project/'user/main.c').write_text(harness)
    (project/'user/stack-contracts.json').write_text('{"environment_bytes":64,"functions":{}}\n')
    generated=project/'generated';runtime=project/'runtime';library=project/'build/host.so'
    command(['cc','-O2','-std=c11','-fPIC','-shared','-I'+str(generated),'-I'+str(runtime),
             generated/'vx_banked.c',*sorted(generated.glob('vxp_*.c')),runtime/'vx_runtime.c',
             '-o',library],project/'build/host-build.log')
    dll=ctypes.CDLL(str(library));dll.vx_entry.argtypes=[ctypes.c_uint8];dll.vx_entry_read.restype=ctypes.c_uint32
    cases=[]
    for n in (0,1,31,127,128,255):
        dll.vx_entry(n);actual=dll.vx_entry_read(0)
        if actual!=expected(n): raise AssertionError(('host',n,actual,expected(n)))
        cases.append({'input':n,'output':actual})
    command(['make','simulate','SIMFLAGS=--max-ticks 200000000 --irq-period 3000'],project/'build/target-test.log',project)
    execution=json.loads((project/'build/execution.json').read_text())
    observation=execution['observation']
    if observation['last_result']!=expected(127): raise AssertionError(('Z80',observation))
    measured=json.loads((project/'build/resolution.json').read_text())
    chunks=[area for area in measured['areas'] if area['module'].startswith('vxp_chunk_') and area['bytes'] and area['area'].lstrip('_')=='CODE']
    fetched=set(observation['fetched_segments'])
    if len(chunks)<2 or any(area['segment'] not in fetched for area in chunks):
        raise AssertionError('Z80 execution did not visit every continuation bank')
    result={'source_sha256':hashlib.sha256((ROOT/'examples/bank_pressure.vx').read_bytes()).hexdigest(),
            'provenance':json.loads((generated/'provenance.json').read_text()),
            'attempts':attempts,'host':cases,'execution':observation,'rom_sha256':measured['rom_sha256'],
            'analysis_sha256':{k:v for k,v in measured['input_sha256'].items() if k.startswith('tools/')},
            'resident_bytes':measured['resident_bytes'],'stack_analysis':measured['stack_analysis'],
            'continuation_code_bytes':[area['bytes'] for area in chunks]}
    (project/'build/comparison.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Bank-pressure workload passed: '+str(project/'build/comparison.json'))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('solver',type=Path)
    parser.add_argument('--project',type=Path,help='existing realization or new editable project directory')
    args=parser.parse_args()
    if args.project is None:
        (ROOT/'build/bank-pressure').mkdir(parents=True,exist_ok=True)
        args.project=Path(tempfile.mkdtemp(prefix='run-',dir=ROOT/'build/bank-pressure'))/'project'
    check(args.solver.resolve(),args.project.resolve())
