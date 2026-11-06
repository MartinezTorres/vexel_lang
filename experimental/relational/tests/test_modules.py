#!/usr/bin/env python3
"""Scoped module closure, activation-independent instances and visible overloads."""
import ctypes
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support

class ModuleTests(unittest.TestCase):
    def check(self,source,modules,expected=None,status='resolved',error=None,query='value:a'):
        with tempfile.TemporaryDirectory(prefix='vexel-relational-modules-') as tmp:
            root=Path(tmp)
            for name,contents in {'source.vx':source,**modules}.items():
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(contents)
            for schedule,branch in support.ORDERS:
                with self.subTest(source=source,schedule=schedule,branch=branch):
                    r=support.run([support.EXE,root/'source.vx','--query',query,'--schedule',schedule,'--branch-order',branch])
                    if error:
                        self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn(error,r.stderr);continue
                    self.assertIn(r.returncode,(0,1),r.stderr);report=json.loads(r.stdout)
                    self.assertEqual(report['status'],status,r.stdout+r.stderr)
                    if expected is not None:self.assertEqual(report['queries'][0]['answer'],expected)

    def test_forward_declarations_cycles_and_type_relationships(self):
        self.check('a={x=twice(later);::part;::part;x};',
                   {'part.vx':'later:#u8=first+1;&twice(n:#u8){n*2}first:#u8=20;'},'42')
        self.check('a={::first;get()};',{'first.vx':'::second;&get(){value}', 'second.vx':'value:#u8=42;::first;'},'42')
        self.check('a={::part;left+1};',{'part.vx':'left:#[right]=(#u8)255;right:#[left]=(#u16)2;'},'256')
        self.check('a={p=#P(7);::part;p.x};',{'part.vx':'#P(x:#u8);value:#u8=9;'},'7')
        self.check('a={::part;0};',{'part.vx':'unused:#u8=missing;'},'0')
        self.check('a={::missing;0};',{},error='cannot resolve import')

    def test_visible_overloads_nested_imports_and_bundled_bits(self):
        self.check('&f(x:#u8)->#u8{x+1}a={::part;f((#u8)1)+f((#u16)2)};',
                   {'part.vx':'&f(x:#u16)->#u8{(#u8)x+10}'},'14')
        self.check('a={::part;f(2)};',{'part.vx':'&f(x:#u8){::other;x+value}', 'other.vx':'value:#u8=40;'},'42')
        self.check('a={::std::bits;f32_as_u32((#f32)1.5)};',{},str(0x3fc00000))
        self.check('a={::std::bits;f32_as_u32(1.5)};',{'std/bits.vx':'&f32_as_u32(value:#f64)->#u32{42}'},'42')
        self.check('a={::std::bits;u32_as_f32(0)};',{'std/bits.vx':'&f32_as_u32(value:#f64)->#u32{42}'},status='unresolved')

    def test_module_isolation_and_automatic_capture_exclusion(self):
        self.check('a={::part;value};b=value;',{'part.vx':'value:#u8=1;'},status='unresolved',query='value:b')
        self.check('&f(n:#u8){::part;value}a=f(3);',{'part.vx':'value:#u8=n;'},status='unresolved')
        self.check('&f(n:#u8){::part;&local(){n}read()}a=f(3);',{'part.vx':'&read(){local()}'},status='unresolved')
        self.check('&f(n:#u8){::part;&local(){n}local()+value}a=f(3);',{'part.vx':'value:#u8=1;'},'4')
        self.check('&f(n:#u8){::part;&local(){42}read()}a=f(3);',{'part.vx':'&read(){local()}'},'42')
        self.check('value:#u8=2;a={::part;value};',{'part.vx':'value:#u8=1;'},error='duplicate symbol')
        self.check('a={value:#u8=2;::part;value};',{'part.vx':'value:#u8=1;'},error='duplicate local')
        self.check('a={::part;value=3;value};',{'part.vx':'value:#u8=1;'},status='contradictory')
        self.check('&a(){::"exit 7" -> unused;}',{},'NoValue')

    def test_persistent_instances_across_specializations_and_activations(self):
        source='&!initial()->#u8;&~step(k){::state;bump((#u8)k)}&^entry(n:#u8){first=step(n);second=step((#u16)n);third={::state;bump(1)};(first,second,third)}'
        module='counter:#u8=initial();&bump(n:#u8){counter+=n;counter}'
        for schedule,branch in support.ORDERS:
            with self.subTest(schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-module-state-') as tmp:
                root=Path(tmp);source_path=root/'source.vx';source_path.write_text(source);(root/'state.vx').write_text(module)
                generated=root/'generated';r=support.run([support.EXE,source_path,'--source','entry','--budget','12','--emit-c',generated,'--schedule',schedule,'--branch-order',branch])
                self.assertEqual(r.returncode,0,r.stdout+r.stderr);report=json.loads(r.stdout)
                self.assertEqual(len(report['boundary_storage']),2,report)
                bindings=root/'bindings.c';bindings.write_text('#include "vexel.h"\nunsigned initial_calls;void vx_external_initial(uint32_t *out){++initial_calls;out[0]=0;}')
                runtime=support.ROOT/'backends/msx-neo8/runtime';library=root/'host.so'
                r=support.run(['cc','-O2','-std=c11','-shared','-fPIC','-I'+str(generated),'-I'+str(runtime),generated/'vx_bridges.c',runtime/'vx_runtime.c',bindings,'-o',library])
                self.assertEqual(r.returncode,0,r.stderr);dll=ctypes.CDLL(str(library));dll.vx_entry.argtypes=[ctypes.c_uint8]
                dll.vx_entry_read.argtypes=[ctypes.c_uint16];dll.vx_entry_read.restype=ctypes.c_uint32;total=0
                for n in range(256):
                    first=(total+n)%256;total=(total+2*n)%256;dll.vx_entry(n)
                    self.assertEqual([dll.vx_entry_read(i) for i in range(3)],[first,total,(n+1)%256],n)
                self.assertEqual(ctypes.c_uint.in_dll(dll,'initial_calls').value,2)

    def test_capabilities_use_closed_lexical_membership(self):
        for body in ('a={x=?f((#u8)1);::part;x};',
                     'a={::part;x=?f((#u8)1);x};'):
            self.check(body,{'part.vx':'&f(x:#u8){missing()}'},'1')
        # Checking a callee body must never add its import to the caller.
        for body in ('a={before=?hidden();f();after=?hidden();before||after};',
                     'a={f();?hidden()};'):
            self.check('&f(){::part;hidden()}'+body,{'part.vx':'&hidden(){7}'},'0')
        # A local overload participates for the whole scope, even before its
        # textual declaration; it remains invisible outside that scope.
        self.check('&f(x:#s){0}a={present=?f((#u8)1);&f(x:#u8){x}present};',{},'1')
        self.check('&f(x:#s){0}&g(){&f(x:#u8){x}f((#u8)1)}a={g();?f((#u8)1)};',{},'0')
        for module in ('&f(x:#u8){1}&f(x:#u8){2}', '&f(x:#u8){2}&f(x:#u8){1}'):
            self.check('a={::part;?f((#u8)1)};',{'part.vx':module},status='ambiguous')

    def test_edit_starts_a_new_declaration_revision(self):
        with tempfile.TemporaryDirectory(prefix='vexel-declaration-revision-') as temp:
            root=Path(temp);source=root/'source.vx';module=root/'part.vx'
            source.write_text('::part;a=?f((#u8)1);')
            for contents,answer in (('&f(x:#s){1}','0'),('&f(x:#u8){2}','1'),('&other(){3}','0')):
                module.write_text(contents)
                result=support.run([support.EXE,source,'--query','value:a'])
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertEqual(json.loads(result.stdout)['queries'][0]['answer'],answer)

    def test_value_function_collisions_do_not_depend_on_statement_order(self):
        for body in ('f:#u8=1;&f(){2}f', '&f(){2}f:#u8=1;f',
                     'f=1;&f(){2}f', '&f(){2}f=1;f',
                     'f,x=(1,2);&f(){2}x', '&f(){2}f,x=(1,2);x'):
            self.check('a={'+body+'};',{},error='declaration')
        self.check('x:#u8=1;a={x:#u8=2;x};',{},error='shadowing')

if __name__=='__main__':unittest.main(verbosity=2)
