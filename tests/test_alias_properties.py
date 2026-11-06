#!/usr/bin/env python3
"""Direct cyclic aliases close demanded properties without closing siblings."""
import ctypes
import json
import os
from pathlib import Path
import shlex
import tempfile
import unittest

import test_context_equations as context
from test_msx import EXE, REPO, project_environment, run

GLOBALS='#Box(x,y);a:#[b]=#Box((#u8)1,missing());b:#[a]=#Box((#u16)2,other());'
RUNTIME='''
&!source()->#u8;
#Box(x,y);
a:#[b]=#Box(source(),(#u8)1);
b:#[a]=#Box((#u16)2,(#u8)3);
&^value()->#u16{a.x+b.x}
&^query()->#u16{
    offset:#u8;
    #Captured(x,data:#u8[x+offset]);
    #Pair(a:#[b],b:#[a]);
    v:#[#Pair(#Captured((#u8)2,[1,2,3]),#Captured((#u16)2,[1,2,3])).a.x]=300;
    v
}
'''


class AliasPropertyTests(unittest.TestCase):
    check=context.ContextEquationTests.check

    def test_record_aliases_join_only_needed_fields(self):
        self.check('#Box(x,y);#P(a:#[b],b:#[a]);^answer:#u16={v:#['
            '#P(#Box((#u8)1,missing()),#Box((#u16)2,other())).a.x]=300;v};',{'answer':'300'})
        self.check('#Box(x,y);#P(a:#[b],b:#[c],c:#[a]);^answer:#u32={v:#['
            '#P(#Box((#u8)1,missing()),#Box((#u16)2,other()),#Box((#u32)3,third())).a.x]=65536;v};',{'answer':'65536'})
        self.check('#P(a:#[b],b:#[a]);^answer:#u16={v:#['
            '#P(((#u8)1,missing()),((#u16)2,other())).a.__0]=300;v};',{'answer':'300'})

    def test_global_aliases_expose_nested_properties(self):
        for source in (GLOBALS,'#Box(x,y);b:#[a]=#Box((#u16)2,other());a:#[b]=#Box((#u8)1,missing());'):
            self.check(source+'^answer:#u16={v:#[a.x]=300;v};',{'answer':'300'})
        self.check('a:#[b]=[[missing(),other()],[other(),missing()]];'
            'b:#[a]=[[other(),missing()],[missing(),other()]];'
            '^answer:#u8={v:#u8[|a[0]|]=[1,2];v[1]};',{'answer':'2'})
        self.check(GLOBALS+'^answer:#u16=a.x;',error='undefined function')
        # General equations still need every initializer witness to widen.
        self.check('a:#[b[0]]=(#u8)255;b:#[[a]]=[(#u16)2];^answer:#u16=a+1;',{'answer':'256'})

    def test_captured_schemas_remain_pending_until_needed(self):
        prefix='^answer:#u8={unknown:#u8;#Box(x:#u8,data:#u8[x+unknown]);#P(a:#[b],b:#[a]);'
        value='#P(#Box(2,[1,2,3]),#Box(2,[1,2,3]))'
        self.check(prefix+'v:#['+value+'.a.x]=7;v};',{'answer':'7'})
        self.check(prefix+'v:#['+value+'];7};',error='uninitialized local')
        prefix='^answer:#u8={k:#u8=1;#Box(x:#u8,data:#u8[x+k]);#P(a:#[b],b:#[a]);'
        prefix+='one=#Box(1,[1,2]);k=2;two=#Box(1,[1,2,3]);'
        self.check(prefix+'v:#[#P(one,two).a.x]=7;v};',{'answer':'7'})
        self.check(prefix+'v:#[#P(one,two)];7};',error='different captured schema')
        self.check(prefix+'#P(one,two).a.x};',error='different captured schema')

    def test_scoped_optional_and_structural_contexts(self):
        module='#Box(x,y);a:#[b]=#Box(width(1),missing());b:#[a]=#Box(width(2),other());'
        self.check('^first:#u16={&width(n){(#u16)n}::part;v:#[a.x]=300;v};'
            '^second:#u32={&width(n){(#u32)n}::part;v:#[a.x]=65536;v};',
            {'first':'300','second':'65536'},files={'part.vx':module})
        self.check('^first:#u16={r:#u16=7;#{::part;v:#[a.x]=300;r=v;missing();};r};'
            '^second:#u16={::part;v:#[a.x]=300;v};',{'first':'7','second':'300'},files={'part.vx':GLOBALS})
        self.check('#Box(x,y);#P(a:#[b],b:#[a]);^answer:#u16=?{n=?(#b,[0,1]);'
            'v:#[#P(n?#Box((#u8)1,missing()):#Box((#u16)1,other()),#Box((#u8)2,third())).a.x]=7;'
            '(#u16)v};',{'answer':'7'},finite=True)

    def test_later_whole_type_demand_does_not_use_an_empty_schema(self):
        with tempfile.TemporaryDirectory(prefix='vexel-alias-properties-api-') as directory:
            root=Path(directory);source=root/'main.vx';harness=root/'main.cpp';executable=root/'check'
            source.write_text(GLOBALS+'first:#u16={v:#[a.x]=300;v};whole:#u8={v:#[a];7};')
            harness.write_text('''#include "vexel.hpp"
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    try {
        vx::Graph graph;graph.options.schedule=argv[2];vx::loadProgram(graph,argv[1]);
        vx::Solver solver(graph);solver.root("first");solver.solve();
        std::cout<<solver.report(true)<<'\\n';solver.root("whole");
        try {solver.solve();return 3;}
        catch(const vx::Error& error) {return error.category==vx::Diagnostic::UndefinedFunction ? 0 : 4;}
    } catch(const std::exception& error) {std::cerr<<error.what();return 1;}
      catch(...) {return 2;}
}
''')
            objects=sorted(p for p in EXE.parent.glob('*.o') if p.name!='main.o')
            flags=shlex.split(os.environ.get('MSX_TEST_RUNNER_CXXFLAGS','-O1 -g'))
            run([*shlex.split(os.environ.get('CXX','c++')),'-std=c++20',*flags,'-I'+str(REPO/'include'),
                harness,*objects,'-ldl','-o',executable])
            for schedule in ('fifo','lifo','random'):
                report=json.loads(run([executable,source,schedule]).stdout)
                self.assertEqual(report['roots'][0]['value'],'300')

    def test_host_queries_preserve_runtime_initialization(self):
        with tempfile.TemporaryDirectory(prefix='vexel-alias-properties-host-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            run([EXE,source,'--emit-msx',project]);helper=root/'helper.c';library=root/'test.so'
            helper.write_text('#include "vexel.h"\nunsigned calls=0;uint8_t vx_ext_source(void) {++calls;return 10;}\n')
            run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(project/'generated'),'-I'+str(project/'runtime'),
                *sorted((project/'generated').glob('*.c')),project/'runtime/vx_runtime.c',helper,'-o',library])
            dll=ctypes.CDLL(str(library));calls=ctypes.c_uint.in_dll(dll,'calls')
            dll.vx_query.restype=ctypes.c_uint16;dll.vx_value.restype=ctypes.c_uint16
            self.assertEqual(dll.vx_query(),300);self.assertEqual(calls.value,0)
            self.assertEqual(dll.vx_value(),12);self.assertEqual(dll.vx_value(),12);self.assertEqual(calls.value,1)

    def test_z80_queries_and_value_aliases(self):
        with tempfile.TemporaryDirectory(prefix='vexel-alias-properties-z80-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            run([EXE,source,'--emit-msx',project])
            (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
uint8_t calls=0;
uint8_t vx_ext_source(void) VX_ABI {++calls;return 10;}
void main(void) VX_ABI VX_RESIDENT {
    vx_last_result=vx_query()+calls*100u;
    vx_last_result+=vx_value()+vx_value();
    vx_last_result+=calls*1000u;
    __asm halt __endasm;
}
''')
            run(['make','-C',project,'-j2','simulate'],env=project_environment(),timeout=180)
            self.assertEqual(json.loads((project/'build/execution.json').read_text())['observation']['last_result'],1324)


if __name__=='__main__':unittest.main(verbosity=2)
