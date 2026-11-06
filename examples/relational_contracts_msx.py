#!/usr/bin/env python3
"""Execute external resource/feedback assumptions and retained result ordering."""
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
BINDINGS='''static volatile uint8_t device;
void vx_external_get(uint32_t *out,VX_STABLE uint8_t n) VX_ABI {out[0]=(uint8_t)(device+n);}
void vx_external_put(uint32_t *out,uint8_t n) VX_ABI {(void)out;device=n;}
void vx_external_exchange(uint32_t *out,VX_STABLE uint8_t a,VX_STABLE uint8_t b,VX_STABLE uint8_t count,
 VX_STABLE uint8_t x,VX_STABLE int16_t y,VX_STABLE uint8_t z,VX_STABLE int16_t w) VX_ABI {
 out[0]=(uint8_t)(a+x);out[1]=(uint8_t)(b+z+count);out[2]=(uint32_t)(int32_t)y;out[3]=(uint32_t)(int32_t)w;
}
'''

def main():
    parser=argparse.ArgumentParser();parser.add_argument('machine',type=Path)
    parser.add_argument('--output',type=Path,default=Path('build/relational-contracts-msx'));args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    machine=args.machine.resolve();source=REPO/'examples/relational_contracts.vx';runs=[]
    sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
    for mode in ('serial','overlap'):
        generated=directory/('projection-'+mode);project=directory/('project-'+mode)
        facts=['--resource-read','get:bus','--resource-write','put:bus','--resource-read','exchange:packet']
        facts += [arg for name in ('get','put','exchange') for arg in ['--sink-no-feedback',name]]
        if mode=='overlap':facts+=['--access-order','first:second']
        support.run([machine,source,'--source','first','--source','arrays','--source','second',
                     '--mode',mode,'--budget','12','--frame-ram-bytes','128','--emit-c',generated,*facts])
        report=support.backend.package(generated,project,source,machine)
        if mode=='serial':assert report['frame_planning']['static_local_bytes']>0
        else:assert report['extent']==12 and report['access_order']==[['first','second']],report
        bindings=directory/('bindings-'+mode+'.c');bindings.write_text('#include "vexel.h"\n'+BINDINGS)
        library=directory/('host-'+mode+'.so')
        support.run(['cc','-O2','-std=c11','-shared','-fPIC','-I'+str(generated),'-I'+str(project/'runtime'),
                     generated/'vx_bridges.c',bindings,project/'runtime/vx_runtime.c','-o',library])
        dll=ctypes.CDLL(str(library))
        for name in ('first','arrays','second'):
            getattr(dll,'vx_'+name).argtypes=[ctypes.c_uint8]
            read=getattr(dll,'vx_'+name+'_read');read.argtypes=[ctypes.c_uint16];read.restype=ctypes.c_uint32
        for n in range(256):
            dll.vx_first(n)
            if mode=='serial':assert dll.vx_first_read(0)==(2*n+1)%256
            dll.vx_arrays(n)
            assert [dll.vx_arrays_read(i) for i in range(4)]==[(2*n)%256,(3*n+2)%256,(n-128)&0xffffffff,n+300]
            dll.vx_second(n);assert dll.vx_second_read(0)==(2*n+3)%256
            if mode=='overlap':assert dll.vx_first_read(0)==(2*n+1)%256
        check_first='if(vx_first_read(0)!=((n+n+1)&255)) {vx_last_result=1+n;__asm halt __endasm;}\n'
        (project/'user/main.c').write_text('#include "vexel.h"\n'+BINDINGS+'''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    uint16_t n;
    vx_last_result=0;
    for(n=0;n<256;++n) {
        vx_first((uint8_t)n);
'''+(check_first if mode=='serial' else '')+'''
        vx_arrays((uint8_t)n);
        if(vx_arrays_read(0)!=((n+n)&255) || vx_arrays_read(1)!=((n+n+n+2)&255) ||
           vx_arrays_read(2)!=(uint32_t)(int32_t)((int16_t)n-128) || vx_arrays_read(3)!=n+300) {
            vx_last_result=257+n;__asm halt __endasm;
        }
        vx_second((uint8_t)n);
        if(vx_second_read(0)!=((n+n+3)&255)) {vx_last_result=513+n;__asm halt __endasm;}
'''+(check_first if mode=='overlap' else '')+'''
    }
    __asm halt __endasm;
}
''')
        built=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=500000000'])
        (directory/('build-'+mode+'.log')).write_text(built.stdout+built.stderr)
        execution=json.loads((project/'build/execution.json').read_text())
        assert execution['observation']['last_result']==execution['observation']['fault']==0,execution
        runs.append({'mode':mode,'report':report,'execution':execution,
                     'execution_sha256':sha(project/'build/execution.json'),'project':str(project)})
    evidence={'machine_sha256':sha(machine),'source_sha256':sha(source),'driver_sha256':sha(__file__),
              'host_activations':1536,'z80_activations':1536,'runs':runs}
    (directory/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('1,536 host and 1,536 Z80 activations passed with resource and feedback contracts')
    print('Evidence:',directory/'comparison.json')

if __name__=='__main__':main()
