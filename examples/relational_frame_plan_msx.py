#!/usr/bin/env python3
"""Compare automatic and exclusive local storage on the same recursive graph."""
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
spec=importlib.util.spec_from_file_location('checker',REPO/'backends/msx-neo8/tools/check_project.py')
checker=importlib.util.module_from_spec(spec);spec.loader.exec_module(checker)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('machine',type=Path)
    parser.add_argument('--output',type=Path,default=Path('build/relational-frame-plan-msx'));args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    machine=args.machine.resolve();source=REPO/'examples/relational_frame_plan.vx';runs=[]
    sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
    for budget in (0,128):
        generated=directory/('projection-'+str(budget));project=directory/('project-'+str(budget))
        support.run([machine,source,'--source','entry','--budget','2','--frame-ram-bytes',str(budget),'--emit-c',generated])
        report=support.backend.package(generated,project,source,machine);plan=report['frame_planning']
        assert plan['static_local_bytes']<=budget
        assert all(fn['frame_policy']=='automatic' for fn in plan['functions'] if fn['recursive'])
        if budget:assert plan['static_local_bytes']>0
        library=directory/('host-'+str(budget)+'.so')
        support.run(['cc','-O2','-std=c11','-shared','-fPIC','-I'+str(generated),'-I'+str(project/'runtime'),
                     generated/'vx_bridges.c',project/'runtime/vx_runtime.c','-o',library])
        dll=ctypes.CDLL(str(library));dll.vx_entry.argtypes=[ctypes.c_uint8]
        dll.vx_entry_read.argtypes=[ctypes.c_uint16];dll.vx_entry_read.restype=ctypes.c_uint32
        for n in range(256):
            dll.vx_entry(n);assert dll.vx_entry_read(0)==3*((n%8)*(n%8+1)//2+n),n
        (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    uint16_t n,k,expected;
    vx_last_result=0;
    for(n=0;n<256;++n) {
        expected=n+n+n;
        for(k=1;k<=(n&7);++k) expected+=k+k+k;
        vx_entry((uint8_t)n);
        if(vx_entry_read(0)!=expected) {vx_last_result=1+n;__asm halt __endasm;}
    }
    __asm halt __endasm;
}
''')
        built=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=500000000'])
        (directory/('build-'+str(budget)+'.log')).write_text(built.stdout+built.stderr)
        execution=json.loads((project/'build/execution.json').read_text())
        assert execution['observation']['last_result']==execution['observation']['fault']==0,execution
        rejections=[]
        if budget:
            # Changing an automatic callee can also invalidate an ancestor's
            # exclusive proof. Module names come from the plan, not prefixes.
            module=next(m['module'] for m in report['bank_modules'] if m['frame_policy']=='automatic')
            replacement=project/('user/'+module+'.c');replacement.write_text((project/('generated/'+module+'.c')).read_text())
            try:
                try:checker.check(project/'generated/plan.json',project/'build/game.rom',project/'build/rejected-override.json')
                except ValueError as error:
                    assert 'closed-call-graph' in str(error),error;rejections.append(str(error))
                else:raise AssertionError('override accepted under a closed frame proof')
            finally:replacement.unlink()
            original=project/('generated/'+module+'.c');contents=original.read_bytes();original.write_bytes(contents+b'\n')
            try:
                try:checker.check(project/'generated/plan.json',project/'build/game.rom',project/'build/rejected-edit.json')
                except ValueError as error:
                    assert 'closed-call-graph' in str(error),error;rejections.append(str(error))
                else:raise AssertionError('edited callee accepted under a closed frame proof')
            finally:original.write_bytes(contents)
        runs.append({'budget':budget,'frame_planning':plan,'execution':execution,
                     'execution_sha256':sha(project/'build/execution.json'),'project':str(project),'rejected_edits':rejections})
    evidence={'machine_sha256':sha(machine),'source_sha256':sha(source),'driver_sha256':sha(__file__),
              'host_activations':512,'z80_activations':512,'runs':runs}
    (directory/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('512 host and 512 Z80 activations passed; automatic and exclusive local policies agree')
    print('Observed stack bytes:',[r['execution']['observation']['observed_stack_bytes'] for r in runs])
    print('Evidence:',directory/'comparison.json')

if __name__=='__main__':main()
