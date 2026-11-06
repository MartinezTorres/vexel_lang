#!/usr/bin/env python3
"""Cyclic interfaces are checked equations over reversible type candidates."""
import ctypes
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

from test_msx import EXE, project_environment, run

WIDTH='a:#[b+1]=(#u8)255;b:#[a+1]=(#u16)2;'

class TypeEquationTests(unittest.TestCase):
    def check(self,source,values=None,error=None,files=None,flags=(),finite=False):
        reports=[]
        with tempfile.TemporaryDirectory(prefix='vexel-type-equations-') as directory:
            root=Path(directory);path=root/'main.vx';path.write_text(source)
            for name,contents in (files or {}).items():
                (root/name).write_text(contents)
            for schedule in ('fifo','lifo','random'):
                for branch in (('forward','reverse') if finite else ('forward',)):
                    result=subprocess.run([str(EXE),str(path),'--json','--schedule',schedule,
                        '--branch-order',branch,*flags],capture_output=True,text=True,timeout=30)
                    if error:
                        self.assertEqual(result.returncode,1,(schedule,branch,result.stderr))
                        self.assertIn(error,result.stderr)
                    else:
                        self.assertEqual(result.returncode,0,(schedule,branch,result.stderr))
                        report=json.loads(result.stdout);reports.append(report)
                        self.assertEqual({r['name']:r['value'] for r in report['roots']},values)
                        self.assertTrue(all(e['verified'] for e in report['type_equations']))
        return reports

    def test_joint_widths_and_original_equation_proofs(self):
        for definitions in (WIDTH,'b:#[a+1]=(#u16)2;a:#[b+1]=(#u8)255;'):
            for roots in (('a','b'),('b','a')):
                reports=self.check(definitions,{'a':'255','b':'2'},flags=('--root',roots[0],'--root',roots[1]))
                for report in reports:
                    equations=report['type_equations']
                    self.assertEqual({e['name']:e['candidate'] for e in equations},{'a':'#u16','b':'#u16'})
                    nodes={n['id']:n for n in report['nodes']}
                    for equation in equations:
                        self.assertEqual(nodes[equation['check']]['rule'],'Type')
                        self.assertTrue(nodes[equation['check']]['demanded'])
                        self.assertEqual(nodes[equation['check']]['type'],equation['candidate'])
        self.check(WIDTH+'^answer:#u16=a+1;',{'answer':'256'})

    def test_helpers_arrays_and_numeric_families_use_source_rules(self):
        self.check('&copy(n){n}a:#[copy(b)+1]=(#u8)255;b:#[copy(a)+1]=(#u16)2;^answer:#u16=a+1;',{'answer':'256'})
        self.check('a:#[b[0]]=(#u8)255;b:#[[a]]=[(#u16)2];^answer:#u16=a+1;',{'answer':'256'})
        self.check('a:#[b+1]=(#f16)1;b:#[a+1]=(#f32)2;^answer:#f32=a+1;',{'answer':'2'})
        self.check('a:#[b==1]=1;b:#[a]=1;^answer:#b=a;',{'answer':'1'})
        self.check('a:#[b+1]=(#u8.8)1;b:#[a+1]=(#u8.8)2;^answer:#u8.8=a+1;',{'answer':'2'})

    def test_temporary_dispatch_errors_do_not_block_other_equations(self):
        self.check('&select(n:#u16){n}&select(n:#u32){n}'
            'a:#[select(b)]=(#u8)1;b:#[a+(#u32)0]=(#u8)2;^answer:#u32=a;',{'answer':'1'})
        # At the initial #u8 candidate b+1 wraps to zero. The #u16
        # requirement from b's equation removes the provisional division trap.
        self.check('a:#[{data:#u8[1/(b+1)];data}]=(#u8[0])[];'
            'b:#[a[0]+(#u16)0]=(#u8)255;^answer:#u8=|a|;',{'answer':'0'})

    def test_type_queries_never_execute_initializer_values(self):
        reports=self.check('a:#[b+1]=(#u8)(1/0);b:#[a+1]=(#u16)2;'
            '^answer:#u16={v:#[a]=300;v};',{'answer':'300'})
        for report in reports:
            divides=[n for n in report['nodes'] if n['rule']=='Binary' and n['operator']=='/']
            self.assertTrue(all(not n['demanded'] for n in divides))
            nodes={n['id']:n for n in report['nodes']}
            self.assertTrue(all(not nodes[e['owner']]['demanded'] for e in report['type_equations']))
        self.check('a:#[b+1]=(#u8)(1/0);b:#[a+1]=(#u16)2;^answer:#u16=a;',error='division by zero')
        self.check('a:#[b+1]=300;b:#[a+1]=(#u8)2;^answer:#u16=a;',error='not representable')

    def test_optional_checkpoints_revise_the_owning_context(self):
        self.check(WIDTH+'^first:#u16={r:#u16=7;#{r=a+1;};r};^second:#u16=a+1;',{'first':'256','second':'256'})
        self.check(WIDTH+'^type:#u16={v:#[a]=300;v};^answer:#u16={r:#u16=7;#{#{r=a+1;};};r};',{'type':'300','answer':'256'})
        self.check(WIDTH+'^first:#u16={r:#u16=7;#{r=a+1;missing();};r};^second:#u16=a+1;',{'first':'7','second':'256'})

    def test_structural_revisions_and_separate_components(self):
        self.check(WIDTH+'^answer:#u16=?{n=?(#u8,[0,1]);data:#u8[n]=[0];a+1};',{'answer':'256'},finite=True)
        self.check(WIDTH+'^answer:#u16={v:#[a]=0;?{n=?(#u8,[0,1]);data:#u8[n]=[0];a+1}};',{'answer':'256'},finite=True)
        self.check(WIDTH+'c:#[d+1]=(#u8)1;d:#[c+1]=(#u32)2;^answer:#u32=a+c+1;',{'answer':'257'})

    def test_import_order_and_demand_order(self):
        files={'first.vx':'a:#[b+1]=(#u8)255;','second.vx':'b:#[a+1]=(#u16)2;'}
        for imports in ('::first;::second;','::second;::first;'):
            self.check(imports+'^answer:#u16=a+1;',{'answer':'256'},files=files)
        reports=self.check('a:#[b+1]=missing();b:#[a+1]=missing();^answer:#u8=7;',{'answer':'7'})
        self.assertTrue(all(not r['type_equations'] for r in reports))

    def test_final_revision_owns_its_input_dependencies(self):
        reports=self.check('a:#[{data:#u8[(b+1)==0?|::provisional.bin|:0];data}]=(#u8[0])[];'
            'b:#[a[0]+(#u16)0]=(#u8)255;^answer:#u8=|a|;',{'answer':'0'},files={'provisional.bin':'abc'})
        self.assertTrue(all(not any(p.endswith('provisional.bin') for p in r['dependencies']) for r in reports))
        reports=self.check('a:#[{data:#u8[|::final.bin|+b*0];data}]=(#u8[3])[1,2,3];'
            'b:#[a[0]+(#u16)0]=(#u8)255;^answer:#u8=|a|;',{'answer':'3'},files={'final.bin':'abc'})
        self.assertTrue(all(any(p.endswith('final.bin') for p in r['dependencies']) for r in reports))

    def test_unanchored_value_cycles_and_inconsistent_equations(self):
        self.check('a:#[b+1]=b;b:#[a+1]=a;^answer:#u8=a;',error='unresolved dependency cycle')
        self.check('a:#[b+1];b:#[a+1];^answer:#u8={v:#[a]=7;v};',error='unresolved dependency cycle')
        self.check('a:#[b==1]=(#u8)1;b:#[a]=(#u8)1;^answer:#u8=a;',error='incompatible types')

    def test_budget_exhaustion_is_not_optional_absence(self):
        self.check(WIDTH+'^answer:#u16=a+1;',error='step budget',flags=('--max-steps','100'))
        self.check(WIDTH+'^answer:#u16={r:#u16=7;#{r=a+1;};r};',error='step budget',flags=('--max-steps','100'))
        self.check(WIDTH+'^answer:#u16=?{n=?(#u16,[255,256]);?(n==a+1);n};',error='step budget',flags=('--max-steps','100'),finite=True)

    def test_later_independent_roots_keep_previously_validated_components(self):
        with tempfile.TemporaryDirectory(prefix='vexel-equations-late-root-') as directory:
            root=Path(directory);source=root/'main.vx';harness=root/'main.cpp';executable=root/'check'
            source.write_text(WIDTH+'c:#[d+1]=(#u8)255;d:#[c+1]=(#u16)2;'
                'first:#u16=a+1;second:#u16=c+1;')
            harness.write_text('''#include "vexel.hpp"
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=5) return 2;
    try {
        vx::Graph graph;graph.options.schedule=argv[2];vx::loadProgram(graph,argv[1]);
        vx::Solver solver(graph);solver.root(argv[3]);solver.solve();
        solver.root(argv[4]);solver.solve();solver.solve();std::cout<<solver.report(true);
    } catch(const std::exception& error) {std::cerr<<error.what();return 1;}
      catch(...) {std::cerr<<"unhandled revision request";return 2;}
}
''')
            objects=sorted(p for p in EXE.parent.glob('*.o') if p.name!='main.o')
            self.assertTrue(objects,'this API regression requires the Makefile build objects')
            flags=shlex.split(os.environ.get('MSX_TEST_RUNNER_CXXFLAGS','-O1 -g'))
            run([*shlex.split(os.environ.get('CXX','c++')),'-std=c++20',*flags,
                '-I'+str(Path(__file__).resolve().parents[1]/'include'),harness,*objects,'-ldl','-o',executable])
            for schedule in ('fifo','lifo','random'):
                for first,second in (('first','second'),('second','first')):
                    result=run([executable,source,schedule,first,second])
                    report=json.loads(result.stdout)
                    self.assertEqual({r['name']:r['value'] for r in report['roots']},{'first':'256','second':'256'})
                    self.assertEqual(len(report['type_equations']),4)
                    self.assertTrue(all(e['verified'] for e in report['type_equations']))

    def test_resolved_interfaces_reach_host_and_z80_code(self):
        with tempfile.TemporaryDirectory(prefix='vexel-equations-backend-') as directory:
            root=Path(directory);source=root/'main.vx';project=root/'project'
            source.write_text(WIDTH+'&~plus(n:#[a])->#[b]{n+1}&^answer(n:#u16)->#u16{plus(n)+a+1}')
            run([EXE,source,'--emit-msx',project])
            library=root/'test.so'
            run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(project/'generated'),'-I'+str(project/'runtime'),
                *sorted((project/'generated').glob('*.c')),project/'runtime/vx_runtime.c','-o',library])
            answer=ctypes.CDLL(str(library)).vx_answer
            answer.argtypes=[ctypes.c_uint16];answer.restype=ctypes.c_uint16
            for n in (0,255,256,65535):
                self.assertEqual(answer(n),(n+257)%65536)
            (project/'user/main.c').write_text('#include "vexel.h"\nvolatile uint32_t vx_last_result;\n'
                'void main(void) VX_ABI VX_RESIDENT {vx_last_result=vx_answer(255);__asm halt __endasm;}\n')
            run(['make','-C',project,'-j2','simulate'],env=project_environment(),timeout=180)
            self.assertEqual(json.loads((project/'build/execution.json').read_text())['observation']['last_result'],512)

if __name__=='__main__':unittest.main(verbosity=2)
