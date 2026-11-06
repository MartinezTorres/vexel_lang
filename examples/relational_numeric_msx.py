#!/usr/bin/env python3
"""Fixed/IEEE source values through banked numeric helpers and the Z80 ABI."""
import argparse
import ctypes
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct
import tempfile

REPO=Path(__file__).resolve().parents[1]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result
support=module('source_msx',REPO/'examples/relational_source_msx.py')
simulator=module('simulator',REPO/'backends/msx-neo8/tools/simulate.py')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def wrap(n):return (n+128)%256-128
def trunc(a,b):return abs(a)//abs(b)*(-1 if (a<0)!=(b<0) else 1)
def half(x):return int.from_bytes(struct.pack('<e',x),'little')
def oracle_fixed(n):return [trunc(n,3),wrap(n*3),n-trunc(n,48)*48,n>>1,wrap(abs(n))]
def oracle_float(n):return [half(n/3),half(-float(n)),half(abs(float(n)))]
SAMPLES=[0,0x8000,1,3,0x3c00,0xbc00,0x7bff,0xfbff,0x7c00,0xfc00,0x7e00,0x7fff]
def oracle_compare(n):
    x=struct.unpack('<e',n.to_bytes(2,'little'))[0]
    return list(map(int,[x<0,x>=0,x==x,x!=x]))

def main():
    parser=argparse.ArgumentParser();parser.add_argument('machine',type=Path)
    parser.add_argument('--output',type=Path,default=Path('build/relational-numeric-msx'));args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    machine=args.machine.resolve();source=REPO/'examples/relational_numeric.vx';generated=directory/'projection';project=directory/'project'
    names=['fixed_values','float_values','float_bits','finite_cast']
    support.run([machine,source,*[item for n in names for item in ['--source',n]],'--budget','16','--emit-c',generated])
    report=support.backend.package(generated,project,source,machine)
    assert report['runtime_features']=={'fixed':True,'float':True},report
    runtime=directory/'runtime.c';runtime.write_text((project/'runtime/vx_runtime.c').read_text().replace('void vx_trap(uint8_t reason)','void vx_unused_trap(uint8_t reason)',1))
    harness=directory/'host.c';harness.write_text('#include "vexel.h"\n#include <setjmp.h>\nstatic jmp_buf trapped;\nvoid vx_trap(uint8_t r){longjmp(trapped,r);}\nunsigned checked(uint16_t n){int r=setjmp(trapped);if(r)return r;vx_finite_cast(n);return 0;}\n')
    library=directory/'host.so';rt=project/'runtime'
    support.run(['cc','-O2','-std=c11','-shared','-fPIC','-DVX_FIXED_RUNTIME=1','-DVX_FLOAT_RUNTIME=1','-I'+str(generated),'-I'+str(rt),generated/'vx_bridges.c',runtime,harness,'-o',library])
    dll=ctypes.CDLL(str(library))
    for name in names:
        getattr(dll,'vx_'+name).argtypes=[ctypes.c_uint16 if name in names[2:] else ctypes.c_int8]
        read=getattr(dll,'vx_'+name+'_read');read.argtypes=[ctypes.c_uint16];read.restype=ctypes.c_uint32
    for n in range(-128,128):
        for name,oracle in [('fixed_values',oracle_fixed),('float_values',oracle_float)]:
            getattr(dll,'vx_'+name)(n);expected=oracle(n)
            assert [getattr(dll,'vx_'+name+'_read')(i) for i in range(len(expected))]==[v&0xffffffff for v in expected],(name,n)
    dll.checked.argtypes=[ctypes.c_uint16];dll.checked.restype=ctypes.c_uint
    for n in SAMPLES:
        dll.vx_float_bits(n);assert [dll.vx_float_bits_read(i) for i in range(4)]==oracle_compare(n),n
        assert dll.checked(n)==(6 if n&0x7c00==0x7c00 else 0),n
    declarations=[]
    for name,oracle,count,ctype in [('fixed',oracle_fixed,5,'int8_t'),('float',oracle_float,3,'uint16_t')]:
        rows=[oracle(wrap(n)) for n in range(256)]
        for i in range(count):declarations.append('static const '+ctype+' '+name+'_'+str(i)+'[]={'+','.join(str(row[i]) for row in rows)+'};')
    declarations.append('static const uint16_t inputs[]={'+','.join(map(str,SAMPLES))+'};')
    for i in range(4):declarations.append('static const uint8_t compare_'+str(i)+'[]={'+','.join(str(oracle_compare(n)[i]) for n in SAMPLES)+'};')
    check_fixed=' || '.join('vx_fixed_values_read('+str(i)+')!=(uint32_t)fixed_'+str(i)+'[n]' for i in range(5))
    check_float=' || '.join('vx_float_values_read('+str(i)+')!=float_'+str(i)+'[n]' for i in range(3))
    check_compare=' || '.join('vx_float_bits_read('+str(i)+')!=compare_'+str(i)+'[i]' for i in range(4))
    (project/'user/main.c').write_text('#include "vexel.h"\nvolatile uint32_t vx_last_result;\n'+'\n'.join(declarations)+'''
void main(void) VX_ABI VX_RESIDENT {
    uint16_t n;uint8_t i;vx_last_result=0;
    for(n=0;n<256;++n){
        vx_fixed_values((int8_t)n);
        if('''+check_fixed+'''){vx_last_result=1+n;__asm halt __endasm;}
        vx_float_values((int8_t)n);
        if('''+check_float+'''){vx_last_result=257+n;__asm halt __endasm;}
    }
    for(i=0;i<12;++i){
        vx_float_bits(inputs[i]);
        if('''+check_compare+'''){vx_last_result=513+i;__asm halt __endasm;}
        if((inputs[i]&0x7c00)!=0x7c00){
            vx_finite_cast(inputs[i]);
            if(vx_finite_cast_read(0)!=7){vx_last_result=525+i;__asm halt __endasm;}
        }
    }
    __asm halt __endasm;
}
''')
    built=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=500000000'])
    (directory/'build.log').write_text(built.stdout+built.stderr)
    execution=json.loads((project/'build/execution.json').read_text());assert execution['observation']['last_result']==execution['observation']['fault']==0,execution
    invalid=directory/'nonfinite';support.backend.package(generated,invalid,source,machine)
    (invalid/'user/main.c').write_text('#include "vexel.h"\nvolatile uint32_t vx_last_result;\nvoid main(void) VX_ABI VX_RESIDENT {vx_last_result=0;vx_finite_cast(0x7c00);vx_last_result=1;__asm halt __endasm;}\n')
    built=support.run(['make','-C',invalid,'-j2','all','build/neo8-runner']);(invalid/'build.log').write_text(built.stdout+built.stderr)
    rom=invalid/'build/game.rom';symbols=simulator.symbols(Path(str(rom)+'.symbols.map'));plan=json.loads((invalid/'generated/plan.json').read_text());snapshot=invalid/'build/ram.bin'
    observed=support.run([invalid/'build/neo8-runner','--rom',rom,'--dump-ram',snapshot,'--max-ticks','10000000','--stack-floor',plan['resident_limit'],'--himem','62336','--irq-period','0','--shadow',symbols['_vx_segment_b'],'--fault',symbols['_vx_fault']])
    trapped=json.loads(observed.stdout);at=symbols['_vx_last_result']-0xc000
    assert trapped['fault']==6 and int.from_bytes(snapshot.read_bytes()[at:at+4],'little')==0,trapped
    evidence={'machine_sha256':sha(machine),'source_sha256':sha(source),'driver_sha256':sha(__file__),
              'host_activations':536,'z80_valid_activations':532,'report':report,'project':str(project),
              'execution_sha256':sha(project/'build/execution.json'),'expected_trap':{'reason':6,'observation':trapped,'rom_sha256':sha(rom),'runner_sha256':sha(invalid/'build/neo8-runner')}}
    (directory/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('536 host and 532 valid Z80 numeric activations passed, plus nonfinite cast trap')
    print('Evidence:',directory/'comparison.json')

if __name__=='__main__':main()
