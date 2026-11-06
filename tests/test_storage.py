#!/usr/bin/env python3
"""Persistent state and physical bindings are separate from automatic locals."""
import ctypes
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_msx import EXE, project_environment, run

SOURCE='''
counter:#u8;
!external_value:#u8;
&!seed()->#u8;
&!change();
&!seed_reenter()->#u8;
initialized:#u8=seed();
recursive:#u8=seed_reenter();
#Pair(x:#u8,y:#u16);
pair:#Pair;
items:#u8[3];
initialized_items:#u8[3]=[seed(),2,3];
[[msx_address(0xf3e0)]] !!mapped:#u16;
[[msx_port(0x99)]] !!status:#u8;
&^reset(value:#u8){counter=value;}
&^tick()->#u8{counter+=1;counter}
&^read_uninitialized()->#u8{counter}
&^initialize()->#u8{initialized}
&^initialized_array()->#u8{initialized_items[0]+initialized_items[1]+initialized_items[2]}
&^reentrant()->#u8{recursive}
&^replace(value:#u8){initialized=value;}
&^external_twice()->#u8{before:#u8=external_value;change();before+external_value}
&^external_write(value:#u8){external_value=value;}
&^aggregate(value:#u8)->#u16{pair=#Pair(value,400);pair.y+=2;pair.y+pair.x}
&(p)#Pair::bump(){p.x+=1;}
&~(p)#Pair::step(n:#u8){(n>0)?{p.x+=1;p.step(n-1);};p.x}
&^receiver()->#u8{pair.bump();pair.x}
&^recursive_receiver(n:#u8)->#u8{pair=#Pair(0,400);pair.step(n)}
&^array(value:#u8)->#u8{items=[value,2,3];items[1]+=4;items[0]+items[1]+items[2]}
&^memory(value:#u16)->#u16{mapped=value;mapped}
&^port()->#u8{status}
&step(value:#u8)->#u8{::state;count+=value;count}
&^shared_a(value:#u8)->#u8{step(value)}
&^shared_b(value:#u8)->#u8{step(value)}
&^separate(value:#u8)->#u8{::state;count+=value;count}
&^local_binding(value:#u8)->#u8{[[msx_address(0xf3e2)]] !!bound:#u8;bound=value;bound}
'''
STATE='count:#u8=seed();'
HOOKS='''#include "vexel.h"
volatile uint8_t vx_ext_external_value;
uint8_t seed_calls;
uint8_t vx_ext_seed(void) VX_ABI VX_RESIDENT { ++seed_calls; return 10; }
void vx_ext_change(void) VX_ABI VX_RESIDENT { vx_ext_external_value = 7; }
uint8_t vx_ext_seed_reenter(void) VX_ABI VX_RESIDENT { return vx_reentrant(); }
'''

class StorageTests(unittest.TestCase):
    def check(self,source,expected=None,error=None):
        with tempfile.TemporaryDirectory(prefix='vexel-storage-source-') as directory:
            path=Path(directory)/'main.vx';path.write_text(source)
            for schedule in ('fifo','lifo','random'):
                result=subprocess.run([str(EXE),str(path),'--json','--schedule',schedule],capture_output=True,text=True,timeout=30)
                if error:
                    self.assertEqual(result.returncode,1,result.stderr);self.assertIn(error,result.stderr)
                else:
                    self.assertEqual(result.returncode,0,result.stderr)
                    self.assertEqual({r['name']:(r['type'],r['value'],r['effect']) for r in json.loads(result.stdout)['roots']},expected)

    def test_runtime_storage_and_independent_type_queries(self):
        self.check('state:#u8;&^read()->#u8{state}',{'read':('#u8','runtime','external')})
        self.check('state:#u8;^answer:#u8={x:#[state]=7;x};',{'answer':('#u8','7','pure')})
        self.check('!external:#u16;^answer:#u16={x:#[external]=9;x};',{'answer':('#u16','9','pure')})
        self.check('&!seed()->#u8;state:#u8=seed();&^read()->#u8{state}',{'read':('#u8','runtime','external')})

    def test_pure_and_runtime_declarations_remain_distinct(self):
        self.check('value:#u8=7;^answer:#u8=value;',{'answer':('#u8','7','pure')})
        self.check('value:#u8=7;&^bad(){value=8;}',error='cannot assign global')
        self.check('value:#u8;^answer:#u8={value=7;7};',error='global requires a constant')
        self.check('!external:#s;&^bad(){external;}',error='concrete runtime type')
        self.check('^value:#u8;',error='cannot be exported')
        self.check('#R(x:#u8);value:#R=#R(7);&(r)#R::change(){r.x=9;} &^bad(){value.change();}',error='cannot assign global')
        self.check('#R(x:#u8);value:#R=#R(7);&(r)#R::change(){r.x=9;} &^bad(){value.x;value.change();}',error='cannot assign global')

    def project(self,root,source=SOURCE):
        path=root/'main.vx';path.write_text(source);(root/'state.vx').write_text(STATE)
        project=root/'project'
        run([EXE,path,'--emit-msx',project])
        (project/'user/hooks.c').write_text(HOOKS)
        return project

    def test_host_persistence_initialization_and_module_identity(self):
        with tempfile.TemporaryDirectory(prefix='vexel-storage-host-') as directory:
            root=Path(directory);project=self.project(root)
            library=root/'storage.so'
            run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(project/'generated'),'-I'+str(project/'runtime'),
                 *sorted((project/'generated').glob('*.c')),project/'runtime/vx_runtime.c',project/'user/hooks.c','-o',library])
            dll=ctypes.CDLL(str(library))
            def call(name,*args,result=ctypes.c_uint8):
                fn=getattr(dll,'vx_'+name);fn.argtypes=[ctypes.c_uint16 if name=='memory' else ctypes.c_uint8]*len(args);fn.restype=result
                return fn(*args)
            call('reset',254,result=None);self.assertEqual(call('tick'),255);self.assertEqual(call('tick'),0)
            calls=ctypes.c_uint8.in_dll(dll,'seed_calls')
            self.assertEqual(calls.value,0);self.assertEqual(call('initialize'),10);self.assertEqual(call('initialize'),10);self.assertEqual(calls.value,1)
            call('replace',42,result=None);self.assertEqual(call('initialize'),42);self.assertEqual(calls.value,1)
            call('external_write',3,result=None);self.assertEqual(call('external_twice'),10)
            self.assertEqual(call('aggregate',5,result=ctypes.c_uint16),407);self.assertEqual(call('receiver'),6)
            self.assertEqual(call('recursive_receiver',7),7)
            self.assertEqual(call('array',9),18)
            self.assertEqual(call('memory',54321,result=ctypes.c_uint16),54321)
            self.assertEqual(call('local_binding',31),31)
            self.assertEqual(call('shared_a',1),11);self.assertEqual(call('shared_b',2),13)
            self.assertEqual(call('separate',4),14);self.assertEqual(calls.value,3)
            self.assertEqual(call('initialized_array'),15);self.assertEqual(call('initialized_array'),15);self.assertEqual(calls.value,4)
            plan=json.loads((project/'generated/plan.json').read_text())
            self.assertEqual({b['name']:b['space'] for b in plan['storage_bindings']},{'mapped':'address','status':'port','bound':'address'})

    def test_binding_contract_diagnostics(self):
        cases=[('!!value:#u8;&^read()->#u8{value}','requires [[msx_address'),
               ('[[msx_port(256)]] !!value:#u8;&^read()->#u8{value}','outside its address space'),
               ('[[msx_port(1)]] !!value:#u16;&^read()->#u16{value}','require #u8'),
               ('[[msx_address(65535)]] !!value:#u16;&^read()->#u16{value}','crosses the address-space'),
               ('^text:#s="a";&^text_length()->#u8{1}','string length symbol')]
        for source,error in cases:
            with self.subTest(error=error),tempfile.TemporaryDirectory(prefix='vexel-storage-invalid-') as directory:
                root=Path(directory);path=root/'main.vx';path.write_text(source)
                result=subprocess.run([str(EXE),str(path),'--emit-msx',str(root/'project')],capture_output=True,text=True,timeout=30)
                self.assertEqual(result.returncode,1,result.stderr);self.assertIn(error,result.stderr)
                self.assertFalse((root/'project').exists())

    def test_z80_storage_and_uninitialized_trap(self):
        with tempfile.TemporaryDirectory(prefix='vexel-storage-z80-') as directory:
            root=Path(directory);project=self.project(root)
            (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    vx_reset(254);
    if (vx_tick()!=255 || vx_tick()!=0) vx_trap(90);
    if (vx_initialize()!=10 || vx_initialize()!=10) vx_trap(91);
    if (vx_initialized_array()!=15 || vx_initialized_array()!=15) vx_trap(91);
    vx_replace(42); if (vx_initialize()!=42) vx_trap(92);
    vx_external_write(3);if (vx_external_twice()!=10) vx_trap(93);
    if (vx_aggregate(5)!=407 || vx_receiver()!=6 || vx_array(9)!=18) vx_trap(94);
    if (vx_recursive_receiver(7)!=7) vx_trap(94);
    if (vx_memory(54321)!=54321 || vx_local_binding(31)!=31) vx_trap(95);
    if (vx_shared_a(1)!=11 || vx_shared_b(2)!=13 || vx_separate(4)!=14) vx_trap(96);
    vx_last_result=42; __asm halt __endasm;
}''')
            run(['make','-C',project,'-j2','simulate','SIMFLAGS=--irq-period 997 --expanded-slot'],env=project_environment(),timeout=180)
            (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=vx_read_uninitialized();__asm halt __endasm;}
''')
            result=subprocess.run(['make','-C',str(project),'-j2','simulate'],env=project_environment(),capture_output=True,text=True,timeout=180)
            self.assertNotEqual(result.returncode,0);self.assertIn('runtime trapped with reason 7',result.stdout+result.stderr)
            self.assertFalse((project/'build/execution.json').exists())
            (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=vx_reentrant();__asm halt __endasm;}
''')
            result=subprocess.run(['make','-C',str(project),'-j2','simulate'],env=project_environment(),capture_output=True,text=True,timeout=180)
            self.assertNotEqual(result.returncode,0);self.assertIn('runtime trapped with reason 8',result.stdout+result.stderr)
            self.assertFalse((project/'build/execution.json').exists())

if __name__=='__main__':unittest.main(verbosity=2)
