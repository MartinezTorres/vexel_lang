#!/usr/bin/env python3
"""Portable bytes, signed padding, Boolean packing and comparison on the Z80."""
import argparse
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile

REPO=Path(__file__).resolve().parents[1]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result
support=module('source_msx',REPO/'examples/relational_source_msx.py')
simulator=module('simulator',REPO/'backends/msx-neo8/tools/simulate.py')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def oracle_wire(n):
    signed=n if n<128 else n-256
    return [n,0 if n<128 else 15,int(n<128),*struct.pack('<e',signed)]

def main():
    parser=argparse.ArgumentParser();parser.add_argument('machine',type=Path)
    parser.add_argument('--output',type=Path,default=Path('build/relational-representation-msx'));args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    machine=args.machine.resolve();source=REPO/'examples/relational_representation.vx';generated=directory/'projection';project=directory/'project'
    names=['wire','decode','order','check']
    support.run([machine,source,*[item for name in names for item in ['--source',name]],'--budget','16','--emit-c',generated])
    report=support.backend.package(generated,project,source,machine)
    harness=directory/'host.c';harness.write_text('#include "vexel.h"\n#include <setjmp.h>\nstatic jmp_buf trapped;\nvoid vx_trap(uint8_t r){longjmp(trapped,r);}\nunsigned checked(uint8_t n){int r=setjmp(trapped);if(r)return r;vx_check(n);return 0;}\n')
    runtime=directory/'runtime.c';runtime.write_text((project/'runtime/vx_runtime.c').read_text().replace('void vx_trap(uint8_t reason)','void vx_unused_trap(uint8_t reason)',1))
    library=directory/'host.so'
    support.run(['cc','-O2','-std=c11','-shared','-fPIC','-DVX_FLOAT_RUNTIME=1','-I'+str(generated),'-I'+str(project/'runtime'),generated/'vx_bridges.c',runtime,harness,'-o',library])
    dll=ctypes.CDLL(str(library));dll.checked.argtypes=[ctypes.c_uint8];dll.checked.restype=ctypes.c_uint
    for name in names:
        getattr(dll,'vx_'+name).argtypes=[ctypes.c_uint8];read=getattr(dll,'vx_'+name+'_read');read.argtypes=[ctypes.c_uint16];read.restype=ctypes.c_uint32
    for n in range(256):
        dll.vx_wire(n);assert [dll.vx_wire_read(i) for i in range(5)]==oracle_wire(n),n
        dll.vx_decode(n);assert [dll.vx_decode_read(i) for i in range(3)]==[(n-256)&0xffffffff,n%2,0x8000],n
        dll.vx_order(n);assert dll.vx_order_read(0)==int(n<=128),n
        assert dll.checked(n)==(0 if n<2 else 16),n
    tables=[]
    for i in range(5):tables.append('static const uint8_t wire_'+str(i)+'[]={'+','.join(str(oracle_wire(n)[i]) for n in range(256))+'};')
    tables.append('static const int16_t signed_expected[]={'+','.join(str(n-256) for n in range(256))+'};')
    check=' || '.join('vx_wire_read('+str(i)+')!=wire_'+str(i)+'[n]' for i in range(5))
    (project/'user/main.c').write_text('#include "vexel.h"\nvolatile uint32_t vx_last_result;\n'+'\n'.join(tables)+'''
void main(void) VX_ABI VX_RESIDENT {
    uint16_t n;vx_last_result=0;
    for(n=0;n<256;++n){
        vx_wire((uint8_t)n);
        if('''+check+'''){vx_last_result=1+n;__asm halt __endasm;}
        vx_decode((uint8_t)n);
        if(vx_decode_read(0)!=(uint32_t)signed_expected[n] || vx_decode_read(1)!=(n&1) || vx_decode_read(2)!=32768UL){vx_last_result=257+n;__asm halt __endasm;}
        vx_order((uint8_t)n);
        if(vx_order_read(0)!=(n<=128)){vx_last_result=513+n;__asm halt __endasm;}
    }
    for(n=0;n<2;++n){vx_check((uint8_t)n);if(vx_check_read(0)!=7){vx_last_result=769+n;__asm halt __endasm;}}
    __asm halt __endasm;
}
''')
    built=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=500000000']);(directory/'build.log').write_text(built.stdout+built.stderr)
    execution=json.loads((project/'build/execution.json').read_text());assert execution['observation']['last_result']==execution['observation']['fault']==0,execution
    invalid=directory/'invalid-bool';support.backend.package(generated,invalid,source,machine)
    (invalid/'user/main.c').write_text('#include "vexel.h"\nvolatile uint32_t vx_last_result;\nvoid main(void) VX_ABI VX_RESIDENT {vx_last_result=0;vx_check(2);vx_last_result=1;__asm halt __endasm;}\n')
    built=support.run(['make','-C',invalid,'-j2','all','build/neo8-runner']);(invalid/'build.log').write_text(built.stdout+built.stderr)
    rom=invalid/'build/game.rom';symbols=simulator.symbols(Path(str(rom)+'.symbols.map'));plan=json.loads((invalid/'generated/plan.json').read_text());snapshot=invalid/'build/ram.bin'
    r=support.run([invalid/'build/neo8-runner','--rom',rom,'--dump-ram',snapshot,'--max-ticks','10000000','--stack-floor',plan['resident_limit'],'--himem','62336','--irq-period','0','--shadow',symbols['_vx_segment_b'],'--fault',symbols['_vx_fault']])
    trapped=json.loads(r.stdout);at=symbols['_vx_last_result']-0xc000
    assert trapped['fault']==16 and int.from_bytes(snapshot.read_bytes()[at:at+4],'little')==0,trapped
    evidence={'machine_sha256':sha(machine),'source_sha256':sha(source),'driver_sha256':sha(__file__),
              'host_activations':1024,'z80_valid_activations':770,'report':report,'project':str(project),
              'execution_sha256':sha(project/'build/execution.json'),'expected_trap':{'reason':16,'observation':trapped,'rom_sha256':sha(rom),'runner_sha256':sha(invalid/'build/neo8-runner')}}
    (directory/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('1,024 host and 770 valid Z80 representation activations passed, plus Boolean decode trap')
    print('Evidence:',directory/'comparison.json')

if __name__=='__main__':main()
