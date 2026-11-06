#!/usr/bin/env python3
"""Independent signed integer tables checked through host and SDCC/Z80 ABIs."""
import argparse
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('source_msx', REPO/'examples/relational_source_msx.py')
support = importlib.util.module_from_spec(spec)
spec.loader.exec_module(support)
sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()

def wrap(n, bits=8):
    return (n+(1<<(bits-1)))%(1<<bits)-(1<<(bits-1))

def quotient(a, b):
    return abs(a)//abs(b)*(-1 if (a<0)!=(b<0) else 1)

def oracle(name, n):
    if name=='arithmetic':
        return [quotient(n,3),n-quotient(n,3)*3,n>>1,wrap(abs(n))]
    if name=='state':
        return [wrap(n+1)]
    if name=='comparison':
        return [int(n<0),int(n>=0)]
    value=-2147483648+n
    return [wrap(-value,32),value>>7]

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('machine',type=Path)
    parser.add_argument('--output',type=Path,default=Path('build/relational-signed-msx'))
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    machine=args.machine.resolve();source=REPO/'examples/relational_signed.vx'
    generated=directory/'projection';project=directory/'project'
    names=['arithmetic','state','comparison','wide']
    support.run([machine,source,*[item for name in names for item in ['--source',name]],
                 '--budget','16','--emit-c',generated])
    report=support.backend.package(generated,project,source,machine)
    library=directory/'host.so'
    support.run(['cc','-O2','-std=c11','-shared','-fPIC','-I'+str(generated),
                 '-I'+str(project/'runtime'),generated/'vx_bridges.c',project/'runtime/vx_runtime.c','-o',library])
    dll=ctypes.CDLL(str(library))
    for name in names:
        entry=getattr(dll,'vx_'+name);read=getattr(dll,'vx_'+name+'_read')
        entry.argtypes=[ctypes.c_uint8 if name=='wide' else ctypes.c_int8]
        read.argtypes=[ctypes.c_uint16];read.restype=ctypes.c_uint32
        for byte in range(256):
            n=byte if name=='wide' else wrap(byte)
            entry(n);expected=oracle(name,n)
            assert [read(i) for i in range(len(expected))]==[v&0xffffffff for v in expected],(name,n)
    # Byte tables avoid using the implementation's arithmetic as its own oracle.
    table=[oracle('arithmetic',wrap(n)) for n in range(256)]
    declarations=[]
    for i in range(4):
        declarations.append('static const int8_t expected_'+str(i)+'[256]={'+','.join(str(row[i]) for row in table)+'};')
    wide_samples=[0,1,127,128,255]
    declarations.append('static const uint8_t wide_inputs[]={'+','.join(map(str,wide_samples))+'};')
    for i in range(2):
        declarations.append('static const uint32_t wide_'+str(i)+'[]={'+','.join(str(oracle('wide',n)[i]&0xffffffff)+'UL' for n in wide_samples)+'};')
    (project/'user/main.c').write_text('#include "vexel.h"\nvolatile uint32_t vx_last_result;\n'+'\n'.join(declarations)+'''
void main(void) VX_ABI VX_RESIDENT {
    uint16_t n;uint8_t i;
    vx_last_result=0;
    for(n=0;n<256;++n){
        vx_arithmetic((int8_t)n);
        if(vx_arithmetic_read(0)!=(uint32_t)expected_0[n] ||
           vx_arithmetic_read(1)!=(uint32_t)expected_1[n] ||
           vx_arithmetic_read(2)!=(uint32_t)expected_2[n] ||
           vx_arithmetic_read(3)!=(uint32_t)expected_3[n]){
            vx_last_result=1+n;__asm halt __endasm;
        }
        vx_state((int8_t)n);
        if(vx_state_read(0)!=(uint32_t)(int8_t)(n+1)){
            vx_last_result=257+n;__asm halt __endasm;
        }
        vx_comparison((int8_t)n);
        if(vx_comparison_read(0)!=(n>=128) || vx_comparison_read(1)!=(n<128)){
            vx_last_result=513+n;__asm halt __endasm;
        }
    }
    for(i=0;i<5;++i){
        vx_wide(wide_inputs[i]);
        if(vx_wide_read(0)!=wide_0[i] || vx_wide_read(1)!=wide_1[i]){
            vx_last_result=769+i;__asm halt __endasm;
        }
    }
    __asm halt __endasm;
}
''')
    built=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=400000000'])
    (directory/'build.log').write_text(built.stdout+built.stderr)
    execution=json.loads((project/'build/execution.json').read_text())
    assert execution['observation']['last_result']==execution['observation']['fault']==0,execution
    evidence={'machine_sha256':sha(machine),'source_sha256':sha(source),'driver_sha256':sha(__file__),
              'host_activations':1024,'z80_activations':773,'report':report,'project':str(project),
              'execution_sha256':sha(project/'build/execution.json')}
    (directory/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('1,024 host and 773 Z80 signed integer activations passed')
    print('Evidence:',directory/'comparison.json')

if __name__=='__main__':
    main()
