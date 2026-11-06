#!/usr/bin/env python3
"""Implicit calls carry schema captures and recursive writes like named calls."""
import ctypes
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_msx import EXE, project_environment, run

PLUS='&(x)#u8::+(y:#u8){(#u16)x+(#u16)y+(#u16)offset}'
MAKER='&make(offset:#u8){'+PLUS+'#P(n:#u8,data:#u8[n+1]);#P(2,[4,5,6,7])}'

def recursive(name,method,operation):
    return ('&^'+name+'(n:#u8)->#u8{total:#u8=0;'+method+
            '&~step(k:#u8)->#u8{k==0?total:{'+operation+';step(k-1)}}step(n)}')

ADD='&(v)#u16::+(w:#u16){total+=1;(#u32)v+(#u32)w}'
RUNTIME=(recursive('binary',ADD,'(#u16)0+(#u16)0')+
         recursive('broadcast',ADD,'[(#u16)0,(#u16)0].+[(#u16)0,(#u16)0]')+
         recursive('iteration','&(v)#u16::@($body){total+=1;_:#u8=0;$body}','(#u16)0@{}')+
         recursive('logical','&(v)#b::&&(w:#b){total+=1;!(!v||!w)}','(#b)1&&(#b)1'))

class OperatorClosureTests(unittest.TestCase):
    def check(self,source,value=None,error=None):
        with tempfile.TemporaryDirectory(prefix='vexel-operator-closure-') as directory:
            path=Path(directory)/'main.vx';path.write_text(source)
            for schedule in ('fifo','lifo','random'):
                result=subprocess.run([str(EXE),str(path),'--json','--schedule',schedule],capture_output=True,text=True,timeout=30)
                if error:
                    self.assertEqual(result.returncode,1,(schedule,result.stderr));self.assertIn(error,result.stderr)
                else:
                    self.assertEqual(result.returncode,0,(schedule,result.stderr))
                    self.assertEqual(json.loads(result.stdout)['roots'][0]['value'],value)

    def test_operator_captures_are_closed_and_frozen(self):
        self.check(MAKER+'^answer:#u8=make(1).data[0];','4')
        self.check(MAKER+'&^answer(offset:#u8)->#u8{p=make(offset);p.n=2;p.data[0]}',error='runtime dependencies in explicit fields')
        self.check('^answer:#u8={offset:#u8=1;'+PLUS+'#P(n:#u8,data:#u8[n+1]);'
                   'p=#P(2,[4,5,6,7]);offset=2;p.n=2;p.data[0]};','4')
        maker='&make(offset:#u8,count:#u8){'+PLUS+'#P(n:#u8,data:#u8[n+1]);#P(count,[4,5,6,7])}'
        self.check(maker+'&^answer(flag:#b)->#u8{p=flag?make(1,2):make(2,1);p.data[0]}',error='same type')

    def test_unary_and_broadcast_captures(self):
        maker='&make(offset:#u8){&(x)#i8::-(){(#i16)x+(#i16)offset}#P(n:#i8,data:#u8[-n]);#P(2,[4,5,6])}'
        self.check(maker+'^answer:#u8=make(1).data[0];','4')
        self.check(maker+'&^answer(offset:#u8)->#u8{make(offset).data[0]}',error='runtime dependencies in explicit fields')
        maker='&make(offset:#u8){'+PLUS+'#P(n:#u8[2],data:#u8[(n .+ [1,1])[0]]);#P([1,2],[4,5,6])}'
        self.check(maker+'^answer:#u8=make(1).data[0];','4')
        self.check(maker+'&^answer(offset:#u8)->#u8{make(offset).data[0]}',error='runtime dependencies in explicit fields')

    def test_builtin_fallback_and_selected_overloads(self):
        for methods in ('&(x)#u8::+(y:#u8){(#u32)x+(#u32)y+1}&(x)#u16::+(y:#u16){(#u32)x+(#u32)y+(#u32)unused}',
                        '&(x)#u16::+(y:#u16){(#u32)x+(#u32)y+(#u32)unused}&(x)#u8::+(y:#u8){(#u32)x+(#u32)y+1}'):
            self.check('^answer:#u8={unused:#u8;'+methods+'#P(n:#u8,data:#u8[n+(#u8)1]);p=#P(2,[4,5,6,7]);p.data[0]};','4')
        self.check('^answer:#u8={unused:#u8;&(x)#u8::+(y:#u8){(#u16)x+(#u16)y+(#u16)unused}'
                   '#P(n:#u32,data:#u8[n+1]);p=#P(2,[4,5,6]);p.data[0]};','4')
        self.check('^answer:#u8={unused:#u8;&(x)#u8::+(y:#u8){(#u16)x+(#u16)y+(#u16)unused}'
                   '&(a:#u8[2]).+(b:#u8[2]){[(#u8)3,(#u8)4]}'
                   '#P(n:#u8[2],data:#u8[(n .+ [(#u8)1,(#u8)1])[0]]);p=#P([1,2],[4,5,6]);p.data[0]};','4')

    def test_compound_helper_operators_keep_their_captures(self):
        method='&(x)#u8::+(y:#u8){(#u8)((#u16)x+(#u16)y+(#u16)offset)}'
        for declaration in ('scratch:#u8=n;','scratch=n;'):
            maker='&make(offset:#u8){'+method+'&width(n:#u8){'+declaration+'scratch+=1;scratch}'
            maker+='#P(count:#u8,data:#u8[width(count)]);#P(2,[4,5,6,7])}'
            self.check(maker+'^answer:#u8=make(1).data[0];','4')
            self.check(maker+'&^answer(offset:#u8)->#u8{make(offset).data[0]}',error='runtime dependencies in explicit fields')

    def test_iteration_and_sorted_fallback_capture_identity(self):
        maker='&make(offset:#u8){&(value)#u8::@($body){_:#u8=value+offset;$body}'
        maker+='#P(n:#u8,data:#u8[{result:#u8=0;n@{result=_;};result}]);#P(2,[4,5,6])}'
        self.check(maker+'^answer:#u8=make(1).data[0];','4')
        self.check(maker+'&^answer(offset:#u8)->#u8{make(offset).data[0]}',error='runtime dependencies in explicit fields')
        self.check('^answer:#u8={reverse:#b=0;&(a)#u16::@@($body){_:#u16=a;$body}'
                   '&(a)#u8::<(b:#u8){reverse?(#u16)a>(#u16)b:(#u16)a<(#u16)b}'
                   '#P(n:#u8[2],data:#u8[{first:#u8=0;n@@{first=_;->|;};first}]);'
                   'p=#P([2,3],[4,5]);reverse=1;p.n=[2,3];p.data[0]};','4')

    def test_recursive_operator_writes_match_source_evaluation(self):
        source=RUNTIME.replace('&^','&')
        for name,multiplier in (('binary',1),('broadcast',2),('iteration',1),('logical',1)):
            self.check(source+'^answer:#u8='+name+'(3);',str(3*multiplier))

    def test_materialized_c_preserves_each_capture_update(self):
        with tempfile.TemporaryDirectory(prefix='vexel-operator-host-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            run([EXE,source,'--emit-msx',project])
            library=root/'test.so'
            run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(project/'generated'),'-I'+str(project/'runtime'),
                 *sorted((project/'generated').glob('*.c')),project/'runtime/vx_runtime.c','-o',library])
            dll=ctypes.CDLL(str(library))
            for name,multiplier in (('binary',1),('broadcast',2),('iteration',1),('logical',1)):
                fn=getattr(dll,'vx_'+name);fn.argtypes=[ctypes.c_uint8];fn.restype=ctypes.c_uint8
                for n in range(8):self.assertEqual(fn(n),n*multiplier,(name,n))

    def test_z80_recursive_capture_updates(self):
        with tempfile.TemporaryDirectory(prefix='vexel-operator-z80-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            run([EXE,source,'--emit-msx',project])
            (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    uint8_t n;
    for(n=0;n<5;++n) {
        if(vx_binary(n)!=n || vx_broadcast(n)!=n*2 || vx_iteration(n)!=n || vx_logical(n)!=n) vx_trap(90);
    }
    vx_last_result=77; __asm halt __endasm;
}''')
            run(['make','-C',project,'-j2','simulate','SIMFLAGS=--irq-period 997 --expanded-slot'],env=project_environment(),timeout=180)
            self.assertEqual(json.loads((project/'build/execution.json').read_text())['observation']['last_result'],77)

if __name__=='__main__':unittest.main(verbosity=2)
