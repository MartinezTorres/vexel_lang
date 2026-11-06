#!/usr/bin/env python3
"""Type equations retain their constructor or lexical module context."""
import ctypes
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_msx import EXE, project_environment, run

PAIR='#Pair(a:#[b+1],b:#[a+1]);'
CHOICE='&choose(n){(n!=0)?(#u8)0:(#u16)0}#Pair(a:#[choose(b)],b:#[a+1]);'
MODULE='a:#[b+1]=(#u8)255;b:#[a+1]=(#u16)2;'
RUNTIME=PAIR+'''
#Packet(a:#[b+1],b:#[a+1],data:#u8[a+1]);
&~make(n:#u8)->#Pair{#Pair(n,(#u16)2)}
&^next(n:#u8)->#u16{make(n).a+1}
&^packet(count:#u8)->#u16{p=#Packet(count,(#u16)2,[4,5,6]);p.a+p.data[0]}
&~local(n:#u8)->#u16{::part;a+n+1}
&^module(n:#u8)->#u16{local(n)}
'''

class ContextEquationTests(unittest.TestCase):
    def check(self,source,values=None,error=None,files=None,flags=(),finite=False):
        reports=[]
        with tempfile.TemporaryDirectory(prefix='vexel-context-equations-') as directory:
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

    def test_field_cycles_and_separate_instances(self):
        self.check(PAIR+'^answer:#u16={p=#Pair((#u8)255,(#u16)2);p.a+1};',{'answer':'256'})
        self.check('#Pair(b:#[a+1],a:#[b+1]);^answer:#u16={p=#Pair((#u16)2,(#u8)255);p.a+1};',{'answer':'256'})
        reports=self.check(PAIR+'^one:#u16={p=#Pair((#u8)255,(#u16)2);p.a+1};'
            '^two:#u32={p=#Pair((#u16)65535,(#u32)2);p.a+1};',{'one':'256','two':'65536'})
        for report in reports:
            self.assertEqual({e['candidate'] for e in report['type_equations']},{'#u16','#u32'})

    def test_array_fields_helpers_and_temporary_errors(self):
        self.check('#P(a:#[b[0]],b:#[[a]]);^answer:#u16={p=#P((#u8)255,[(#u16)2]);p.a+1};',{'answer':'256'})
        self.check('&copy(n){n}#P(a:#[copy(b)+1],b:#[copy(a)+1]);'
            '^answer:#u16={p=#P((#u8)255,(#u16)2);p.a+1};',{'answer':'256'})
        self.check('&select(n:#u16){n}&select(n:#u32){n}#P(a:#[select(b)],b:#[a+(#u32)0]);'
            '^answer:#u32={p=#P((#u8)1,(#u8)2);p.a};',{'answer':'1'})
        self.check('#P(a:#[{x:#u8[1/(b+1)];x}],b:#[a[0]+(#u16)0]);'
            '^answer:#u8={p=#P((#u8[0])[],(#u8)255);|p.a|};',{'answer':'0'})

    def test_only_needed_field_properties_are_demanded(self):
        self.check(PAIR+'^answer:#u16={v:#[#Pair((#u8)(1/0),(#u16)2).a]=300;v};',{'answer':'300'})
        self.check('#P(a:#[b+1],b:#[a+1],c:#u8);'
            '^answer:#u8={v:#[#P(missing(),missing(),1).c]=7;v};',{'answer':'7'})
        self.check(PAIR+'^answer:#u16={p=#Pair((#u8)(1/0),(#u16)2);p.a};',error='division by zero')

    def test_captures_dispatch_and_invariants_use_final_field_types(self):
        self.check('^answer:#u16={offset:#u8=1;&copy(n){offset+n}'
            '#P(a:#[copy(b)],b:#[copy(a)]);p=#P((#u8)255,(#u16)2);p.a+1};',{'answer':'256'})
        self.check('^answer:#u16={unused:#u8;&size(n:#u8){n+unused}&size(n:#u16){n+1}'
            '#P(a:#[b+1],b:#[a+1],data:#u8[size(a)]);p=#P((#u8)2,(#u16)3,[1,2,3]);p.a+p.data[2]};',{'answer':'5'})
        self.check('#P(a:#[b+1],b:#[a+1],data:#u8[a+1]);'
            '^answer:#u16={p=#P((#u8)255,(#u16)2,[]);p.a};',error='dimension does not match')

    def test_recursive_and_early_result_interfaces(self):
        self.check(PAIR+'&~walk(n:#u8)->#Pair{n==0?#Pair((#u8)255,(#u16)2):walk(n-1)}'
            '^answer:#u16=walk(3).a+1;',{'answer':'256'})
        self.check(PAIR+'&~make(n:#b)->#Pair{n?{->#Pair((#u8)255,(#u16)2);};#Pair((#u8)254,(#u16)2)}'
            '^answer:#u16=make(1).a+1;',{'answer':'256'})
        self.check('a:#[#Pair(b,(#u16)2).a]=(#u8)255;b:#[a+1]=(#u8)2;'+PAIR+
            '^answer:#u16=a+1;',{'answer':'256'})

    def test_scoped_globals_and_distinct_module_instances(self):
        self.check('^answer:#u16={::part;a+1};',{'answer':'256'},files={'part.vx':MODULE})
        reports=self.check('^one:#u16={&w(n){(#u16)n}::part;a+1};'
            '^two:#u32={&w(n){(#u32)n}::part;a+1};',{'one':'256','two':'256'},
            files={'part.vx':'a:#[w(b)]=(#u8)255;b:#[a+1]=(#u8)2;'})
        for report in reports:
            self.assertEqual(sorted(e['candidate'] for e in report['type_equations']),['#u16','#u16','#u32','#u32'])
        self.check('^answer:#u16={::first;::second;a+1};',{'answer':'256'},
            files={'first.vx':'a:#[b+1]=(#u8)255;','second.vx':'b:#[a+1]=(#u16)2;'})

    def test_optional_rollback_retains_other_instances(self):
        self.check(PAIR+'^first:#u16={r:#u16=7;#{p=#Pair((#u8)255,(#u16)2);r=p.a+1;missing();};r};'
            '^second:#u16={p=#Pair((#u8)255,(#u16)2);p.a+1};',{'first':'7','second':'256'})
        self.check('^first:#u16={r:#u16=7;#{::part;r=a+1;missing();};r};'
            '^second:#u16={::part;a+1};',{'first':'7','second':'256'},files={'part.vx':MODULE})

    def test_late_structural_choices_distinguish_unique_and_ambiguous(self):
        self.check(CHOICE+'^answer:#u16=?{n=?(#u8,[0,1]);?(n==0);p=#Pair((#u8)255,n);p.a+1};',{'answer':'256'},finite=True)
        self.check(CHOICE+'^answer:#u16=?{n=?(#u8,[0,1]);p=#Pair((#u8)255,n);p.a+1};',error='ambiguous',finite=True)
        # The puzzle's own observable interface is fixed; only its internal
        # field types differ. Both completed revisions now return #u16(2).
        self.check(CHOICE+'^answer:#u16=?{n=?(#u8,[0,1]);p=#Pair((#u8)1,n);(#u16)(p.a+1)};',{'answer':'2'},finite=True)

    def test_guarded_inconsistent_equations_reject_only_their_branch(self):
        prefix='#P(a:#[b==1],b:#[a]);'
        self.check(prefix+'^answer:#u8=?{n=?(#b,[0,1]);n?{p=#P((#u8)1,(#u8)2);};7};',{'answer':'7'},finite=True)
        self.check(prefix+'^answer:#u8=?{n=?(#b,[0,1]);?(n);n?{p=#P((#u8)1,(#u8)2);};7};',error='no solution',finite=True)
        self.check(prefix+'^answer:#u8={p=#P((#u8)1,(#u8)2);p.a};',error='incompatible types')

    def test_exhaustion_is_not_a_rejected_candidate(self):
        self.check(PAIR+'^answer:#u16={p=#Pair((#u8)255,(#u16)2);p.a+1};',error='budget',flags=('--max-steps','100'))
        self.check('^answer:#u16={r:#u16=7;#{::part;r=a+1;};r};',error='budget',files={'part.vx':MODULE},flags=('--max-steps','100'))
        self.check(CHOICE+'^answer:#u16=?{n=?(#u8,[0,1]);p=#Pair((#u8)255,n);p.a+1};',error='budget',finite=True,flags=('--max-decisions','1'))

    def test_host_materialized_records_and_module_bindings(self):
        with tempfile.TemporaryDirectory(prefix='vexel-context-equations-host-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            (root/'part.vx').write_text(MODULE);run([EXE,source,'--emit-msx',project])
            library=root/'test.so'
            run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(project/'generated'),'-I'+str(project/'runtime'),
                *sorted((project/'generated').glob('*.c')),project/'runtime/vx_runtime.c','-o',library])
            dll=ctypes.CDLL(str(library))
            for name in ('next','packet','module'):
                fn=getattr(dll,'vx_'+name);fn.argtypes=[ctypes.c_uint8];fn.restype=ctypes.c_uint16
            self.assertEqual([dll.vx_next(n) for n in (0,1,254,255)],[1,2,255,256])
            self.assertEqual(dll.vx_packet(2),6);self.assertEqual(dll.vx_module(1),257)

    def test_z80_materialization_and_invariant_trap(self):
        with tempfile.TemporaryDirectory(prefix='vexel-context-equations-z80-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            (root/'part.vx').write_text(MODULE);run([EXE,source,'--emit-msx',project])
            main='#include "vexel.h"\nvolatile uint32_t vx_last_result;\nvoid main(void) VX_ABI VX_RESIDENT {vx_last_result=%s;__asm halt __endasm;}\n'
            (project/'user/main.c').write_text(main%'vx_next(255)+vx_packet(2)+vx_module(1)')
            run(['make','-C',project,'-j2','simulate'],env=project_environment(),timeout=180)
            self.assertEqual(json.loads((project/'build/execution.json').read_text())['observation']['last_result'],519)
            (project/'user/main.c').write_text(main%'vx_packet(255)')
            result=subprocess.run(['make','-C',str(project),'-j2','simulate'],env=project_environment(),capture_output=True,text=True,timeout=180)
            self.assertNotEqual(result.returncode,0);self.assertIn('runtime trapped with reason 9',result.stdout+result.stderr)
            self.assertFalse((project/'build/execution.json').exists())

if __name__=='__main__':unittest.main(verbosity=2)
