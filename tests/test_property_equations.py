#!/usr/bin/env python3
"""Transforming interface equations close only the properties they prove."""
import ctypes
import itertools
import json
import os
from pathlib import Path
import shlex
import tempfile
import unittest

import test_context_equations as context
from test_msx import EXE, REPO, project_environment, run

PAIR='#Pair(a:#[#Box(b.x+1,b.data)],b:#[#Box(a.x+1,a.data)]);'
CAPTURED='unknown:#u8;#Box(x,data:#u8[x+unknown]);'+PAIR
ARGS='#Pair(#Box((#u8)2,[1,2,3]),#Box((#u16)2,[1,2,3]))'
GLOBALS='#Box(x,data);a:#[#Box(b.x+1,b.data)]=#Box((#u8)1,missing());b:#[#Box(a.x+1,a.data)]=#Box((#u16)2,other());'


class PropertyEquationTests(unittest.TestCase):
    check=context.ContextEquationTests.check

    def test_transforming_equations_preserve_open_schema(self):
        reports=self.check('^answer:#u16={'+CAPTURED+'v:#['+ARGS+'.a.x]=300;v};',{'answer':'300'})
        for report in reports:
            proofs=report['type_properties']
            self.assertEqual(len(proofs),2)
            nodes={n['id']:n for n in report['nodes']}
            for proof in proofs:
                self.assertTrue(proof['verified'])
                self.assertEqual(proof['path'],[0])
                self.assertEqual(proof['candidate'],'#u16')
                self.assertEqual(nodes[proof['check']]['type'],'#u16')
                self.assertEqual(nodes[proof['check']]['rule'],'TypeJoin')
                self.assertTrue(nodes[proof['owner']]['schema_obligations'])
                self.assertTrue(all(not nodes[i]['demanded'] for i in nodes[proof['owner']]['schema_obligations']))
        self.check('^answer:#u16={'+CAPTURED+'v:#['+ARGS+'];7};',error='uninitialized local')
        self.check('^answer:#u16={'+CAPTURED+ARGS+'.a.x};',error='uninitialized local')

    def test_nested_properties_arrays_and_tuples(self):
        self.check('^answer:#u16={unknown:#u8;#Box(x,data:#u8[x+unknown]);#Wrap(box);'
            '#Pair(a:#[#Wrap(#Box(b.box.x+1,b.box.data))],b:#[#Wrap(#Box(a.box.x+1,a.box.data))]);'
            'v:#[#Pair(#Wrap(#Box((#u8)2,[1,2,3])),#Wrap(#Box((#u16)2,[1,2,3]))).a.box.x]=300;v};',{'answer':'300'})
        self.check('#Pair(a:#[(b.__0+1,b.__1)],b:#[(a.__0+1,a.__1)]);'
            '^answer:#u16={v:#[#Pair(((#u8)1,missing()),((#u16)2,other())).a.__0]=300;v};',{'answer':'300'})
        self.check('#Box(x,data);#Pair(a:#[[#Box(b[0].x+1,b[0].data)]],b:#[[#Box(a[0].x+1,a[0].data)]]);'
            '^answer:#u16={v:#[#Pair([#Box((#u8)1,missing())],[#Box((#u16)2,other())]).a[0].x]=300;v};',{'answer':'300'})

    def test_globals_imports_and_separate_lexical_instances(self):
        self.check(GLOBALS+'^answer:#u16={v:#[a.x]=300;v};',{'answer':'300'})
        self.check('#Box(x,data);b:#[#Box(a.x+1,a.data)]=#Box((#u16)2,other());'
            'a:#[#Box(b.x+1,b.data)]=#Box((#u8)1,missing());^answer:#u16={v:#[a.x]=300;v};',{'answer':'300'})
        module='#Box(x,data);a:#[#Box(width(b.x),b.data)]=#Box((#u8)1,missing());b:#[#Box(a.x+1,a.data)]=#Box((#u8)2,other());'
        self.check('^first:#u16={&width(n){(#u16)n}::part;v:#[a.x]=300;v};'
            '^second:#u32={&width(n){(#u32)n}::part;v:#[a.x]=65536;v};',
            {'first':'300','second':'65536'},files={'part.vx':module})

    def test_cross_field_widths_against_overflow_oracle(self):
        # Two distinct cycles cross field positions. Check exact inferred widths
        # by overflow, not merely that a large number fits in the result.
        prefix='#B(x,y,z);#P(a:#[#B(b.y+1,b.x+1,b.z)],b:#[#B(a.y+1,a.x+1,a.z)]);'
        for widths in itertools.product((8,16,32),repeat=2):
            high=max(widths);maximum=(1<<high)-1
            args=f'#P(#B((#u{widths[0]})1,(#u8)1,missing()),#B((#u8)2,(#u{widths[1]})2,other()))'
            self.check(prefix+f'^answer:#u32={{v:#[{args}.a.x]={maximum};v+1}};',{'answer':'0'})

    def test_overload_selection_discovers_another_equation(self):
        prefix='#B(x,y,z);&choose(n:#u16,box){box.y}&choose(n:#u32,box){box.x}'
        prefix+='#P(a:#[#B(choose(b.x,b),b.y,b.z)],b:#[#B(a.x+(#u16)0,a.x,a.z)]);'
        source=prefix+'^answer:#u32={v:#[#P(#B((#u8)1,(#u8)1,missing()),#B((#u8)2,(#u32)3,other())).a.x]=4294967295;v+1};'
        reports=self.check(source,{'answer':'0'})
        for report in reports:
            self.assertTrue(any(p['path']==[1] and p['candidate']=='#u32' for p in report['type_properties']))

    def test_initializer_witnesses_are_rechecked_after_each_revision(self):
        self.check('#B(x,y,z);&choose(n:#u16,box){box.y}&choose(n:#u32,box){box.y}'
            '&choose(n:#u64,box){box.x}&widen(n:#u16){(#u32)0}&widen(n:#u32){(#u64)0}&widen(n:#u64){(#u64)0}'
            'a:#[#B(choose(b.x,b),b.y,b.z)]=#B((#u8)1,(#u8)1,missing());'
            'b:#[#B(a.x+(#u16)0,a.x,a.z)]=#B((#u8)2,widen(b.x),other());'
            '^answer:#u64={v:#[a.x]=18446744073709551615;v+1};',{'answer':'0'})

    def test_temporary_dependency_errors_do_not_escape_a_revised_candidate(self):
        prefix='#B(x,y);&choose(n:#u16,box){box.y}&choose(n:#u32,box){box.x}'
        prefix+='#P(a:#[#B(choose(b.x,b),b.y)],b:#[#B(a.x+(#u32)0,a.y)]);'
        self.check(prefix+'^answer:#u32={v:#[#P(#B((#u8)1,missing()),#B((#u16)2,other())).a.x]=65536;v};',{'answer':'65536'})

    def test_fixed_properties_and_real_value_obligations(self):
        prefix='#Box(x,data);#Pair(a:#[#Box((#u8)0,b.data)],b:#[#Box((#u16)0,a.data)]);'
        self.check(prefix+'^answer:#u16={v:#[#Pair(#Box((#u16)1,(#u8)2),#Box((#u16)2,(#u16)3)).a.data]=300;v};',{'answer':'300'})
        self.check(prefix+'^answer:#u16={p=#Pair(#Box((#u16)1,(#u8)2),#Box((#u16)2,(#u16)3));p.a.data};',error='without narrowing')
        self.check('#Box(x,data);'+PAIR+'^answer:#u16={v:#[#Pair(#Box((#u8)(1/0),missing()),#Box((#u16)2,other())).a.x]=300;v};',{'answer':'300'})
        self.check('#Box(x,data);'+PAIR+'^answer:#u16={p=#Pair(#Box((#u8)(1/0),(#u8)1),#Box((#u16)2,(#u8)2));p.a.x};',error='division by zero')

    def test_optional_finite_and_exhausted_contexts(self):
        query=CAPTURED+'v:#['+ARGS+'.a.x]=300;'
        self.check('^answer:#u16={r:#u16=7;#{'+query+'r=v;missing();};r};',{'answer':'7'})
        self.check('#Box(x,data);'+PAIR+'^answer:#u16=?{n=?(#b,[0,1]);?(n==0);'
            'n?{v:#[#Pair(#Box(missing(),missing()),#Box((#u16)2,other())).a.x]=300;};7};',{'answer':'7'},finite=True)
        self.check('^answer:#u16={'+query+'v};',error='budget',flags=('--max-steps','100'))

    def test_later_api_demand_must_discharge_schema(self):
        with tempfile.TemporaryDirectory(prefix='vexel-property-equation-api-') as directory:
            root=Path(directory);source=root/'main.vx';harness=root/'api.cpp';executable=root/'api'
            harness.write_text(r'''#include "vexel.hpp"
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=3) return 5;
    try {
        vx::Graph graph;graph.options.schedule=argv[2];vx::loadProgram(graph,argv[1]);
        vx::Solver solver(graph);solver.root("first");solver.solve();
        std::cout<<solver.report(true)<<'\n';solver.root("whole");
        try {solver.solve();return 3;}
        catch(const vx::Error& error) {return std::string(error.what()).find("uninitialized local")!=std::string::npos ? 0 : 4;}
    } catch(const std::exception& error) {std::cerr<<error.what();return 1;}
      catch(...) {return 2;}
}
''')
            objects=sorted(p for p in EXE.parent.glob('*.o') if p.name!='main.o')
            flags=shlex.split(os.environ.get('MSX_TEST_RUNNER_CXXFLAGS','-O1 -g'))
            run([*shlex.split(os.environ.get('CXX','c++')),'-std=c++20',*flags,'-I'+str(REPO/'include'),
                harness,*objects,'-ldl','-o',executable])
            source.write_text('state={'+CAPTURED+ARGS+'};first:#u16={v:#[state.a.x]=300;v};whole:#u8={v:#[state];7};')
            for schedule in ('fifo','lifo','random'):
                report=json.loads(run([executable,source,schedule]).stdout)
                self.assertEqual(report['roots'][0]['value'],'300')
                self.assertTrue(all(p['verified'] for p in report['type_properties']))

    def test_host_and_z80_preserve_initialization_and_queries(self):
        source_text='&!source()->#u8;#Box(x,data);'
        source_text+='a:#[#Box(b.x+1,b.data)]=#Box(source(),(#u8)1);b:#[#Box(a.x+1,a.data)]=#Box((#u16)2,(#u8)3);'
        source_text=source_text.replace('#Box','#RuntimeBox')
        source_text+='&^value()->#u16{a.x+b.x}&^query()->#u16{'+CAPTURED+'v:#['+ARGS+'.a.x]=300;v}'
        with tempfile.TemporaryDirectory(prefix='vexel-property-equation-backend-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(source_text);project=root/'project'
            run([EXE,source,'--emit-msx',project]);helper=root/'helper.c';library=root/'test.so'
            helper.write_text('#include "vexel.h"\nunsigned calls=0;uint8_t vx_ext_source(void) {++calls;return 10;}\n')
            run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(project/'generated'),'-I'+str(project/'runtime'),
                *sorted((project/'generated').glob('*.c')),project/'runtime/vx_runtime.c',helper,'-o',library])
            dll=ctypes.CDLL(str(library));calls=ctypes.c_uint.in_dll(dll,'calls')
            dll.vx_query.restype=ctypes.c_uint16;dll.vx_value.restype=ctypes.c_uint16
            self.assertEqual(dll.vx_query(),300);self.assertEqual(calls.value,0)
            self.assertEqual(dll.vx_value(),12);self.assertEqual(dll.vx_value(),12);self.assertEqual(calls.value,1)
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
