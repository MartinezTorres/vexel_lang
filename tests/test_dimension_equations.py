#!/usr/bin/env python3
"""Initializer shapes are candidates; original dimensions remain obligations."""
import ctypes
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

from test_msx import EXE, REPO, project_environment, run

PAIR='a:#u8[|b|]=[1,2,3];b:#u8[|a|]=[4,5,6];'
FINITE='a:#u8[|b|]=[1,2];b:#u8[?{n=?(#u8,[1,2,3]);?(n==|a|);n}]=[4,5];'
RUNTIME='''
&!source()->#u8;
a:#u8[|b|]=[source(),2,3];b:#u8[|a|]=[4,5,6];
#P(x,data:#u8[x]);
c:#P[|d|]=[#P((#u8)2,[1,2]),#P((#u8)2,[3,4])];
d:#P[|c|]=[#P((#u8)2,[5,6]),#P((#u8)2,[7,8])];
&^answer(n:#u8)->#u16{a[0]+b[n]+c[0].data[1]+d[1].data[0]}
&^query()->#u8{v:#[a[0]]=7;v}
'''


class DimensionEquationTests(unittest.TestCase):
    def check(self,source,values=None,error=None,files=None,flags=(),finite=False):
        reports=[]
        with tempfile.TemporaryDirectory(prefix='vexel-dimension-equations-') as directory:
            root=Path(directory);path=root/'main.vx';path.write_text(source)
            for name,content in (files or {}).items():(root/name).write_text(content)
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

    def test_global_dimensions_and_declaration_order(self):
        for pair in (PAIR,'b:#u8[|a|]=[4,5,6];a:#u8[|b|]=[1,2,3];'):
            reports=self.check(pair+'^answer:#u8=a[0]+b[0];',{'answer':'5'})
            self.assertTrue(all(r['type_equations'] for r in reports))
        self.check('a:#u8[|b|]=[];b:#u8[|a|]=[];^answer:#u8=|a|;',{'answer':'0'})

    def test_nested_dimensions_do_not_require_leaf_values_or_types(self):
        self.check('a:#u8[|b|][|b[0]|]=[[1,2,3],[4,5,6]];'
            'b:#u8[|a|][|a[0]|]=[[1,2],[3,4],[5,6]];^answer:#u8=a[0][0]+b[0][0];',{'answer':'2'})
        self.check('a:#u8[|b|][|b[0]|]=[[missing(),missing()],[other(),other()]];'
            'b:#u8[|a|][|a[0]|]=[[1,2],[3,4]];^answer:#u8={v:#[a[0][0]]=7;v};',{'answer':'7'})

    def test_declared_element_families_and_value_checks(self):
        prefix='a:#u8[|b|]=[(#u16)256,2];b:#u8[|a|]=[4,5];'
        self.check(prefix+'^answer:#u8={v:#[a[0]]=7;v};',{'answer':'7'})
        self.check(prefix+'^answer:#u8=a[0];',error='narrowing')
        prefix='a:#u8[|b|]=[1/0,2];b:#u8[|a|]=[4,5];'
        self.check(prefix+'^answer:#u8={v:#[a[0]]=7;v};',{'answer':'7'})
        self.check(prefix+'^answer:#u8=a[0];',error='division by zero')
        self.check('a:#u8[|b|]=[missing(),other()];b:#u8[|a|]=[4,5];'
            '^answer:#u8={v:#[a[0]]=7;v};',{'answer':'7'})

    def test_original_shape_requirements_still_reject_mismatches(self):
        self.check('a:#u8[|b|]=[1,2];b:#u8[|a|]=[3,4,5];^answer:#u8=|a|;',error='incompatible types')
        self.check('a:#u8[|b|][2]=[1,2];b:#u8[|a|]=[3,4];^answer:#u8=|a|;',error='array shape')
        self.check('a:#u8[|b|]=[1,2];b:#u8[|a|+1]=[3,4];^answer:#u8=|a|;',error='incompatible types')

    def test_record_element_requirements_and_invariants(self):
        self.check('#P(x);a:#P[|b|]=[#P((#u8)1),#P((#u8)2)];'
            'b:#P[|a|]=[#P((#u8)3),#P((#u8)4)];^answer:#u8=a[0].x+b[0].x;',{'answer':'4'})
        prefix='#P(x,data:#u8[x]);a:#P[|b|]=[#P((#u8)2,[1,2]),#P((#u8)2,[3,4])];'
        suffix='b:#P[|a|]=[#P((#u8)2,[5,6]),#P((#u8)2,[7,8])];^answer:#u8=a[0].data[0]+b[0].data[0];'
        self.check(prefix+suffix,{'answer':'6'})
        self.check(prefix.replace('[1,2]','[1]')+suffix,error='dimension does not match')

    def test_helpers_and_queried_element_requirements(self):
        self.check('&extent(data){|data|}a:#u8[extent(b)]=[1,2];b:#u8[extent(a)]=[3,4];'
            '^answer:#u8=a[0]+b[0];',{'answer':'4'})
        self.check('a:#[b[0]][|b|]=[(#u16)255,2];b:#u8[|a|]=[4,5];'
            '^answer:#u8={v:#[a[0]]=7;v};',{'answer':'7'})

    def test_scoped_instances_and_import_order(self):
        self.check('^first:#u8={&items(){[1,2]}::part;|a|};'
            '^second:#u8={&items(){[3,4,5]}::part;|a|};',{'first':'2','second':'3'},
            files={'part.vx':'a:#u8[|b|]=items();b:#u8[|a|]=items();'})
        for imports in ('::left;::right;','::right;::left;'):
            self.check('^answer:#u8={'+imports+'a[0]+b[0]};',{'answer':'5'},
                files={'left.vx':'a:#u8[|b|]=[1,2];','right.vx':'b:#u8[|a|]=[4,5];'})

    def test_optional_failure_restores_shape_candidates(self):
        self.check('^first:#u8={r:#u8=7;#{::bad;r=|a|;missing();};r};^second:#u8={::good;a[0]+b[0]};',
            {'first':'7','second':'5'},files={'bad.vx':'a:#u8[|b|]=[1,2];b:#u8[|a|]=[3,4];','good.vx':PAIR})

        self.check('^answer:#u8={#{::bad;|a|;};7};',error='incompatible types',
            files={'bad.vx':'a:#u8[|b|]=[1,2];b:#u8[|a|+1]=[3,4];'})

    def test_finite_queries_keep_their_uniqueness_and_exhaustion_rules(self):
        self.check(FINITE+'^answer:#u8=a[0]+b[0];',{'answer':'5'},finite=True)
        self.check(FINITE.replace('[1,2,3]','[1,3]')+'^answer:#u8=a[0]+b[0];',error='no solution',finite=True)
        self.check(FINITE.replace('?(n==|a|);','')+'^answer:#u8=a[0]+b[0];',error='ambiguous',finite=True)
        self.check(PAIR+'^answer:#u8=a[0]+b[0];',error='budget',flags=('--max-steps','20'))
        self.check('^answer:#u8={r:#u8=7;#{::part;r=|a|;};r};',error='budget',
            files={'part.vx':PAIR},flags=('--max-steps','20'))

    def test_later_roots_preserve_verified_shapes(self):
        with tempfile.TemporaryDirectory(prefix='vexel-dimensions-api-') as directory:
            root=Path(directory);source=root/'main.vx';harness=root/'main.cpp';executable=root/'check'
            source.write_text(PAIR+'c:#u8[|d|]=[7,8];d:#u8[|c|]=[9,10];'
                'first:#u8=a[0]+b[0];second:#u8=c[0]+d[0];')
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
            self.assertTrue(objects)
            flags=shlex.split(os.environ.get('MSX_TEST_RUNNER_CXXFLAGS','-O1 -g'))
            run([*shlex.split(os.environ.get('CXX','c++')),'-std=c++20',*flags,'-I'+str(REPO/'include'),
                harness,*objects,'-ldl','-o',executable])
            for schedule in ('fifo','lifo','random'):
                for first,second in (('first','second'),('second','first')):
                    report=json.loads(run([executable,source,schedule,first,second]).stdout)
                    self.assertEqual({r['name']:r['value'] for r in report['roots']},{'first':'5','second':'16'})
                    self.assertTrue(all(e['verified'] for e in report['type_equations']))

    def test_host_persistent_initializers_and_type_only_queries(self):
        with tempfile.TemporaryDirectory(prefix='vexel-dimensions-host-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            run([EXE,source,'--emit-msx',project]);helper=root/'helper.c';library=root/'test.so'
            helper.write_text('#include "vexel.h"\nunsigned calls=0;uint8_t vx_ext_source(void) {++calls;return 10;}\n')
            run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(project/'generated'),'-I'+str(project/'runtime'),
                *sorted((project/'generated').glob('*.c')),project/'runtime/vx_runtime.c',helper,'-o',library])
            dll=ctypes.CDLL(str(library));calls=ctypes.c_uint.in_dll(dll,'calls')
            dll.vx_query.restype=ctypes.c_uint8;dll.vx_answer.argtypes=[ctypes.c_uint8];dll.vx_answer.restype=ctypes.c_uint16
            self.assertEqual(dll.vx_query(),7);self.assertEqual(calls.value,0)
            self.assertEqual([dll.vx_answer(n) for n in (0,1,2,0)],[23,24,25,23]);self.assertEqual(calls.value,1)

    def test_z80_shape_resolution_and_initialization(self):
        with tempfile.TemporaryDirectory(prefix='vexel-dimensions-z80-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            run([EXE,source,'--emit-msx',project])
            (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
uint8_t calls=0;
uint8_t vx_ext_source(void) VX_ABI {++calls;return 10;}
void main(void) VX_ABI VX_RESIDENT {
    vx_last_result=vx_query()+calls*100u;
    vx_last_result+=vx_answer(0)+vx_answer(1)+vx_answer(2);
    vx_last_result+=calls*1000u;
    __asm halt __endasm;
}
''')
            run(['make','-C',project,'-j2','simulate'],env=project_environment(),timeout=180)
            self.assertEqual(json.loads((project/'build/execution.json').read_text())['observation']['last_result'],1079)


if __name__=='__main__':unittest.main(verbosity=2)
