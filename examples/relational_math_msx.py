#!/usr/bin/env python3
"""Independent mathematical host and Z80 checks of bundled numeric helpers."""
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
spec=importlib.util.spec_from_file_location('source_msx',REPO/'examples/relational_source_msx.py')
support=importlib.util.module_from_spec(spec);spec.loader.exec_module(support)
def bits(value):return int.from_bytes(struct.pack('<f',value),'little')
def wrap(value):return (value+128)%256-128
def div(a,b):return abs(a)//abs(b)*(-1 if (a<0)!=(b<0) else 1)
def fixedpow(base,power):
    result=16
    while power:
        if power&1:result=wrap(div(result*base,16))
        power>>=1
        if power:base=wrap(div(base*base,16))
    return result

def main():
    parser=argparse.ArgumentParser();parser.add_argument('machine',type=Path)
    parser.add_argument('--output',type=Path,default=Path('build/relational-math-msx'));args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    machine=args.machine.resolve();source=REPO/'examples/relational_math.vx';generated=directory/'projection';project=directory/'project'
    patterns=[0x7f800000,0xff800000,0x7f800001,0xffc12345,0,0x80000000,0x3f800000,0xbf800000]
    oracles={'integer':lambda n:[math.isqrt(n),pow(n,3,256),min(n,100),max(n,100)],
             'floating':lambda n:[math.isqrt(n),math.floor((n-129)/2),round((n-129)/2),n**3//4096,bits(-float(n))],
             'fixed':lambda n:[wrap(math.floor(wrap(n*16)/32)*16),wrap(int(math.copysign(math.floor(abs(wrap(n*16)/32)+.5),wrap(n*16)))*16),
                               wrap(round(wrap(n*16)/32)*16),fixedpow(div(wrap(n*16),2),n%6)],
             'classified':lambda n:[int(n%8 in (2,3)),int(n%8 in (0,1)),int(n%8>=4),patterns[n%8]>>31,patterns[n%8]|0x80000000]}
    support.run([machine,source,*[arg for name in oracles for arg in ['--source',name]],'--budget','20','--emit-c',generated])
    report=support.backend.package(generated,project,source,machine)
    assert report['runtime_features']=={'fixed':True,'float':True,'math':True},report
    library=directory/'host.so'
    support.run(['cc','-O2','-std=c11','-shared','-fPIC','-DVX_FIXED_RUNTIME=1','-DVX_FLOAT_RUNTIME=1','-DVX_MATH_RUNTIME=1',
                 '-I'+str(generated),'-I'+str(project/'runtime'),generated/'vx_bridges.c',project/'runtime/vx_runtime.c','-o',library])
    dll=ctypes.CDLL(str(library))
    for name,oracle in oracles.items():
        invoke,read=getattr(dll,'vx_'+name),getattr(dll,'vx_'+name+'_read')
        invoke.argtypes=[ctypes.c_uint8];read.argtypes=[ctypes.c_uint16];read.restype=ctypes.c_uint32
        for n in range(256):
            invoke(n);expected=oracle(n);actual=[read(i) for i in range(len(expected))]
            assert actual==[v&0xffffffff for v in expected],(name,n,actual,expected)
    (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
static uint32_t product(uint32_t a,uint32_t b) VX_ABI VX_RESIDENT {
    uint32_t result=0;
    while(b) { if(b&1) result+=a;a<<=1;b>>=1; }
    return result;
}
static int16_t signed_product(int16_t a,int16_t b) VX_ABI VX_RESIDENT {
    uint16_t result=(uint16_t)product((uint16_t)(a<0 ? -a : a),(uint16_t)(b<0 ? -b : b));
    return (a<0)!=(b<0) ? -(int16_t)result : (int16_t)result;
}
static int16_t power(int16_t base,uint8_t exponent) VX_ABI VX_RESIDENT {
    int16_t result=16;
    while(exponent) {
        if(exponent&1) result=(int8_t)(signed_product(result,base)/16);
        exponent>>=1;
        if(exponent) base=(int8_t)(signed_product(base,base)/16);
    }
    return result;
}
void main(void) VX_ABI VX_RESIDENT {
    uint16_t n,root,k;uint8_t exponent;
    int16_t lower,nearest,raw,away,even;
    uint32_t negative,cube;
    const uint32_t patterns[8]={0x7f800000UL,0xff800000UL,0x7f800001UL,0xffc12345UL,0,0x80000000UL,0x3f800000UL,0xbf800000UL};
    vx_last_result=0;
    for(n=0;n<256;++n) {
        root=0;while(product(root+1,root+1)<=n) ++root;
        cube=product(product(n,n),n);
        vx_integer((uint8_t)n);
        if(vx_integer_read(0)!=root || vx_integer_read(1)!=(cube&255) ||
           vx_integer_read(2)!=(n<100 ? n : 100) || vx_integer_read(3)!=(n>100 ? n : 100)) {
            vx_last_result=1+n;__asm halt __endasm;
        }
        lower=n>=129 ? (int16_t)((n-129)/2) : -(int16_t)((130-n)/2);nearest=lower;
        if(!(n&1) && (nearest&1)) ++nearest;
        negative=0x80000000UL;
        if(n) {
            exponent=0;k=n;while(k>>=1) ++exponent;
            negative|=((uint32_t)(127+exponent)<<23)|((uint32_t)(n^(1<<exponent))<<(23-exponent));
        }
        vx_floating((uint8_t)n);
        if(vx_floating_read(0)!=root || vx_floating_read(1)!=(uint32_t)(int32_t)lower ||
           vx_floating_read(2)!=(uint32_t)(int32_t)nearest || vx_floating_read(3)!=cube/4096 || vx_floating_read(4)!=negative) {
            vx_last_result=257+n;__asm halt __endasm;
        }
        raw=(n%16)<8 ? (int16_t)(n%16)*8 : ((int16_t)(n%16)-16)*8;
        lower=raw/16;if(raw<0 && (raw&15)) --lower;
        away=raw>=0 ? (raw+8)/16 : -((-raw+8)/16);
        even=lower;if((raw&15) && (even&1)) ++even;
        k=n;while(k>=6) k-=6;
        vx_fixed((uint8_t)n);
        if(vx_fixed_read(0)!=(uint32_t)(int32_t)(int8_t)(lower*16) || vx_fixed_read(1)!=(uint32_t)(int32_t)(int8_t)(away*16) ||
           vx_fixed_read(2)!=(uint32_t)(int32_t)(int8_t)(even*16) || vx_fixed_read(3)!=(uint32_t)(int32_t)power(raw,(uint8_t)k)) {
            vx_last_result=513+n;__asm halt __endasm;
        }
        k=n%8;vx_classified((uint8_t)n);
        if(vx_classified_read(0)!=(k==2 || k==3) || vx_classified_read(1)!=(k==0 || k==1) ||
           vx_classified_read(2)!=(k>=4) || vx_classified_read(3)!=(patterns[k]>>31) || vx_classified_read(4)!=(patterns[k]|0x80000000UL)) {
            vx_last_result=769+n;__asm halt __endasm;
        }
    }
    __asm halt __endasm;
}
''')
    build=support.run(['make','-C',project,'-j2','simulate','SIMFLAGS=--max-ticks=2000000000']);(directory/'build.log').write_text(build.stdout+build.stderr)
    result=json.loads((project/'build/execution.json').read_text())
    assert result['observation']['last_result']==result['observation']['fault']==0,result
    sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
    evidence={'machine_sha256':sha(machine),'source_sha256':sha(source),'driver_sha256':sha(__file__),
              'host_activations':1024,'z80_valid_activations':1024,'report':report,'project':str(project),
              'execution_sha256':sha(project/'build/execution.json')}
    (directory/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('1,024 host and 1,024 Z80 bundled math activations passed')
    print('Evidence:',directory/'comparison.json')

if __name__=='__main__':main()
