#!/usr/bin/env python3
"""Constructor field interfaces select the captures of generic record schemas."""
import ctypes
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_msx import EXE, project_environment, run

OVERLOADS='&width(n:#u8){n+1}&width(n:#u16){n+unused}'
RUNTIME='&^answer(count:#u8)->#u8{unused:#u8;'+OVERLOADS+'#P(n,data:#u8[width(n)]);p=#P(count,[4,5,6]);p.data[0]}'

class ConstructorSchemaTests(unittest.TestCase):
    def check(self,source,value=None,error=None):
        with tempfile.TemporaryDirectory(prefix='vexel-constructor-schema-') as directory:
            path=Path(directory)/'main.vx';path.write_text(source)
            for schedule in ('fifo','lifo','random'):
                result=subprocess.run([str(EXE),str(path),'--json','--schedule',schedule],capture_output=True,text=True,timeout=30)
                if error:
                    self.assertEqual(result.returncode,1,(schedule,result.stderr));self.assertIn(error,result.stderr)
                else:
                    self.assertEqual(result.returncode,0,(schedule,result.stderr))
                    self.assertEqual(json.loads(result.stdout)['roots'][0]['value'],value)

    def test_concrete_generic_fields_select_the_applicable_capture_set(self):
        for methods in (OVERLOADS,'&width(n:#u16){n+unused}&width(n:#u8){n+1}'):
            self.check('^answer:#u8={unused:#u8;'+methods+'#P(count,data:#u8[width(count)]);'
                       'p=#P((#u8)2,[1,2,3]);p.data[0]};','1')
            self.check('^answer:#u8={unused:#u8;'+methods+'#P(count,data:#u8[width(count)]);'
                       'p=#P((#u16)2,[1,2,3]);p.data[0]};',error='uninitialized local: unused')
        self.check('^answer:#u8={unused:#u8;'+OVERLOADS+'&forward(n){width(n)}'
                   '#P(count,data:#u8[forward(count)]);p=#P((#u8)2,[1,2,3]);p.data[0]};','1')

    def test_generic_operator_fields_and_frozen_captures(self):
        methods='&(a)#u8::+(b:#u8){(#u32)a+(#u32)b+1}&(a)#u16::+(b:#u16){(#u32)a+(#u32)b+(#u32)unused}'
        self.check('^answer:#u8={unused:#u8;'+methods+'#P(n,data:#u8[n+(#u8)1]);'
                   'p=#P((#u8)2,[4,5,6,7]);p.data[0]};','4')
        self.check('^answer:#u8={offset:#u8=1;&width(n:#u8){n+offset}#P(count,data:#u8[width(count)]);'
                   'p=#P((#u8)2,[4,5,6]);offset=2;p.count=2;p.data[0]};','4')

    def test_constructor_context_survives_revisions(self):
        self.check('^answer:#u8=?{unused:#u8;'+OVERLOADS+'#P(count,data:#u8[width(count)]);'
                   'wide=?(#b,[0,1]);count=wide?(#u16)2:(#u8)2;p=#P(count,[4,5,6]);(#u8)p.count};','2')
        self.check('^answer:#u8={unused:#u8;'+OVERLOADS+'#P(count,data:#u8[width(count)]);result:#u8=1;'
                   '#{p=#P((#u8)2,[4,5,6]);result=p.data[0];missing();};result};','1')

    def test_host_c_retains_the_runtime_invariant(self):
        with tempfile.TemporaryDirectory(prefix='vexel-generic-schema-host-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            run([EXE,source,'--emit-msx',project])
            library=root/'test.so'
            run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(project/'generated'),'-I'+str(project/'runtime'),
                 *sorted((project/'generated').glob('*.c')),project/'runtime/vx_runtime.c','-o',library])
            dll=ctypes.CDLL(str(library));dll.vx_answer.argtypes=[ctypes.c_uint8];dll.vx_answer.restype=ctypes.c_uint8
            self.assertEqual(dll.vx_answer(2),4)
            self.assertIn('vx_trap(9)',(project/'generated/vxp_answer.c').read_text())

    def test_z80_generic_schema_guard(self):
        with tempfile.TemporaryDirectory(prefix='vexel-generic-schema-z80-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            run([EXE,source,'--emit-msx',project])
            main='#include "vexel.h"\nvolatile uint32_t vx_last_result;\nvoid main(void) VX_ABI VX_RESIDENT {vx_last_result=vx_answer(%d);__asm halt __endasm;}\n'
            (project/'user/main.c').write_text(main%2)
            run(['make','-C',project,'-j2','simulate'],env=project_environment(),timeout=180)
            self.assertEqual(json.loads((project/'build/execution.json').read_text())['observation']['last_result'],4)
            (project/'user/main.c').write_text(main%1)
            result=subprocess.run(['make','-C',str(project),'-j2','simulate'],env=project_environment(),capture_output=True,text=True,timeout=180)
            self.assertNotEqual(result.returncode,0);self.assertIn('runtime trapped with reason 9',result.stdout+result.stderr)
            self.assertFalse((project/'build/execution.json').exists())

if __name__=='__main__':unittest.main(verbosity=2)
