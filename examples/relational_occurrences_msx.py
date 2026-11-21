#!/usr/bin/env python3
"""Finite occurrence ownership, release reuse and nested callbacks on host/Z80."""
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
CASES={
    'init_read':(['first','second'],[],4),
    'init_store':(['first','second'],[],4),
    'shared':(['first','second'],[],4),
    'retained':(['first','second'],['--access-order','first:second'],4),
    'released':(['first','second'],['--event-order','first.release:second.begin'],2),
    'partial':(['first','second','third'],['--event-order','first.release:third.begin'],4),
    'callback':(['first','second'],['--event-order','first.begin:second.begin','--event-order','second.produce:first.produce'],4),
}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('machine',type=Path)
    parser.add_argument('--output',type=Path,default=Path('build/relational-occurrences-msx'));args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    machine=args.machine.resolve();source=REPO/'examples/relational_occurrences.vx';runs=[];activations=0
    sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
    for budget in (0,64):
        for case,(names,order,payload) in CASES.items():
            label=case+'-'+str(budget);generated=directory/('projection-'+label);project=directory/('project-'+label)
            targets={'shared':('summed','summed'),'init_read':('fromInitial','summed'),
                     'init_store':('storeInitial','summed')}.get(case,('entry',)*len(names))
            occurrences=[part for name,target in zip(names,targets) for part in ('--occurrence',name+':'+target)]
            contracts=['--resource-read','enter:clock']
            if case!='callback':contracts+=['--sink-no-feedback','enter']
            if case.startswith('init_'):contracts+=['--resource-read','initial:clock','--sink-no-feedback','initial']
            support.run([machine,source,*occurrences,'--mode','graph',*order,*contracts,'--budget',str(payload),
                         '--frame-ram-bytes',str(budget),'--emit-c',generated])
            report=support.backend.package(generated,project,source,machine)
            plan=json.loads((project/'generated/plan.json').read_text())
            assert plan['activation_graph']==report['activation_graph']
            assert plan['source_occurrences']==[{'name':name,'source':target,'stream':None} for name,target in zip(names,targets)]
            if case!='callback':
                assert (report['frame_planning']['static_local_bytes']>0)==bool(budget)
            else:assert report['frame_planning']['static_local_bytes']==0
            if case in ('shared','init_read','init_store'):
                shared=[f for f in report['frame_planning']['functions'] if len(f['source_occurrences'])==2]
                # Calls may have distinct specializations. Only a physically
                # shared helper needs the overlap restriction; execution below
                # checks both retained results for either valid implementation.
                for function in shared:
                    assert function['frame_policy']=='automatic' and function['overlapping_occurrences']
            if case.startswith('init_'):
                initializers=[f for f in report['frame_planning']['functions'] if f['name'].startswith('vx_ensure_')]
                assert len(initializers)==1 and initializers[0]['source_occurrences']==['vx_first']
                assert initializers[0]['frame_policy']=='automatic'
            offsets={s['name']:s['offset'] for s in report['sources']}
            assert (offsets['first']==offsets['second'])==(case=='released')
            if case=='partial':assert offsets['first']==offsets['third']
            binding='void vx_external_enter(uint32_t *out,uint8_t k) VX_ABI {'
            if case=='callback':binding+='if(k)vx_second(0);'
            binding+='out[0]=(uint8_t)(k+3);}\n'
            if case.startswith('init_'):binding+='void vx_external_initial(uint32_t *out) VX_ABI {out[0]=7;}\n'
            bind=directory/('bindings-'+label+'.c');bind.write_text('#include "vexel.h"\n'+binding)
            library=directory/('host-'+label+'.so')
            support.run(['cc','-O2','-std=c11','-shared','-fPIC','-I'+str(generated),'-I'+str(project/'runtime'),
                         generated/'vx_bridges.c',project/'runtime/vx_runtime.c',bind,'-o',library])
            dll=ctypes.CDLL(str(library))
            for name in names:
                getattr(dll,'vx_'+name).argtypes=[ctypes.c_uint8]
                getattr(dll,'vx_'+name+'_read').argtypes=[ctypes.c_uint16]
                getattr(dll,'vx_'+name+'_read').restype=ctypes.c_uint32
            wide=case in ('shared','init_read','init_store')
            oracle=(lambda n:2*n+3) if wide else (lambda n:(2*n+4)&255)
            first_oracle=(lambda n:n+18) if case=='init_read' else (lambda n:n) if case=='init_store' else oracle
            for n in range(1 if case=='callback' else 0,256):
                dll.vx_first(n)
                if case=='released':assert dll.vx_first_read(0)==first_oracle(n)
                if case!='callback':dll.vx_second(255-n)
                if case!='released':assert dll.vx_first_read(0)==first_oracle(n)
                if case=='partial':dll.vx_third(n^85)
                assert dll.vx_second_read(0)==(4 if case=='callback' else oracle(255-n))
                if case=='partial':assert dll.vx_third_read(0)==oracle(n^85)
            c='''#include "vexel.h"
volatile uint32_t vx_last_result;
'''+binding+'''void main(void) VX_ABI VX_RESIDENT {
    uint16_t n;vx_last_result=0;
'''+f'    for(n={1 if case=="callback" else 0};n<256;++n) {{\n        vx_first((uint8_t)n);\n'
            first_expected='(uint16_t)(n+18)' if case=='init_read' else 'n' if case=='init_store' else '(uint16_t)(2*n+3)' if wide else '(uint8_t)(2*n+4)'
            first=f'        if(vx_first_read(0)!={first_expected}) {{vx_last_result=1+n;__asm halt __endasm;}}\n'
            if case=='released':c+=first
            if case!='callback':c+='        vx_second((uint8_t)(255-n));\n'
            if case!='released':c+=first
            if case=='partial':c+='        vx_third((uint8_t)(n^85));\n'
            second='4' if case=='callback' else '(uint16_t)(2*(255-n)+3)' if wide else '(uint8_t)(2*(255-n)+4)'
            c+=f'        if(vx_second_read(0)!={second}) {{vx_last_result=257+n;__asm halt __endasm;}}\n'
            if case=='partial':c+='        if(vx_third_read(0)!=(uint8_t)(2*(n^85)+4)) {vx_last_result=513+n;__asm halt __endasm;}\n'
            c+='    }\n    __asm halt __endasm;\n}\n';(project/'user/main.c').write_text(c)
            built=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=1000000000'])
            (directory/('build-'+label+'.log')).write_text(built.stdout+built.stderr)
            execution=json.loads((project/'build/execution.json').read_text())
            assert execution['observation']['last_result']==execution['observation']['fault']==0,execution
            count=(255 if case=='callback' else 256)*len(names);activations+=count
            runs.append({'case':case,'budget':budget,'activations':count,'report':report,'execution':execution,
                         'execution_sha256':sha(project/'build/execution.json'),'project':str(project)})
    evidence={'machine_sha256':sha(machine),'source_sha256':sha(source),'driver_sha256':sha(__file__),
              'host_activations':activations,'z80_activations':activations,'runs':runs}
    (directory/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print(f'{activations:,} host and {activations:,} Z80 activations passed with independent occurrence ownership')
    print('Evidence:',directory/'comparison.json')

if __name__=='__main__':main()
