#!/usr/bin/env python3
"""A generic record requirement belongs to an application, including revisions."""
import ctypes
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_msx import EXE, project_environment, run

METHODS='&width(n:#u8){n+1}&width(n:#u16){n+unused}'
PREFIX='unused:#u8;'+METHODS+'#P(count,data:#u8[width(count)]);'
RUNTIME='''&^answer(count:#u8)->#u8{'''+PREFIX+'''
    &~read(p:#P)->#u8{p.data[0]+(#u8)p.count}
    read(#P(count,[4,5,6]))
}
&^array_answer(count:#u8)->#u8{'''+PREFIX+'''
    &~read(items:#P[1])->#u8{items[0].data[0]}
    read([#P(count,[7,8,9])])
}
&^walk(depth:#u8)->#u8{'''+PREFIX+'''
    &~step(k:#u8,p:#P)->#u8{k==0?p.data[0]:step(k-1,p)}
    step(depth,#P((#u8)2,[4,5,6]))
}'''

class ParameterSchemaTests(unittest.TestCase):
    def check(self,body,value=None,error=None,finite=False):
        with tempfile.TemporaryDirectory(prefix='vexel-parameter-schema-') as directory:
            path=Path(directory)/'main.vx';path.write_text('^answer:#u8='+('?{' if finite else '{')+body+'};')
            for schedule in ('fifo','lifo','random'):
                for branch in (('forward','reverse') if finite else ('forward',)):
                    result=subprocess.run([str(EXE),str(path),'--json','--schedule',schedule,'--branch-order',branch],capture_output=True,text=True,timeout=30)
                    if error:
                        self.assertEqual(result.returncode,1,(schedule,branch,result.stderr));self.assertIn(error,result.stderr)
                    else:
                        self.assertEqual(result.returncode,0,(schedule,branch,result.stderr))
                        self.assertEqual(json.loads(result.stdout)['roots'][0]['value'],value)

    def test_bare_nested_array_and_tuple_requirements(self):
        for parameter,argument,access in (
            ('#P','#P((#u8)2,[1,2,3])','p.data[0]'),
            ('#P[1]','[#P((#u8)2,[1,2,3])]','p[0].data[0]'),
            ('#P[1][1]','[[#P((#u8)2,[1,2,3])]]','p[0][0].data[0]'),
            ('(#P,#u8)','(#P((#u8)2,[1,2,3]),9)','p.__0.data[0]'),
        ):
            self.check(PREFIX+'&read(p:'+parameter+'){'+access+'}read('+argument+')','1')
        self.check(PREFIX+'#H(inner:#P);&read(p:#H){p.inner.data[0]}read(#H(#P((#u8)2,[1,2,3])))','1')
        self.check(PREFIX+'&read(p:#P){p.data[0]}&forward(x:#P){read(x)}forward(#P((#u8)2,[1,2,3]))','1')
        self.check(PREFIX+'&read($p:#P){$p.data[0]}read(#P((#u8)2,[1,2,3]))','1')

    def test_each_call_specializes_its_own_requirement(self):
        for methods in (METHODS,'&width(n:#u16){n+unused}&width(n:#u8){n+1}'):
            prefix='unused:#u8=2;'+methods+'#P(count,data:#u8[width(count)]);&read(p:#P){p.data[0]}'
            first='read(#P((#u8)2,[1,2,3]))';second='read(#P((#u16)2,[4,5,6,7]))'
            for a,b in ((first,second),(second,first)):
                self.check(prefix+'a='+a+';b='+b+';a+b','5')
        self.check(PREFIX+'&read(p:#P){p.data[0]}read(#P((#u16)2,[1,2,3]))',error='uninitialized local: unused')

    def test_unrelated_nominal_candidates_do_not_close_captures(self):
        for methods in ('&read(p:#P){p.data[0]}&read(p:#Other){p.a}',
                        '&read(p:#Other){p.a}&read(p:#P){p.data[0]}'):
            self.check(PREFIX+'#Other(a:#u8);'+methods+'read(#Other(7))','7')
        self.check(PREFIX+'#Other(a:#u8);&read(p:(#P,#u8)){p.__0.data[0]}&read(p:#Other){p.a}read(#Other(7))','7')
        self.check(PREFIX+'#H(inner:#P);#Other(a:#u8);&read(p:#H){p.inner.data[0]}&read(p:#Other){p.a}read(#Other(7))','7')
        self.check(PREFIX+'&read(p:#P){p.data[0]}read((#u8)2)',error='no matching overload')

    def test_type_demand_does_not_execute_argument_values(self):
        self.check(PREFIX+'&read(p:#P){p.data[0]}n:#u8;v:#[read(#P(n,[1,2,3]))]=7;v','7')
        self.check(PREFIX+'&read(p:#P[1]){p[0].data[0]}n:#u8;v:#[read([#P(n,[1,2,3])])]=7;v','7')
        self.check(PREFIX+'&read(p:#P){p.data[0]}count:#u8=1;packet=#P(count+=1,[4,5,6]);read(packet)+count','6')

    def test_schema_captures_still_freeze_in_the_annotation_scope(self):
        prefix='offset:#u8=1;&width(n:#u8){n+offset}#P(count,data:#u8[width(count)]);&read(p:#P){p.data[0]}'
        self.check(prefix+'packet=#P((#u8)2,[1,2,3]);read(packet)','1')
        self.check(prefix+'packet=#P((#u8)2,[1,2,3]);offset=2;read(packet)',error='no matching overload')
        self.check(prefix+'packet=#P((#u8)2,[1,2,3]);offset=2;packet.count=2;packet.data[0]','1')

    def test_application_context_survives_structural_and_optional_revisions(self):
        self.check(PREFIX+'&read(p:#P){p.data[0]}wide=?(#b,[0,1]);count=wide?(#u16)2:(#u8)2;read(#P(count,[1,2,3]))','1',finite=True)
        self.check(PREFIX+'&read(p:#P){p.data[0]}result:#u8=2;#{result=read(#P((#u8)2,[1,2,3]));missing();};result','2')

    def test_host_materialized_array_and_recursive_interfaces(self):
        with tempfile.TemporaryDirectory(prefix='vexel-parameter-host-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            run([EXE,source,'--emit-msx',project])
            library=root/'test.so'
            run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(project/'generated'),'-I'+str(project/'runtime'),
                 *sorted((project/'generated').glob('*.c')),project/'runtime/vx_runtime.c','-o',library])
            dll=ctypes.CDLL(str(library))
            for name in ('answer','array_answer','walk'):
                fn=getattr(dll,'vx_'+name);fn.argtypes=[ctypes.c_uint8];fn.restype=ctypes.c_uint8
            self.assertEqual(dll.vx_answer(2),6);self.assertEqual(dll.vx_array_answer(2),7)
            self.assertEqual([dll.vx_walk(i) for i in range(6)],[4]*6)
            self.assertIn('vx_trap(9)',(project/'generated/vxp_answer.c').read_text())

    def test_z80_materialized_parameters_and_runtime_guard(self):
        with tempfile.TemporaryDirectory(prefix='vexel-parameter-z80-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            run([EXE,source,'--emit-msx',project])
            main='#include "vexel.h"\nvolatile uint32_t vx_last_result;\nvoid main(void) VX_ABI VX_RESIDENT {vx_last_result=%s;__asm halt __endasm;}\n'
            (project/'user/main.c').write_text(main%'vx_answer(2)+vx_array_answer(2)+vx_walk(4)')
            run(['make','-C',project,'-j2','simulate'],env=project_environment(),timeout=180)
            self.assertEqual(json.loads((project/'build/execution.json').read_text())['observation']['last_result'],17)
            (project/'user/main.c').write_text(main%'vx_answer(1)')
            result=subprocess.run(['make','-C',str(project),'-j2','simulate'],env=project_environment(),capture_output=True,text=True,timeout=180)
            self.assertNotEqual(result.returncode,0);self.assertIn('runtime trapped with reason 9',result.stdout+result.stderr)
            self.assertFalse((project/'build/execution.json').exists())

if __name__=='__main__':unittest.main(verbosity=2)
