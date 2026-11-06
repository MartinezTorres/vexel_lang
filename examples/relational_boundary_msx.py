#!/usr/bin/env python3
"""Persistent state, external ABI, and lazy initialization on host and Z80."""
import argparse
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
REPO=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('source_msx',REPO/'examples/relational_source_msx.py')
source_msx=importlib.util.module_from_spec(spec);spec.loader.exec_module(source_msx)
BINDINGS='''
static uint8_t sample_count, observed, observe_count;
void vx_external_sample(uint32_t *result,uint8_t tag) VX_ABI {++sample_count;result[0]=tag;}
void vx_external_observe(uint32_t *result,uint8_t value) VX_ABI {(void)result;observed=value;++observe_count;}
uint8_t samples(void) VX_ABI {return sample_count;}
uint8_t last_observed(void) VX_ABI {return observed;}
uint8_t observations(void) VX_ABI {return observe_count;}
'''
def main():
    parser=argparse.ArgumentParser();parser.add_argument('machine',type=Path);parser.add_argument('--output',type=Path,default=Path('build/relational-boundary-msx'));args=parser.parse_args()
    machine=args.machine.resolve();source=REPO/'examples/relational_boundary.vx';args.output.mkdir(parents=True,exist_ok=True)
    directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()));generated=directory/'projection';project=directory/'project'
    source_msx.run([machine,source,'--source','reset','--source','step','--budget','6','--emit-c',generated])
    report=source_msx.backend.package(generated,project,source,machine)
    binding=directory/'bindings.c';binding.write_text('#include "vexel.h"\n'+BINDINGS)
    library=directory/'execution.so';source_msx.run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(project/'generated'),'-I'+str(project/'runtime'),project/'generated/vx_bridges.c',project/'runtime/vx_runtime.c',binding,'-o',library])
    dll=ctypes.CDLL(str(library));dll.vx_step.argtypes=[ctypes.c_uint8];dll.vx_reset.argtypes=[ctypes.c_uint8];dll.vx_step_read.argtypes=[ctypes.c_uint16];dll.vx_step_read.restype=ctypes.c_uint32
    total=0;data=[2,3]
    for n in range(256):
        if n%64==0:total=n;dll.vx_reset(n)
        total+=n;data[n&1]=(data[n&1]+n)%256;dll.vx_step(n)
        assert [dll.vx_step_read(i) for i in range(3)]==[total,*data],n
        assert dll.samples()==3 and dll.last_observed()==total%256 and dll.observations()==(n+1)%256,n
    (project/'user/main.c').write_text('#include "vexel.h"\nvolatile uint32_t vx_last_result;\n'+BINDINGS+'''
void main(void) VX_ABI VX_RESIDENT {
    uint16_t n,total=0;uint8_t data[2]={2,3},index;
    vx_last_result=0;
    for(n=0;n<256;++n) {
        if((n&63)==0) {total=n;vx_reset((uint8_t)n);}
        total+=n;index=(uint8_t)(n&1);data[index]+=(uint8_t)n;vx_step((uint8_t)n);
        if(vx_step_read(0)!=total || vx_step_read(1)!=data[0] || vx_step_read(2)!=data[1] || samples()!=3 || last_observed()!=(uint8_t)total || observations()!=(uint8_t)(n+1)) {
            vx_last_result=n+1;__asm halt __endasm;
        }
    }
    __asm halt __endasm;
}
''')
    execution=source_msx.run(['make','-C',project,'-j2','simulate']);(directory/'build.log').write_text(execution.stdout+execution.stderr)
    result=json.loads((project/'build/execution.json').read_text());assert result['observation']['last_result']==result['observation']['fault']==0,result
    sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
    evidence={'format':'vexel-relational-boundary-msx-1','machine_sha256':sha(machine),'source_sha256':sha(source),'driver_sha256':sha(__file__),'host_cases':256,'z80_cases':256,'project':str(project),'report':report,'execution_sha256':sha(project/'build/execution.json')}
    (directory/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n');print('256 host and 256 Z80 activations passed; lazy state, partial writes, sink calls, and automatic frames');print('Evidence:',directory/'comparison.json')
if __name__=='__main__':main()
