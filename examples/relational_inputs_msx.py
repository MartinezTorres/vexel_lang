#!/usr/bin/env python3
"""Independent host and Z80 execution of flattened logical source interfaces."""
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
spec=importlib.util.spec_from_file_location('simulator',REPO/'backends/msx-neo8/tools/simulate.py')
simulator=importlib.util.module_from_spec(spec);spec.loader.exec_module(simulator)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('machine',type=Path)
    parser.add_argument('--output',type=Path,default=Path('build/relational-inputs-msx'));args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    machine=args.machine.resolve();source=REPO/'examples/relational_inputs.vx';generated=directory/'projection'
    names=['records','matrix','schema','small','empty']
    support.run([machine,source,*[arg for name in names for arg in ['--source',name]],'--budget','8','--emit-c',generated])
    project=directory/'valid';report=support.backend.package(generated,project,source,machine)
    library=directory/'host.so'
    support.run(['cc','-O2','-std=c11','-shared','-fPIC','-I'+str(generated),'-I'+str(project/'runtime'),
                 generated/'vx_bridges.c',project/'runtime/vx_runtime.c','-o',library])
    dll=ctypes.CDLL(str(library));u8=ctypes.c_uint8;i8=ctypes.c_int8;u16=ctypes.c_uint16;i16=ctypes.c_int16
    cases={
        'records':([u8,i16,u8,i16,u16,u8],lambda n:[n,1000-n,(n+1)%256,-n,300+n,n],lambda n:[(2*n)%256,-n-1,300+n,n]),
        'matrix':([u8,u8,u8,u8,i8,u8],lambda n:[n,(n+1)%256,(n+2)%256,(n+3)%256,n%128-64,n],lambda n:[(n+(n%2)*2+(n//2)%2)%256,n%128-64]),
        'schema':([u8]*4,lambda n:[3,n,(n+1)%256,(n+2)%256],lambda n:[(3*n+3)%256]),
        'small':([u8,u8,i8],lambda n:[n%2,n%8,n%32-16],lambda n:[n%8+n%32-16 if n%2 else 0]),
        'empty':([u8],lambda n:[n],lambda n:[n]),
    }
    for name,(types,inputs,oracle) in cases.items():
        invoke,read=getattr(dll,'vx_'+name),getattr(dll,'vx_'+name+'_read');invoke.argtypes=types
        read.argtypes=[ctypes.c_uint16];read.restype=ctypes.c_uint32
        for n in range(256):
            invoke(*inputs(n));expected=[v&0xffffffff for v in oracle(n)]
            assert [read(i) for i in range(len(expected))]==expected,(name,n,expected)
    (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    uint16_t n;int16_t signed_value;
    vx_last_result=0;
    for(n=0;n<256;++n) {
        vx_records((uint8_t)n,1000-n,(uint8_t)(n+1),-(int16_t)n,300+n,(uint8_t)n);
        if(vx_records_read(0)!=((2*n)&255) || vx_records_read(1)!=(uint32_t)(int32_t)(-(int16_t)n-1) ||
           vx_records_read(2)!=300+n || vx_records_read(3)!=n) {vx_last_result=1+n;__asm halt __endasm;}
        signed_value=(int16_t)(n&127)-64;
        vx_matrix((uint8_t)n,(uint8_t)(n+1),(uint8_t)(n+2),(uint8_t)(n+3),(int8_t)signed_value,(uint8_t)n);
        if(vx_matrix_read(0)!=((n+((n&1)*2)+((n>>1)&1))&255) || vx_matrix_read(1)!=(uint32_t)(int32_t)signed_value) {
            vx_last_result=257+n;__asm halt __endasm;
        }
        vx_schema(3,(uint8_t)n,(uint8_t)(n+1),(uint8_t)(n+2));
        if(vx_schema_read(0)!=((n+n+n+3)&255)) {vx_last_result=513+n;__asm halt __endasm;}
        signed_value=(int16_t)(n&31)-16;
        vx_small(n&1,n&7,(int8_t)signed_value);
        signed_value=(n&1) ? signed_value+(int16_t)(n&7) : 0;
        if(vx_small_read(0)!=(uint32_t)(int32_t)signed_value) {vx_last_result=769+n;__asm halt __endasm;}
        vx_empty((uint8_t)n);
        if(vx_empty_read(0)!=n) {vx_last_result=1025+n;__asm halt __endasm;}
    }
    __asm halt __endasm;
}
''')
    def execute(project):
        built=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=500000000'])
        (project/'build-driver.log').write_text(built.stdout+built.stderr)
        return json.loads((project/'build/execution.json').read_text())
    execution=execute(project)
    assert execution['observation']['last_result']==execution['observation']['fault']==0,execution
    traps=[]
    for name,call,reason in [('schema','vx_schema(2,1,2,3)',19),('range','vx_small(2,0,0)',16)]:
        invalid=directory/name;support.backend.package(generated,invalid,source,machine)
        (invalid/'user/main.c').write_text('#include "vexel.h"\nvolatile uint32_t vx_last_result;\n'
            'void main(void) VX_ABI VX_RESIDENT {vx_last_result=99;'+call+';vx_last_result=100;__asm halt __endasm;}\n')
        built=support.run(['make','-C',invalid,'-j2','all','build/neo8-runner'])
        (invalid/'build-driver.log').write_text(built.stdout+built.stderr)
        rom=invalid/'build/game.rom';symbols=simulator.symbols(Path(str(rom)+'.symbols.map'))
        plan=json.loads((invalid/'generated/plan.json').read_text());snapshot=invalid/'build/ram.bin'
        observed=support.run([invalid/'build/neo8-runner','--rom',rom,'--dump-ram',snapshot,
            '--max-ticks','10000000','--stack-floor',str(plan['resident_limit']),'--himem','62336',
            '--irq-period','0','--shadow',str(symbols['_vx_segment_b']),'--fault',str(symbols['_vx_fault'])])
        trapped=json.loads(observed.stdout);at=symbols['_vx_last_result']-0xc000
        assert trapped['fault']==reason and int.from_bytes(snapshot.read_bytes()[at:at+4],'little')==99,trapped
        evidence={'reason':reason,'observation':trapped,'project':str(invalid),
            'rom_sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),
            'runner_sha256':hashlib.sha256((invalid/'build/neo8-runner').read_bytes()).hexdigest()}
        (invalid/'build/expected-trap.json').write_text(json.dumps(evidence,indent=2)+'\n')
        traps.append(evidence)
    sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
    evidence={'machine_sha256':sha(machine),'source_sha256':sha(source),'driver_sha256':sha(__file__),
              'host_activations':1280,'z80_activations':1280,'traps':traps,'report':report,'project':str(project),
              'execution_sha256':sha(project/'build/execution.json')}
    (directory/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('1,280 host and 1,280 Z80 activations passed; schema and range traps checked')
    print('Evidence:',directory/'comparison.json')

if __name__=='__main__':main()
