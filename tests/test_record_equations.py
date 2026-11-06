#!/usr/bin/env python3
"""Record candidates preserve pending fields, frozen schemas and proof ownership."""
import ctypes
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import test_context_equations as context
from test_msx import EXE, project_environment, run

BOX='#Box(x);'
PAIR=BOX+'#P(a:#[#Box(b.x+1)],b:#[#Box(a.x+1)]);'
GLOBALS=BOX+'a:#[#Box(b.x+1)]=#Box((#u8)255);b:#[#Box(a.x+1)]=#Box((#u16)2);'
CHOICE=BOX+'&choose(n){(n!=0)?(#u8)0:(#u16)0}#P(a:#[#Box(choose(b.x))],b:#[#Box(a.x+1)]);'
RUNTIME=PAIR+'''
&~make(n:#u8)->#P{#P(#Box(n),#Box((#u16)2))}
&^next(n:#u8)->#u16{make(n).a.x+1}
&^captured(n:#u8)->#u16{
    offset:#u8=1;
    #Packet(count,data:#u8[count+offset]);
    #Pair(a:#[copy(b)],b:#[copy(a)]);
    &copy(value){value}
    p=#Pair(#Packet(n,[4,5,6]),#Packet((#u16)2,[1,2,3]));
    p.a.count+p.a.data[0]
}
&~walk(n:#u8)->#P{n==0?#P(#Box((#u8)255),#Box((#u16)2)):walk(n-1)}
&^recursive(n:#u8)->#u16{walk(n).a.x+1}
'''


class RecordEquationTests(unittest.TestCase):
    check=context.ContextEquationTests.check

    def test_field_order_separate_instances_and_proof_nodes(self):
        self.check(PAIR+'^answer:#u16={p=#P(#Box((#u8)255),#Box((#u16)2));p.a.x+1};',{'answer':'256'})
        self.check(BOX+'#P(b:#[#Box(a.x+1)],a:#[#Box(b.x+1)]);'
            '^answer:#u16={p=#P(#Box((#u16)2),#Box((#u8)255));p.a.x+1};',{'answer':'256'})
        reports=self.check(PAIR+'^one:#u16={p=#P(#Box((#u8)255),#Box((#u16)2));p.a.x+1};'
            '^two:#u32={p=#P(#Box((#u16)65535),#Box((#u32)2));p.a.x+1};',{'one':'256','two':'65536'})
        for report in reports:
            self.assertEqual({e['candidate'].removeprefix('#Box(').removesuffix(')')
                              for e in report['type_equations']+report.get('type_properties',[])},{'#u16','#u32'})
            nodes={n['id']:n for n in report['nodes']}
            for equation in report['type_equations']+report.get('type_properties',[]):
                self.assertEqual(nodes[equation['check']]['rule'],'TypeJoin' if 'path' in equation else 'Type')
                self.assertTrue(equation['verified'])
                self.assertEqual(nodes[equation['check']]['type'],equation['candidate'])

    def test_global_records_and_lexical_module_instances(self):
        for roots in (('a','b'),('b','a')):
            self.check(GLOBALS,{'a':'[255]','b':'[2]'},flags=('--root',roots[0],'--root',roots[1]))
        for imports in ('::first;::second;','::second;::first;'):
            self.check(BOX+imports+'^answer:#u16=a.x+1;',{'answer':'256'},files={
                'first.vx':'a:#[#Box(b.x+1)]=#Box((#u8)255);',
                'second.vx':'b:#[#Box(a.x+1)]=#Box((#u16)2);'})
        reports=self.check(BOX+'^one:#u16={&w(n){(#u16)n}::part;a.x+1};'
            '^two:#u32={&w(n){(#u32)n}::part;a.x+1};',{'one':'256','two':'256'},files={
                'part.vx':'a:#[#Box(w(b.x))]=#Box((#u8)255);b:#[#Box(a.x+1)]=#Box((#u8)2);'})
        for report in reports:
            self.assertEqual({e['candidate'].removeprefix('#Box(').removesuffix(')')
                              for e in report['type_equations']+report.get('type_properties',[])},{'#u16','#u32'})

    def test_nested_records_tuples_and_arrays(self):
        self.check(BOX+'#Wrap(box);#P(a:#[#Wrap(#Box(b.box.x+1))],b:#[#Wrap(#Box(a.box.x+1))]);'
            '^answer:#u16={p=#P(#Wrap(#Box((#u8)255)),#Wrap(#Box((#u16)2)));p.a.box.x+1};',{'answer':'256'})
        self.check('#P(a:#[(b.__0+1,(#u8)0)],b:#[(a.__0+1,(#u8)0)]);'
            '^answer:#u16={p=#P(((#u8)255,(#u8)1),((#u16)2,(#u8)3));p.a.__0+1};',{'answer':'256'})
        self.check(BOX+'#P(a:#[[b[0]]],b:#[[a[0]]]);'
            '^answer:#u16={p=#P([#Box((#u8)255)],[#Box((#u16)2)]);p.a[0].x+1};',{'answer':'256'})

    def test_fixed_fields_remain_constraints(self):
        prefix='#Box(x,y);#P(a:#[#Box((#u16)0,b.y)],b:#[#Box((#u16)0,a.y)]);'
        self.check(prefix+'^answer:#u16={p=#P(#Box((#u8)1,(#u8)255),#Box((#u8)2,(#u16)3));p.a.y+p.a.x};',{'answer':'256'})
        prefix='#Box(x,y);#P(a:#[#Box((#u8)0,b.y)],b:#[#Box((#u16)0,a.y)]);'
        arguments='#P(#Box((#u16)1,(#u8)255),#Box((#u16)2,(#u16)3))'
        self.check(prefix+'^answer:#u16={v:#['+arguments+'.a.y]=300;v};',{'answer':'300'})
        self.check(prefix+'^answer:#u16={p='+arguments+';p.a.y};',error='without narrowing')

    def test_partial_fields_do_not_demand_unused_siblings(self):
        prefix='#Box(x,y);#P(a:#[#Box((#u8)0,b.y)],b:#[#Box((#u8)0,a.y)]);'
        field='^first:#u8={v:#[#P(#Box((#u8)1,missing()),#Box((#u8)2,missing())).a.x]=7;v};'
        global_='#Box(x,y);a:#[#Box((#u8)1,b.y)]=#Box((#u8)1,missing());'
        global_+='b:#[#Box((#u8)1,a.y)]=#Box((#u8)2,missing());^first:#u8={v:#[a.x]=7;v};'
        for source in (prefix+field,global_):
            reports=self.check(source,{'first':'7'})
            self.assertTrue(all(not r['type_equations'] for r in reports))
            self.check(source+'c:#[d+1]=(#u8)255;d:#[c+1]=(#u16)2;^second:#u16=c+1;',{'first':'7','second':'256'})

    def test_frozen_captured_schemas_are_preserved(self):
        prefix='^answer:#u16={offset:#u8=1;#Box(n,data:#u8[n+offset]);#P(a:#[copy(b)],b:#[copy(a)]);&copy(x){x}'
        self.check(prefix+'p=#P(#Box((#u8)2,[1,2,3]),#Box((#u16)2,[4,5,6]));p.a.n+1};',{'answer':'3'})
        self.check(prefix+'v:#[#P(#Box((#u8)(1/0),[1,2,3]),#Box((#u16)2,[4,5,6])).a.n]=300;v};',{'answer':'300'})
        self.check(prefix+'one=#Box((#u8)2,[1,2,3]);offset=2;two=#Box((#u16)2,[1,2,3,4]);p=#P(one,two);p.a.n};',error='different captured schema')

    def test_type_only_queries_and_value_validation(self):
        reports=self.check(BOX+'a:#[#Box(b.x+1)]=#Box((#u8)(1/0));b:#[#Box(a.x+1)]=#Box((#u16)2);'
            '^answer:#u16={v:#[a.x]=300;v};',{'answer':'300'})
        for report in reports:
            self.assertTrue(all(not n['demanded'] for n in report['nodes'] if n['rule']=='Binary' and n['operator']=='/'))
        self.check(PAIR+'^answer:#u16={v:#[#P(#Box((#u8)(1/0)),#Box((#u16)2)).a.x]=300;v};',{'answer':'300'})
        self.check(PAIR+'^answer:#u16={p=#P(#Box((#u8)(1/0)),#Box((#u16)2));p.a.x};',error='division by zero')

    def test_temporary_overload_errors_are_revised(self):
        self.check(BOX+'&select(n:#u16){n}&select(n:#u32){n}'
            '#P(a:#[#Box(select(b.x))],b:#[#Box(a.x+(#u32)0)]);'
            '^answer:#u32={p=#P(#Box((#u8)1),#Box((#u8)2));p.a.x};',{'answer':'1'})

    def test_recursive_and_early_record_results(self):
        self.check(PAIR+'&~walk(n:#u8)->#P{n==0?#P(#Box((#u8)255),#Box((#u16)2)):walk(n-1)}'
            '^answer:#u16=walk(3).a.x+1;',{'answer':'256'})
        self.check(PAIR+'&~make(n:#b)->#P{n?{->#P(#Box((#u8)255),#Box((#u16)2));};#P(#Box((#u8)254),#Box((#u16)2))}'
            '^answer:#u16=make(1).a.x+1;',{'answer':'256'})

    def test_optional_rollback_and_independent_roots(self):
        self.check(PAIR+'^first:#u16={r:#u16=7;#{p=#P(#Box((#u8)255),#Box((#u16)2));r=p.a.x+1;missing();};r};'
            '^second:#u16={p=#P(#Box((#u8)255),#Box((#u16)2));p.a.x+1};',{'first':'7','second':'256'})
        self.check('^first:#u16={r:#u16=7;#{::part;r=a.x+1;missing();};r};'
            '^second:#u16={::part;a.x+1};',{'first':'7','second':'256'},files={'part.vx':GLOBALS})

    def test_structural_choices_and_guarded_rejection(self):
        self.check(CHOICE+'^answer:#u16=?{n=?(#u8,[0,1]);?(n==0);p=#P(#Box((#u8)255),#Box(n));p.a.x+1};',{'answer':'256'},finite=True)
        self.check(CHOICE+'^answer:#u16=?{n=?(#u8,[0,1]);p=#P(#Box((#u8)255),#Box(n));p.a.x+1};',error='ambiguous',finite=True)
        self.check(CHOICE+'^answer:#u16=?{n=?(#u8,[0,1]);p=#P(#Box((#u8)1),#Box(n));(#u16)(p.a.x+1)};',{'answer':'2'},finite=True)
        self.check(PAIR+'^answer:#u8=?{n=?(#b,[0,1]);n?{p=#P(#Box(missing()),#Box((#u16)1));};7};',{'answer':'7'},finite=True)
        self.check(BOX+'#Other(x);#P(a:#[copy(b)],b:#[copy(a)]);&copy(n){n}'
            '^answer:#u8=?{n=?(#b,[0,1]);n?{p=#P(#Box((#u8)1),#Other((#u16)1));};7};',{'answer':'7'},finite=True)

    def test_exhaustion_and_missing_values_do_not_become_solutions(self):
        self.check(PAIR+'^answer:#u16={p=#P(#Box((#u8)255),#Box((#u16)2));p.a.x+1};',error='budget',flags=('--max-steps','100'))
        self.check('^answer:#u16={r:#u16=7;#{::part;r=a.x+1;};r};',error='budget',files={'part.vx':GLOBALS},flags=('--max-steps','100'))
        self.check(CHOICE+'^answer:#u16=?{n=?(#u8,[0,1]);p=#P(#Box((#u8)255),#Box(n));p.a.x+1};',error='budget',finite=True,flags=('--max-decisions','1'))
        self.check(BOX+'a:#[#Box(b.x+1)]=b;b:#[#Box(a.x+1)]=a;^answer:#u16=a.x;',error='unresolved dependency cycle')

    def test_host_materialized_and_recursive_interfaces(self):
        with tempfile.TemporaryDirectory(prefix='vexel-record-equations-host-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            run([EXE,source,'--emit-msx',project]);library=root/'test.so'
            run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(project/'generated'),'-I'+str(project/'runtime'),
                *sorted((project/'generated').glob('*.c')),project/'runtime/vx_runtime.c','-o',library])
            dll=ctypes.CDLL(str(library))
            for name in ('next','captured','recursive'):
                fn=getattr(dll,'vx_'+name);fn.argtypes=[ctypes.c_uint8];fn.restype=ctypes.c_uint16
            self.assertEqual([dll.vx_next(n) for n in (0,1,254,255)],[1,2,255,256])
            self.assertEqual(dll.vx_captured(2),6)
            self.assertEqual([dll.vx_recursive(n) for n in (0,1,4)],[256]*3)

    def test_z80_recursion_and_captured_invariant_trap(self):
        with tempfile.TemporaryDirectory(prefix='vexel-record-equations-z80-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            run([EXE,source,'--emit-msx',project])
            main='#include "vexel.h"\nvolatile uint32_t vx_last_result;\nvoid main(void) VX_ABI VX_RESIDENT {vx_last_result=%s;__asm halt __endasm;}\n'
            (project/'user/main.c').write_text(main%'vx_next(255)+vx_captured(2)+vx_recursive(4)')
            run(['make','-C',project,'-j2','simulate'],env=project_environment(),timeout=180)
            self.assertEqual(json.loads((project/'build/execution.json').read_text())['observation']['last_result'],518)
            (project/'user/main.c').write_text(main%'vx_captured(255)')
            result=subprocess.run(['make','-C',str(project),'-j2','simulate'],env=project_environment(),capture_output=True,text=True,timeout=180)
            self.assertNotEqual(result.returncode,0);self.assertIn('runtime trapped with reason 9',result.stdout+result.stderr)
            self.assertFalse((project/'build/execution.json').exists())


if __name__=='__main__':unittest.main(verbosity=2)
