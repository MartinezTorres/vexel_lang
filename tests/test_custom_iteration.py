#!/usr/bin/env python3
"""Custom iteration owns break and resumes its implementation after continue."""
import ctypes
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_msx import EXE, project_environment, run

SCHEMA='#Bag(items:#u8[3]);'
METHOD='&(bag)#Bag::@($body){i:#u8=0;(i<3)@{_=bag.items[i];$body;i+=1;};}'
RUNTIME=SCHEMA+METHOD+'''
&^loop(limit:#u8)->#u8{s:#u8=0;v=#Bag([1,2,3]);v@{_>=limit?{->|;};_==2?{->>;};s+=_;};s}
&^returned(limit:#u8)->#u8{v=#Bag([1,2,3]);v@{_==limit?{->(_+10);};};99}
&^nested()->#u8{s:#u8=0;a=#Bag([1,2,3]);b=#Bag([1,2,3]);a@{_==2?{->>;};first=_;b@{_==2?{->|;};s+=first*10+_;};};s}
'''

class CustomIterationTests(unittest.TestCase):
    def check(self,source,value):
        with tempfile.TemporaryDirectory(prefix='vexel-custom-iteration-') as directory:
            path=Path(directory)/'main.vx';path.write_text(source)
            for schedule in ('fifo','lifo','random'):
                result=subprocess.run([str(EXE),str(path),'--json','--schedule',schedule],capture_output=True,text=True,timeout=30)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertEqual(json.loads(result.stdout)['roots'][0]['value'],value)

    def test_break_and_continue_have_custom_loop_targets(self):
        self.check(SCHEMA+METHOD+'^answer:#u8={s:#u8=0;v=#Bag([1,2,3]);v@{_==2?{->|;};s+=_;};s};','1')
        self.check(SCHEMA+METHOD+'^answer:#u8={s:#u8=0;v=#Bag([1,2,3]);v@{_==2?{->>;};s+=_;};s};','4')
        self.check(SCHEMA+METHOD+'^answer:#u8={s:#u8=0;[1,2,3]@{_==2?{->|;};s+=(#u8)_;};s};','1')

    def test_break_leaves_nested_implementation_loops(self):
        method='&(bag)#Bag::@($body){i:#u8=0;(i<3)@{_=bag.items[i];j:#u8=0;(j<2)@{$body;j+=1;};i+=1;};}'
        self.check(SCHEMA+method+'^answer:#u8={s:#u8=0;visits:#u8=0;v=#Bag([1,2,3]);v@{visits+=1;_==2?{->|;};s+=_;};visits*10+s};','32')

    def test_forwarding_and_caller_returns(self):
        method='&forward($x){$x;}&(bag)#Bag::@($body){i:#u8=0;(i<3)@{_=bag.items[i];forward($body);i+=1;};}'
        self.check(SCHEMA+method+'^answer:#u8={s:#u8=0;v=#Bag([1,2,3]);v@{_==2?{->>;};s+=_;};s};','4')
        self.check(SCHEMA+METHOD+'^answer:#u8={s:#u8=0;&f(){v=#Bag([1,2,3]);v@{s+=_;_==2?{->7;};};9}x=f();x+s};','10')

    def test_nested_custom_loops_and_signature_queries(self):
        self.check(RUNTIME+'^answer:#u8=nested();','42')
        self.check(SCHEMA+METHOD+'&!source()->#u8;&f(limit:#u8){v=#Bag([1,2,3]);v@{_==limit?{->source();};};(#u8)7}'
                   '^answer:#u8={limit:#u8;x:#[f(limit)]=9;x};','9')

    def test_c_projection_and_runtime_implementation_loops(self):
        for residual in (False,True):
            with self.subTest(residual=residual),tempfile.TemporaryDirectory(prefix='vexel-custom-host-') as directory:
                root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME.replace('(i<3)@','(i<3)!@') if residual else RUNTIME)
                project=root/'project';run([EXE,source,'--emit-msx',project]);library=root/'test.so'
                run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(project/'generated'),'-I'+str(project/'runtime'),
                     *sorted((project/'generated').glob('*.c')),project/'runtime/vx_runtime.c','-o',library])
                dll=ctypes.CDLL(str(library))
                for name,args,expected in [('loop',[1],0),('loop',[2],1),('loop',[4],4),('returned',[2],12),('returned',[4],99),('nested',[],42)]:
                    fn=getattr(dll,'vx_'+name);fn.argtypes=[ctypes.c_uint8]*len(args);fn.restype=ctypes.c_uint8
                    self.assertEqual(fn(*args),expected)

    def test_z80_control_protocol(self):
        with tempfile.TemporaryDirectory(prefix='vexel-custom-z80-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME.replace('(i<3)@','(i<3)!@'))
            project=root/'project';run([EXE,source,'--emit-msx',project])
            (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    if(vx_loop(1)!=0 || vx_loop(2)!=1 || vx_loop(4)!=4 || vx_returned(2)!=12 || vx_returned(4)!=99) vx_trap(90);
    vx_last_result=vx_nested(); if(vx_last_result!=42) vx_trap(91); __asm halt __endasm;
}''')
            run(['make','-C',project,'-j2','simulate','SIMFLAGS=--irq-period 997 --expanded-slot'],env=project_environment(),timeout=180)

if __name__=='__main__':unittest.main(verbosity=2)
