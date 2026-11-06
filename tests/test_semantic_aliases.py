#!/usr/bin/env python3
"""Prove cyclic type equalities without assuming a complete record schema."""
import ctypes
import json
import os
from pathlib import Path
import shlex
import tempfile
import unittest

import test_context_equations as context
from test_msx import EXE, REPO, project_environment, run

PAIR='#Pair(a:#[forward(b)],b:#[forward(a)]);&forward(value){value}'
GLOBALS='#Box(x,y);&forward(value){value}a:#[forward(b)]=#Box((#u8)1,missing());b:#[forward(a)]=#Box((#u16)2,other());'
CAPTURED='unknown:#u8;#Box(x,data:#u8[x+unknown]);'+PAIR
ARGS='#Pair(#Box((#u8)2,[1,2,3]),#Box((#u16)2,[1,2,3]))'
RUNTIME='''
&!source()->#u8;
&forward(item){item}
#Box(x,y);
a:#[forward(b)]=#Box(source(),(#u8)1);
b:#[forward(a)]=#Box((#u16)2,(#u8)3);
&^value()->#u16{a.x+b.x}
&^query()->#u16{
    unknown:#u8;
    #Captured(x,data:#u8[x+unknown]);
    #Pair(a:#[forward(b)],b:#[forward(a)]);
    v:#[#Pair(#Captured((#u8)2,[1,2,3]),#Captured((#u16)2,[1,2,3])).a.x]=300;
    v
}
'''


class SemanticAliasTests(unittest.TestCase):
    check=context.ContextEquationTests.check

    def test_captured_schema_and_unproved_siblings(self):
        reports=self.check('^answer:#u16={'+CAPTURED+'v:#['+ARGS+'.a.x]=300;v};',{'answer':'300'})
        for report in reports:
            self.assertEqual(len(report['type_aliases']),2)
            self.assertFalse(report['type_equations'])
            nodes={n['id']:n for n in report['nodes']}
            for proof in report['type_aliases']:
                self.assertEqual(proof['proof'],'opaque-type-equality')
                self.assertIn(proof['target'],nodes)
                self.assertEqual(nodes[proof['join']]['rule'],'TypeJoin')
                self.assertTrue(nodes[proof['join']]['schema_obligations'])
                self.assertTrue(all(not nodes[i]['demanded'] for i in nodes[proof['join']]['schema_obligations']))
        self.check('^answer:#u16={'+CAPTURED+'v:#['+ARGS+'];7};',error='uninitialized local')
        self.check('^answer:#u16={'+CAPTURED+ARGS+'.a.x};',error='uninitialized local')
        self.check('#Box(x,y);'+PAIR+'^answer:#u16={v:#['
            '#Pair(#Box((#u8)1,missing()),#Box((#u16)2,other())).a.x]=300;v};',{'answer':'300'})

    def test_ordinary_helpers_tuple_projection_and_expression_parameters(self):
        for helper in ('&forward(value){value}',
                       '&pass(value){value}&forward(value){pass(value)}',
                       '&forward(value){(value,(#u8)1).__0}',
                       '&pass($value){$value}&forward(value){pass(value)}'):
            self.check('#Box(x,y);#Pair(a:#[forward(b)],b:#[forward(a)]);'+helper+
                '^answer:#u16={v:#[#Pair(#Box((#u8)1,missing()),#Box((#u16)2,other())).a.x]=300;v};',{'answer':'300'})
        self.check('#Box(x,y);&forward(value){value}#Pair(b:#[forward(a)],a:#[forward(b)]);'
            '^answer:#u16={v:#[#Pair(#Box((#u16)2,other()),#Box((#u8)1,missing())).a.x]=300;v};',{'answer':'300'})

    def test_global_and_scoped_declaration_groups(self):
        self.check(GLOBALS+'^answer:#u16={v:#[a.x]=300;v};',{'answer':'300'})
        self.check('#Box(x,y);&forward(value){value}b:#[forward(a)]=#Box((#u16)2,other());'
            'a:#[forward(b)]=#Box((#u8)1,missing());^answer:#u16={v:#[a.x]=300;v};',{'answer':'300'})
        module='#Box(x,y);a:#[forward(b)]=#Box(width(1),missing());b:#[forward(a)]=#Box(width(2),other());'
        self.check('&forward(value){value}'
            '^first:#u16={&width(n){(#u16)n}::part;v:#[a.x]=300;v};'
            '^second:#u32={&width(n){(#u32)n}::part;v:#[a.x]=65536;v};',
            {'first':'300','second':'65536'},files={'part.vx':module})
        source='&forward(value){value}a:#[forward(b)]=(#u8)255;b:#[forward(a)]=(#u16)2;'
        for roots in (('a','b'),('b','a')):
            self.check(source,{'a':'255','b':'2'},flags=('--root',roots[0],'--root',roots[1]))

    def test_all_component_witnesses_contribute(self):
        self.check('#Box(x,y);&forward(item){item}'
            '#Pair(a:#[forward(b)],b:#[forward(c)],c:#[forward(a)],d:#[forward(c)]);'
            '^answer:#u32={v:#[#Pair(#Box((#u8)1,missing()),#Box((#u8)2,other()),'
            '#Box((#u16)3,third()),#Box((#u32)4,fourth())).a.x]=65536;v};',{'answer':'65536'})
        # A probe cannot turn an opaque type into evidence of absence.
        reports=self.check('&forward(item){item.?x?(#u16)0:item}'
            '#Pair(a:#[forward(b)],b:#[forward(a)]);'
            '^answer:#u16={p=#Pair((#u8)255,(#u16)2);p.a+1};',{'answer':'256'})
        self.assertTrue(all(not r['type_aliases'] for r in reports))

    def test_array_shape_and_nested_fields(self):
        self.check(PAIR+'^answer:#u8={v:#u8[|#Pair([missing(),other()],[other(),missing()]).a|]=[1,2];v[1]};',{'answer':'2'})
        self.check('#Box(x,y);#Wrap(box);'+PAIR+'^answer:#u32={v:#['
            '#Pair(#Wrap(#Box((#u8)1,missing())),#Wrap(#Box((#u32)2,other()))).a.box.x]=65536;v};',{'answer':'65536'})
        self.check(PAIR+'^answer:#u16={v:#['
            '#Pair([((#u8)1,missing())],[((#u16)2,other())]).a[0].__0]=300;v};',{'answer':'300'})

    def test_original_captured_constraints_and_dimensions(self):
        prefix='^answer:#u16={offset:#u8=1;#Box(x,data:#u8[x+offset]);'+PAIR
        prefix+='one=#Box((#u8)2,[1,2,3]);offset=2;two=#Box((#u16)2,[1,2,3,4]);'
        self.check(prefix+'v:#[#Pair(one,two).a.x]=300;v};',{'answer':'300'})
        self.check(prefix+'v:#[#Pair(one,two)];7};',error='different captured schema')
        self.check(prefix+'#Pair(one,two).a.x};',error='different captured schema')
        prefix='^answer:#u16={offset:#u8=1;#Box(x,data:#u8[x+offset]);'+PAIR
        self.check(prefix+'#Pair(#Box((#u8)2,[1,2]),#Box((#u16)2,[1,2])).a.x};',error='dimension does not match')

    def test_non_equal_requirements_are_not_inferred_as_aliases(self):
        for helper in ('&forward(value){(#u8)value}',
                       '&forward(value){value}&forward(value:#u16){(#u8)value}'):
            self.check(helper+'#Pair(a:#[forward(b)],b:#[forward(a)]);'
                '^answer:#u16={v:#[#Pair((#u16)1,(#u16)2).a]=300;v};',error='do not admit')
        reports=self.check('&forward(value){value+(#u32)0}#Pair(a:#[forward(b)],b:#[forward(a)]);'
            '^answer:#u32={p=#Pair((#u8)255,(#u16)2);p.a+1};',{'answer':'256'})
        self.assertTrue(all(not r['type_aliases'] and r['type_equations'] for r in reports))
        self.check('#Box(x);#Other(x);'+PAIR+'^answer:#u8={v:#['
            '#Pair(#Box((#u8)1),#Other((#u8)2)).a.x]=7;v};',error='incompatible types')
        # A function's result may depend on the supplied value, even though
        # there is an unconstrained parameter and no declared result type.
        self.check('&choose(value){value!=0?(#u8)0:(#u16)0}'
            '#Pair(a:#[choose(b)],b:#[a+1]);^answer:#u16={p=#Pair((#u8)255,(#u8)0);p.a+1};',{'answer':'256'})

    def test_strict_call_boundaries_remain_obligations(self):
        source=PAIR+'^answer:#u16={v:#[#Pair((1,2),(3,4)).a.__0]=7;v};'
        self.check(source,{'answer':'7'},flags=('--strictness','1'))
        self.check(source,error='strictness 2 requires concrete arguments',flags=('--strictness','2'))
        self.check(PAIR+'^answer:#u16={v:#['
            '#Pair(((#u8)1,(#u8)2),((#u16)3,(#u8)4)).a.__0]=300;v};',
            {'answer':'300'},flags=('--strictness','2'))
        self.check('&forward(item){item}^answer:#u16={v:#[forward((1,2)).__0]=7;v};',
            error='strictness 2 requires concrete arguments',flags=('--strictness','2'))

    def test_finite_branches_optional_rollback_and_budget(self):
        self.check('#Box(x,y);'+PAIR+'^answer:#u16=?{n=?(#b,[0,1]);v:#['
            '#Pair(n?#Box((#u8)1,missing()):#Box((#u16)1,other()),#Box((#u8)2,third())).a.x]=7;(#u16)v};',
            {'answer':'7'},finite=True)
        reports=self.check('^first:#u16={r:#u16=7;#{::part;v:#[a.x]=300;r=v;missing();};r};'
            '^second:#u16={::part;v:#[a.x]=300;v};',{'first':'7','second':'300'},files={'part.vx':GLOBALS})
        self.assertTrue(all(len(r['type_aliases'])==2 for r in reports))
        self.check('^answer:#u16={r:#u16=7;#{::part;v:#[a.x]=300;r=v;};r};',
            error='budget',files={'part.vx':GLOBALS},flags=('--max-steps','100'))
        self.check('#Box(x);'+PAIR+'^answer:#u16={p=#Pair(#Box((#u8)(1/0)),#Box((#u16)2));p.a.x};',error='division by zero')

    def test_later_whole_demand_preserves_obligations(self):
        with tempfile.TemporaryDirectory(prefix='vexel-semantic-alias-api-') as directory:
            root=Path(directory);source=root/'main.vx';harness=root/'main.cpp';executable=root/'check'
            harness.write_text('''#include "vexel.hpp"
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=4) return 2;
    try {
        vx::Graph graph;graph.options.schedule=argv[2];vx::loadProgram(graph,argv[1]);
        vx::Solver solver(graph);solver.root("first");solver.solve();
        std::cout<<solver.report(true)<<'\\n';solver.root("whole");
        try {solver.solve();return 3;}
        catch(const vx::Error& error) {return std::string(error.what()).find(argv[3])!=std::string::npos ? 0 : 4;}
    } catch(const std::exception& error) {std::cerr<<error.what();return 1;}
      catch(...) {return 2;}
}
''')
            objects=sorted(p for p in EXE.parent.glob('*.o') if p.name!='main.o')
            flags=shlex.split(os.environ.get('MSX_TEST_RUNNER_CXXFLAGS','-O1 -g'))
            run([*shlex.split(os.environ.get('CXX','c++')),'-std=c++20',*flags,'-I'+str(REPO/'include'),
                harness,*objects,'-ldl','-o',executable])
            cases=[(GLOBALS+'first:#u16={v:#[a.x]=300;v};whole:#u8={v:#[a];7};','undefined function'),
                ('state={'+CAPTURED+ARGS+'};first:#u16={v:#[state.a.x]=300;v};whole:#u8={v:#[state];7};','uninitialized local')]
            for text,error in cases:
                source.write_text(text)
                for schedule in ('fifo','lifo','random'):
                    report=json.loads(run([executable,source,schedule,error]).stdout)
                    self.assertEqual(report['roots'][0]['value'],'300')
                    self.assertEqual(len(report['type_aliases']),2)

    def test_host_and_z80_keep_runtime_initialization(self):
        with tempfile.TemporaryDirectory(prefix='vexel-semantic-alias-backend-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
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
