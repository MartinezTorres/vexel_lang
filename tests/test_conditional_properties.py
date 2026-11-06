#!/usr/bin/env python3
"""Conditional interfaces resolve each demanded property independently."""
import ctypes
import json
from pathlib import Path
import tempfile
import unittest

import test_context_equations as context
from test_msx import EXE, project_environment, run

BOX='#Box(x,y);'
SAME='c?#Box((#u8)1,missing()):#Box((#u8)2,missing())'
DIFFERENT='c?#Box((#u8)1,missing()):#Box((#u16)2,missing())'
RUNTIME=BOX+'''
&!tick()->#b;
&pick(c:#b){c?#Box((#u8)1,missing()):#Box((#u8)2,missing())}
&^query(n:#u8)->#u16{v:#[pick(tick()).x]=7;n+(#u16)v}
#Wire(x:#u16,y:#u16);
&~choose(c:#b)->#Wire{c?#Wire(10,20):#Wire(30,40)}
&^runtime(c:#b)->#u16{p=choose(c);p.x+p.y}
&^effect()->#u16{p=choose(tick());p.x+p.y}
&^shape(n:#u8)->#u16{
    c:#b;&size(a){|a|}
    data:#u8[size(c?[missing(),missing()]:[(#u8)1,(#u16)2])]=[3,4];
    n+(#u16)data[1]
}
'''


class ConditionalPropertyTests(unittest.TestCase):
    check=context.ContextEquationTests.check

    def test_equal_fields_do_not_need_the_condition_or_siblings(self):
        reports=self.check(BOX+'^answer:#u8={c:#b;v:#[('+SAME+').x]=7;v};',{'answer':'7'})
        for report in reports:
            self.assertFalse(any(n['rule']=='Invoke' and n['operator']=='missing' and n['demanded'] for n in report['nodes']))
        self.check('#Box(x:#u8,y);^answer:#u8={c:#b;v:#[(c?#Box(1,missing()):#Box(2,missing())).x]=7;v};',{'answer':'7'})
        self.check(BOX+'^answer:#u8={c:#b;v:#[(c?#Box((#u8)1,(#u8)2):#Box((#u16)1,(#u8)2)).y]=7;v};',{'answer':'7'})
        self.check(BOX+'^answer:#u8={c:#u8;v:#[('+SAME+').x]=7;v};',error='condition must be boolean')

    def test_nested_records_tuples_and_array_elements(self):
        self.check(BOX+'#Wrap(box);^answer:#u8={c:#b;v:#[(c?#Wrap(#Box((#u8)1,missing())):#Wrap(#Box((#u8)2,missing()))).box.x]=7;v};',{'answer':'7'})
        self.check('^answer:#u8={c:#b;v:#[(c?((#u8)1,missing()):((#u8)2,missing())).__0]=7;v};',{'answer':'7'})
        self.check(BOX+'^answer:#u8={c:#b;v:#[(c?[#Box((#u8)1,missing())]:[#Box((#u8)2,missing())])[0].x]=7;v};',{'answer':'7'})

    def test_array_extents_are_independent_of_elements(self):
        self.check('^answer:#u8={c:#b;&size(a){|a|}v:#u8[size(c?[missing(),missing()]:[(#u8)1,(#u16)2])]=[3,4];v[1]};',{'answer':'4'})
        self.check('^answer:#u8={c:#b;&size(a){|a|}v:#u8[size(c?[missing()]:[(#u8)1,(#u16)2])]=[3,4];v[1]};',error='uninitialized local: c')
        self.check('^answer:#u8={c:#b=0;&size(a){|a|}v:#u8[size(c?[missing()]:[(#u8)1,(#u16)2])]=[3,4];v[1]};',{'answer':'4'})

    def test_different_properties_require_the_guard(self):
        self.check(BOX+'^answer:#u16={c:#b;v:#[('+DIFFERENT+').x]=300;v};',error='uninitialized local: c')
        self.check(BOX+'^answer:#u16={c:#b=0;v:#[('+DIFFERENT+').x]=300;v};',{'answer':'300'})
        self.check(BOX+'^answer:#u16={c:#b=1;v:#[('+DIFFERENT+').x]=300;v};',error='not representable as #u8')
        self.check(BOX+'^answer:#u16={c:#b=0;v:#[(c?#Box(missing(),1):#Box((#u16)2,3)).x]=300;v};',{'answer':'300'})

    def test_schema_captures_are_separate_and_selected_origins_survive(self):
        self.check('^answer:#u8={c:#b;offset:#u8;#P(x:#u8,data:#u8[x+offset]);v:#[(c?#P(2,[1,2,3]):#P(2,[4,5,6])).x]=7;v};',{'answer':'7'})
        prefix='^answer:#u8={offset:#u8=1;#Packet(n:#u8,data:#u8[n+offset]);one=#Packet(2,[1,2,3]);offset=2;two=#Packet(2,[4,5,6,7]);c:#b=0;v:#[(c?one:two)]=two;'
        self.check(prefix+'v.data[0]=9;v.data[0]};',{'answer':'9'})
        self.check(prefix+'v.n+=1;7};',error='dimension does not match')
        source='&make(c:#b){offset:#u8=1;#P(n:#u8,data:#u8[n+offset]);one=#P(2,[1,2,3]);offset=2;two=#P(2,[4,5,6,7]);c?one:two}'
        source+='r=make(0);first:#u8={v:#[r.n]=7;v};second:#u8={v:#[r]=r;v.data[0]=9;v.data[0]};'
        for roots in (('first','second'),('second','first')):
            self.check(source,{'first':'7','second':'9'},flags=('--root',roots[0],'--root',roots[1]))

    def test_helpers_and_optional_revisions(self):
        prefix=BOX+'&pick(c:#b){'+SAME+'}'
        self.check(prefix+'^answer:#u8={c:#b;v:#[pick(c).x]=7;v};',{'answer':'7'})
        self.check(prefix+'^answer:#u8={c:#b;r:#u8=1;#{v:#[pick(c).x]=7;r=v;};r};',{'answer':'7'})
        self.check(prefix+'^answer:#u8={c:#b;r:#u8=1;#{v:#[pick(c).x]=7;r=v;missing();};r};',{'answer':'1'})

    def test_structural_choices_preserve_observable_uniqueness(self):
        self.check(BOX+'^answer:#u8=?{c=?(#b,[0,1]);v:#[('+SAME+').x]=7;v};',{'answer':'7'},finite=True)
        self.check(BOX+'^answer:#u16=?{c=?(#b,[0,1]);v:#[('+DIFFERENT+').x]=255;(#u16)(v+1)};',error='ambiguous',finite=True)
        self.check(BOX+'^answer:#u16=?{c=?(#b,[0,1]);?(!c);v:#[('+DIFFERENT+').x]=255;(#u16)(v+1)};',{'answer':'256'},finite=True)
        self.check(BOX+'^answer:#u16=?{c=?(#b,[0,1]);v:#[('+DIFFERENT+').x]=255;(#u16)(v+1)};',error='budget',finite=True,flags=('--max-decisions','1'))

    def test_whole_interfaces_values_and_completion_keep_requirements(self):
        self.check(BOX+'^answer:#u8={c:#b;(c?#Box((#u8)1,2):#Box((#u8)2,3)).x};',error='uninitialized local: c')
        self.check(BOX+'^answer:#u8={v:#[(1?#Box((#u8)1,missing()):#Box((#u8)2,3))];7};',error='undefined function: missing')
        self.check(BOX+'^answer:#u8={v:#[(1?#Box((#u8)1,(#u16)2):#Box((#u8)2,(#u16)3))]=#Box(7,8);v.x};',{'answer':'7'})
        self.check(BOX+'&f(c:#b){c?{->(#u16)7;}:#Box((#u8)1,(#u8)2);(#u8)0}'
            '^answer:#u16={v:#[f(1)]=300;v};',{'answer':'300'})

    def test_host_queries_do_not_execute_external_guards(self):
        with tempfile.TemporaryDirectory(prefix='vexel-conditional-properties-host-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            run([EXE,source,'--emit-msx',project]);library=root/'test.so';external=root/'external.c'
            external.write_text('#include "vexel.h"\nunsigned calls=0;uint8_t vx_ext_tick(void){++calls;return 1;}\n')
            run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(project/'generated'),'-I'+str(project/'runtime'),
                *sorted((project/'generated').glob('*.c')),project/'runtime/vx_runtime.c',external,'-o',library])
            dll=ctypes.CDLL(str(library))
            for name in ('query','runtime','shape'):
                fn=getattr(dll,'vx_'+name);fn.argtypes=[ctypes.c_uint8];fn.restype=ctypes.c_uint16
            dll.vx_effect.argtypes=[];dll.vx_effect.restype=ctypes.c_uint16
            self.assertEqual([dll.vx_query(n) for n in (0,1,255)],[7,8,262])
            self.assertEqual(dll.vx_shape(255),259)
            self.assertEqual([dll.vx_runtime(n) for n in (0,1)],[70,30])
            self.assertEqual(ctypes.c_uint.in_dll(dll,'calls').value,0)
            self.assertEqual(dll.vx_effect(),30)
            self.assertEqual(ctypes.c_uint.in_dll(dll,'calls').value,1)

    def test_z80_queries_and_runtime_effects(self):
        with tempfile.TemporaryDirectory(prefix='vexel-conditional-properties-z80-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            run([EXE,source,'--emit-msx',project])
            (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
uint8_t calls=0;
uint8_t vx_ext_tick(void) VX_ABI {++calls;return 1;}
void main(void) VX_ABI VX_RESIDENT {
    vx_last_result=vx_query(255)+vx_shape(255)+vx_runtime(0)+vx_runtime(1)+vx_effect();
    vx_last_result+=calls*1000u;
    __asm halt __endasm;
}
''')
            run(['make','-C',project,'-j2','simulate'],env=project_environment(),timeout=180)
            self.assertEqual(json.loads((project/'build/execution.json').read_text())['observation']['last_result'],1651)


if __name__=='__main__':unittest.main(verbosity=2)
