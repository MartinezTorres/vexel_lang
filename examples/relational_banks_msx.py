#!/usr/bin/env python3
"""Check editable C/assembly bank modules and suspended mapper restoration."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile

REPO=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('source_msx',REPO/'examples/relational_source_msx.py')
support=importlib.util.module_from_spec(spec);spec.loader.exec_module(support)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('machine',type=Path)
    parser.add_argument('--output',type=Path,default=Path('build/relational-banks-msx'));args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);directory=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    machine=args.machine.resolve();source=directory/'source.vx';source.write_text('&^entry(n:#u8){n+1}\n')
    generated=directory/'projection';support.run([machine,source,'--source','entry','--budget','1','--emit-c',generated])
    sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
    evidence={'machine_sha256':sha(machine),'source_sha256':sha(source),'driver_sha256':sha(__file__),'cases':[]}
    for mode in ('generated','c','assembly'):
        project=directory/mode;report=support.backend.package(generated,project,source,machine)
        assert report['bank_modules']==[{'name':'vx_entry','module':'vxp_source_entry','window':'B','frame_policy':'automatic'}],report
        if mode=='c':
            (project/'user/vxp_source_entry.c').write_text('''#include "vexel.h"
void vxi_vx_entry(VX_STABLE uint8_t vxp_n) VX_ABI {
    vx_payload[0]=(uint8_t)(vxp_n+1);
}
''')
        elif mode=='assembly':
            (project/'user/vxp_source_entry.s').write_text('''; SDCC call 0: return address, then one byte argument.
    .module vxp_source_entry
    .globl _vxi_vx_entry, _vx_payload
    .area _CODE
_vxi_vx_entry::
    ld hl,#2
    add hl,sp
    ld a,(hl)
    inc a
    ld (_vx_payload),a
    ret
''')
        (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    uint16_t n,previous;
    vx_last_result=0;
    for(n=0;n<256;++n) {
        previous=vx_bank_b(n&7);
        vx_entry((uint8_t)n);
        if(vx_entry_read(0)!=(uint8_t)(n+1) || vx_segment_b!=(n&7)) {
            vx_last_result=1+n;__asm halt __endasm;
        }
        (void)vx_bank_b(previous);
    }
    __asm halt __endasm;
}
''')
        built=support.run(['make','-C',project,'-j2','simulate'])
        (directory/(mode+'-build.log')).write_text(built.stdout+built.stderr)
        execution=json.loads((project/'build/execution.json').read_text())
        assert execution['observation']['last_result']==execution['observation']['fault']==0,execution
        resolution=json.loads((project/'build/resolution.json').read_text())
        assert resolution['handwritten_overrides']==([] if mode=='generated' else ['user/vxp_source_entry.'+('c' if mode=='c' else 's')]),resolution
        evidence['cases'].append({'mode':mode,'z80_activations':256,'project':str(project),
            'execution_sha256':sha(project/'build/execution.json'),'resolution_sha256':sha(project/'build/resolution.json')})
    (directory/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('768 Z80 activations passed: generated, C override and assembly override; caller banks restored')
    print('Evidence:',directory/'comparison.json')

if __name__=='__main__':main()
