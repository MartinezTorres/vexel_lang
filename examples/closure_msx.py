#!/usr/bin/env python3
"""One workload through source solving, leased C ABI, bank cuts and real Z80 code."""
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
from project_msx import package


def command(args, log, cwd=None):
    result=subprocess.run([str(x) for x in args],cwd=cwd,capture_output=True,text=True)
    log.write_text(result.stdout+result.stderr)
    if result.returncode: raise RuntimeError('command failed; see '+str(log))
    return result.stdout


def check(solver, output):
    output.mkdir(parents=True,exist_ok=True)
    work=Path(tempfile.mkdtemp(prefix='run-',dir=output)).resolve()
    reports=[]
    for mapper in (False,True):
        directory=work/('mapper' if mapper else 'base');directory.mkdir()
        source=directory/'application.vx';text=(ROOT/'examples/closure.vx').read_text()
        if not mapper: text=text.replace('[[mapper]] ','').replace('require(mapper_segments, 2)','require(mapper_segments, 0)')
        source.write_text(text);generated=directory/'c';project=directory/'project'
        plan=json.loads(command([solver,source,'--require','stack_proof=1','--emit-c',generated],directory/'solve.json'))
        package(generated,project,source,solver)
        # The model's ENASLT stub has no stack-local storage. This contract is
        # about that stub, not a claim concerning arbitrary installed BIOS ROMs.
        (project/'user/stack-contracts.json').write_text(json.dumps({'environment_bytes':64,'functions':{'0x0024':0}})+'\n')
        harness='''#include "vexel.h"
volatile uint32_t vx_last_result;
uint32_t workload(void) VX_ABI VX_RESIDENT {
    uint8_t n; uint32_t first, second, checksum=0;
MAPPER_INIT
    for (n=0; n<32; ++n) {
        first=vx_frame_submit(n); second=vx_frame_submit((uint8_t)(n+1)&31u);
        if (!first || !second || first==second) vx_trap(90);
        checksum=(checksum<<5)+checksum+vx_frame_read_lease(first,0);
        checksum=(checksum<<5)+checksum+vx_frame_read_lease(second,1);
        if (!vx_frame_release(first) || vx_frame_release(first) || !vx_frame_release(second)) vx_trap(91);
    }
MAPPER_CROSSING
    return checksum;
}
#ifdef __SDCC
void main(void) VX_ABI VX_RESIDENT { vx_last_result=workload(); __asm halt __endasm; }
#endif
'''.replace('MAPPER_INIT','    vx_mapper_init(3,1,2);' if mapper else '').replace('MAPPER_CROSSING',
    '    vx_mapper_write(16383UL,11); vx_mapper_write(16384UL,29);\n'
    '    if (vx_mapper_read(16383UL)!=11 || vx_mapper_read(16384UL)!=29) vx_trap(92);' if mapper else '')
        (project/'user/main.c').write_text(harness)
        runtime=project/'runtime';library=directory/'host.so'
        command(['cc','-O2','-std=c11','-fPIC','-shared',f'-DVX_MAPPER_SEGMENTS={2 if mapper else 0}',
            '-I'+str(project/'generated'),'-I'+str(runtime),project/'generated/vx_banked.c',
            *sorted((project/'generated').glob('vxp_*.c')),runtime/'vx_runtime.c',runtime/'vx_mapper.c',project/'user/main.c',
            '-o',library],directory/'host-build.log')
        dll=ctypes.CDLL(str(library));dll.workload.restype=ctypes.c_uint32
        expected=0
        for n in range(32):
            value=sum(x for x in range(n,n+4) if x!=2)+(n%8)*(n%8+1)//2
            expected=((expected*33+value)*33+(n+1)%32)&0xffffffff
        actual=dll.workload()
        if actual!=expected: raise AssertionError(('host',actual,expected))
        simflags='--max-ticks 100000000 --irq-period 3000'+(' --ram-segments 3' if mapper else '')
        command(['make','simulate','SIMFLAGS='+simflags],directory/'target.log',project)
        execution=json.loads((project/'build/execution.json').read_text())
        measured=json.loads((project/'build/resolution.json').read_text())
        if execution['observation']['last_result']!=expected: raise AssertionError(('Z80',execution,expected))
        # Required proofs must reject missing premises and oversized bounds.
        # Use a separate report so negative checks cannot replace valid evidence.
        contracts=project/'user/stack-contracts.json'; saved=contracts.read_text()
        checker=[sys.executable,project/'tools/check_project.py',project/'generated/plan.json',
                 project/'build/game.rom',directory/'rejected-resolution.json']
        try:
            for allowance, functions, diagnostic in (
                [(64,{},'required stack proof is open')] if mapper else []
            )+[(2048,{'0x0024':0},'exceeds reservation')]:
                contracts.write_text(json.dumps({'environment_bytes':allowance,'functions':functions})+'\n')
                rejected=subprocess.run([str(x) for x in checker],capture_output=True,text=True)
                if rejected.returncode==0 or diagnostic not in rejected.stderr:
                    raise AssertionError(('stack rejection',rejected.stdout,rejected.stderr))
                (directory/('stack-missing-contract.log' if allowance==64 else 'stack-budget.log')).write_text(rejected.stderr)
                if (directory/'rejected-resolution.json').exists(): raise AssertionError('failed proof published a report')
        finally:
            contracts.write_text(saved)
        reports.append({'profile':'16k-plus-mapper' if mapper else '16k','checksum':expected,
                        'activations':64,'mapper_bytes':plan['mapper_storage']['bytes'],
                        'bank_modules':len(plan['bank_modules']),'resident_bytes':measured['resident_bytes'],
                        'analysis_sha256':{k:v for k,v in measured['input_sha256'].items() if k.startswith('tools/')},
                        'stack_analysis':measured['stack_analysis'],'execution':execution['observation'],
                        'project':str(project)})
    result={'source_sha256':hashlib.sha256((ROOT/'examples/closure.vx').read_bytes()).hexdigest(),
            'solver_sha256':hashlib.sha256(Path(solver).read_bytes()).hexdigest(),'profiles':reports}
    (work/'comparison.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Closure workload passed: '+str(work/'comparison.json'))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('solver',type=Path)
    parser.add_argument('--output',type=Path,default=ROOT/'build/closure-msx');args=parser.parse_args()
    check(args.solver.resolve(),args.output.resolve())
