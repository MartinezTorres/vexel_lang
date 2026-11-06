#!/usr/bin/env python3
"""Open record requirements retain their instance context at every boundary."""
import ctypes
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_msx import EXE, project_environment, run

PREFIX='unused:#u8;&width(m:#u8){m+1}&width(m:#u16){m+unused}#P(count,data:#u8[width(count)]);'
RUNTIME='''&^local(count:#u8)->#u8{'''+PREFIX+'''
    packet:#P=#P(count,[4,5,6]);packet.data[0]
}
&^returned(count:#u8)->#u8{'''+PREFIX+'''
    &~make(n:#u8)->#P{#P(n,[7,8,9])}
    make(count).data[0]
}
&^early(flag:#b)->#u8{'''+PREFIX+'''
    &~make(condition:#b)->#P{condition?{->#P((#u8)2,[1,2,3]);};#P((#u8)2,[4,5,6])}
    make(flag).data[0]
}
&^walk(depth:#u8)->#u8{'''+PREFIX+'''
    &~make(n:#u8)->#P{n==0?#P((#u8)2,[10,11,12]):make(n-1)}
    make(depth).data[0]
}
&^walk_early(depth:#u8)->#u8{'''+PREFIX+'''
    &~make(n:#u8)->#P{n==0?{->#P((#u8)2,[13,14,15]);};make(n-1)}
    make(depth).data[0]
}'''

class RequirementSchemaTests(unittest.TestCase):
    def check(self,source,value=None,error=None,finite=False):
        with tempfile.TemporaryDirectory(prefix='vexel-requirement-schema-') as directory:
            path=Path(directory)/'main.vx';path.write_text(source)
            for schedule in ('fifo','lifo','random'):
                for branch in (('forward','reverse') if finite else ('forward',)):
                    result=subprocess.run([str(EXE),str(path),'--json','--schedule',schedule,'--branch-order',branch],capture_output=True,text=True,timeout=30)
                    if error:
                        self.assertEqual(result.returncode,1,(schedule,branch,result.stderr));self.assertIn(error,result.stderr)
                    else:
                        self.assertEqual(result.returncode,0,(schedule,branch,result.stderr))
                        self.assertEqual(json.loads(result.stdout)['roots'][0]['value'],value)

    def body(self,body,value=None,error=None,finite=False):
        self.check('^answer:#u8='+('?{' if finite else '{')+PREFIX+body+'};',value,error,finite)

    def test_initialized_local_and_cast_requirements(self):
        self.body('packet:#P=#P((#u8)2,[1,2,3]);packet.data[0]','1')
        self.body('packets:#P[1]=[#P((#u8)2,[1,2,3])];packets[0].data[0]','1')
        self.body('packet=(#P)#P((#u8)2,[1,2,3]);packet.data[0]','1')
        self.body('pair:(#P,#u8)=(#P((#u8)2,[1,2,3]),9);pair.__0.data[0]','1')
        self.body('empty:#[#P((#u8)2,[1,2,3])][0]=[];packets:#P[0]=empty;|packets|','0')
        self.body('packet:#P=#P((#u16)2,[1,2,3]);packet.data[0]',error='uninitialized local: unused')

    def test_global_initialization_and_existing_type_alias_equations(self):
        self.check(PREFIX+'packet:#P=#P((#u8)2,[1,2,3]);^answer:#u8=packet.data[0];','1')
        self.check('a:#[b]=(#u8)255;b:#[a]=(#u16)2;^answer:#u16=a+1;','256')

    def test_ordinary_and_early_return_requirements(self):
        self.body('&make()->#P{#P((#u8)2,[1,2,3])}make().data[0]','1')
        self.body('&make()->#P{->#P((#u8)2,[1,2,3]);}make().data[0]','1')
        for value in (0,1):
            self.body('&make(flag:#b)->#P{flag?{->#P((#u8)2,[1,2,3]);};#P((#u8)2,[4,5,6])}make('+str(value)+').data[0]',str(1 if value else 4))
        self.body('&make()->#P[1]{[#P((#u8)2,[1,2,3])]}make()[0].data[0]','1')
        self.body('&make()->(#P,#u8){(#P((#u8)2,[1,2,3]),9)}make().__0.data[0]','1')

    def test_return_queries_do_not_execute_values_or_equal_interface_guards(self):
        self.body('&make(n:#u8)->#P{#P(n,[1,2,3])}input:#u8;v:#[make(input).data]=[4,5,6];v[0]','4')
        self.body('&make(flag:#b)->#P{flag?{->#P((#u8)2,[1,2,3]);};#P((#u8)2,[4,5,6])}condition:#b;v:#[make(condition).data]=[7,8,9];v[0]','7')

    def test_recursive_return_candidates_are_checked(self):
        self.body('&~make(n:#u8)->#P{n==0?#P((#u8)2,[1,2,3]):make(n-1)}make(3).data[0]','1')
        self.body('&~make(n:#u8)->#P{n==0?{->#P((#u8)2,[1,2,3]);};make(n-1)}make(3).data[0]','1')
        self.check('^answer:#u8={offset:#u8=1;#P(count:#u8,data:#u8[count+offset]);&~make()->#P{packet=#P(2,[1,2,3]);offset=2;packet}make().data[0]};',error='different captured schema')
        self.check('#P(x);&~make(n:#u8)->#P{n==0?#P((#u8)1):#P(make(n-1))}&^answer(n:#u8)->#u8{make(n).x}',error='branches must have the same type')

    def test_contextual_requirements_survive_revisions(self):
        self.body('wide=?(#b,[0,1]);count=wide?(#u16)2:(#u8)2;packet:#P=#P(count,[1,2,3]);packet.data[0]','1',finite=True)
        self.body('&make()->#P{#P((#u8)2,[1,2,3])}result:#u8=7;#{packet:#P=make();result=packet.data[0];missing();};result','7')

    def test_host_recursive_record_results_and_runtime_guards(self):
        with tempfile.TemporaryDirectory(prefix='vexel-requirement-host-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            run([EXE,source,'--emit-msx',project])
            library=root/'test.so'
            run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(project/'generated'),'-I'+str(project/'runtime'),
                 *sorted((project/'generated').glob('*.c')),project/'runtime/vx_runtime.c','-o',library])
            dll=ctypes.CDLL(str(library))
            for name in ('local','returned','early','walk','walk_early'):
                fn=getattr(dll,'vx_'+name);fn.argtypes=[ctypes.c_uint8];fn.restype=ctypes.c_uint8
            self.assertEqual(dll.vx_local(2),4);self.assertEqual(dll.vx_returned(2),7)
            self.assertEqual([dll.vx_early(i) for i in (0,1)],[4,1])
            self.assertEqual([dll.vx_walk(i) for i in range(6)],[10]*6)
            self.assertEqual([dll.vx_walk_early(i) for i in range(6)],[13]*6)
            self.assertTrue(any('vx_trap(9)' in path.read_text() for path in (project/'generated').glob('*.c')))

    def test_z80_local_cast_and_materialized_return_contexts(self):
        with tempfile.TemporaryDirectory(prefix='vexel-requirement-z80-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            run([EXE,source,'--emit-msx',project])
            main='#include "vexel.h"\nvolatile uint32_t vx_last_result;\nvoid main(void) VX_ABI VX_RESIDENT {vx_last_result=%s;__asm halt __endasm;}\n'
            (project/'user/main.c').write_text(main%'vx_local(2)+vx_returned(2)+vx_early(0)+vx_early(1)+vx_walk(3)+vx_walk_early(4)')
            run(['make','-C',project,'-j2','simulate'],env=project_environment(),timeout=180)
            self.assertEqual(json.loads((project/'build/execution.json').read_text())['observation']['last_result'],39)
            (project/'user/main.c').write_text(main%'vx_returned(1)')
            result=subprocess.run(['make','-C',str(project),'-j2','simulate'],env=project_environment(),capture_output=True,text=True,timeout=180)
            self.assertNotEqual(result.returncode,0);self.assertIn('runtime trapped with reason 9',result.stdout+result.stderr)
            self.assertFalse((project/'build/execution.json').exists())

if __name__=='__main__':unittest.main(verbosity=2)
