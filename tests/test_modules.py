#!/usr/bin/env python3
"""Module discovery and lexical bindings must be independent of demand order."""
import ctypes
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()

class ModuleTests(unittest.TestCase):
    def check(self, source, modules, expected=None, error=None, flags=()):
        with tempfile.TemporaryDirectory(prefix='vexel-modules-') as directory:
            root=Path(directory)
            for name,content in {'main.vx':source,**modules}.items():
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(content)
            for schedule in ('fifo','lifo','random'):
                result=subprocess.run([str(BINARY),str(root/'main.vx'),'--json','--schedule',schedule,*flags],capture_output=True,text=True,timeout=30)
                if error:
                    self.assertEqual(result.returncode,1,result.stderr);self.assertIn(error,result.stderr)
                else:
                    self.assertEqual(result.returncode,0,result.stderr)
                    self.assertEqual({r['name']:r['value'] for r in json.loads(result.stdout)['roots']},expected)

    def test_forward_scoped_imports_and_constants(self):
        self.check('^answer:#u8={x=twice(later);::values;::values;x};',
                   {'values.vx':'later:#u8=first+1; &twice(n:#u8)->#u8{n*2} first:#u8=20;'}, {'answer':'42'})
        self.check('^answer:#u8={::a;get()};',{'a.vx':'::b; &get()->#u8{value}', 'b.vx':'value:#u8=42;::a;'}, {'answer':'42'})

    def test_scoped_alias_groups_are_closed_before_resolution(self):
        self.check('^answer:#u16={::part;a+1};',{'part.vx':'a:#[b]=(#u8)255;b:#[a]=(#u16)2;'}, {'answer':'256'})
        self.check('::first;::second;^answer:#u16=a+1;',{'first.vx':'a:#[b]=(#u8)255;', 'second.vx':'b:#[a]=(#u16)2;'}, {'answer':'256'})
        self.check('::second;::first;^answer:#u16=a+1;',{'first.vx':'a:#[b]=(#u8)255;', 'second.vx':'b:#[a]=(#u16)2;'}, {'answer':'256'})

    def test_scopes_are_isolated_and_types_are_forward_visible(self):
        self.check('^a:#u8={p=#P(7);::part;p.x}; ^b:#u8={::part;get()};',
                   {'part.vx':'#P(x:#u8); value:#u8=9;&get()->#u8{value}'}, {'a':'7','b':'9'})
        self.check('^a:#u8={::part;value};^b:#u8=value;',{'part.vx':'value:#u8=1;'},error='undefined identifier')
        self.check('^a:#u8={::part;0};',{'part.vx':'value:#u8=missing;'}, {'a':'0'})

    def test_nested_imports_and_local_overloads(self):
        self.check('&f(x:#u8)->#u8{x+1} ^answer:#u8={::part;f((#u8)1)+f((#u16)2)};',
                   {'part.vx':'&f(x:#u16)->#u8{(#u8)x+10}'}, {'answer':'14'})
        self.check('^answer:#u8={::part;f(2)};',
                   {'part.vx':'&f(x:#u8)->#u8{::other;x+value}', 'other.vx':'value:#u8=40;'}, {'answer':'42'})

    def test_no_shadowing_or_conflicting_definitions(self):
        modules={'part.vx':'value:#u8=1;'}
        for source in ('value:#u8=2;^a:#u8={::part;value};','^a:#u8={value:#u8=2;::part;value};','^a:#u8={::part;value=3;value};'):
            self.check(source,modules,error='symbol' if source.startswith('value') else 'local' if 'value:#u8=2' in source else 'cannot assign')
        self.check('::a;::b;^a:#u8=0;',{'a.vx':'#P(x:#u8);','b.vx':'#P(x:#u8);'},error='duplicate record type')

    def test_optional_import_absence_and_diagnostics(self):
        self.check('^answer:#u8={x:#u8=1;#{::missing;x=9;};x};',{}, {'answer':'1'})
        self.check('^answer:#u8={x:#u8=1;#{::part;x=value;};x};',{'part.vx':'value:#u8=42;'}, {'answer':'42'})
        self.check('^answer:#u8={#{::part;};1};',{'part.vx':'&broken( {}'},error='delimiter')
        self.check('^answer:#u8={#{x=::missing.bin;};1};',{},error='cannot read file')
        self.check('^answer:#u8={#{x:#u0=0;};1};',{},error='unsupported type or integer width')

    def test_process_import_is_lexical_and_lazy(self):
        self.check('^a:#u8={::"printf abc" -> text;|text|};',{}, {'a':'3'},flags=('--allow-processes',))
        self.check('^a:#u8={::"exit 1" -> text;1};',{}, {'a':'1'})

    def test_external_functions_resolve_scoped_record_types(self):
        self.check('&^read()->#u8{::part;get().x}',{'part.vx':'#R(x:#u8);&!get()->#R;'}, {'read':'runtime'})
        self.check('&^read()->#u8{::part;get(#R(1))}',{'part.vx':'#R(x:#u8);&!get(r:#R)->#u8;'}, {'read':'runtime'})
        self.check('^answer:#u8={::part;x:#[get().x]=7;x};',{'part.vx':'#R(x:#u8);&!get()->#R;'}, {'answer':'7'})

    def test_bundled_bits_and_whole_module_overrides(self):
        self.check('::std::bits;^a:#u32=f32_as_u32((#f32)1.5);^b:#u64=f64_as_u64(u64_as_f64(0x7ff123456789abcd));',{},
                   {'a':str(0x3fc00000),'b':str(0x7ff123456789abcd)})
        self.check('::std::bits;^a:#u32=f32_as_u32(1.5);',{'std/bits.vx':'&f32_as_u32(value:#f64)->#u32{42}'},{'a':'42'})
        self.check('::std::bits;^a:#u32=u32_as_f32(0);',{'std/bits.vx':'&f32_as_u32(value:#f64)->#u32{42}'},error='undefined function')
        self.check('::nested::part;^a:#u32=f32_as_u32((#f32)1.5);',
                   {'nested/part.vx':'::std::bits;', 'nested/std/bits.vx':'&f32_as_u32(value:#f64)->#u32{42}'},{'a':str(0x3fc00000)})

    def test_external_interfaces_belong_to_each_invocation(self):
        prefix='&bridge(w:#u8){#P(data:#u8[w]);&!take(p:#P)->#u8;take(#P(0..w))}'
        entries=['&^one()->#u8{bridge(1)}','&^two()->#u8{bridge(2)}']
        with tempfile.TemporaryDirectory(prefix='vexel-external-specialization-') as directory:
            root=Path(directory);source=root/'main.vx';project=root/'project'
            for definitions in (entries,entries[::-1]):
                source.write_text(prefix+''.join(definitions))
                for schedule in ('fifo','lifo','random'):
                    result=subprocess.run([str(BINARY),str(source),'--schedule',schedule,'--emit-msx',str(project)],capture_output=True,text=True,timeout=30)
                    self.assertEqual(result.returncode,1,result.stderr)
                    self.assertIn('inconsistent external C signature',result.stderr)
                    self.assertFalse(project.exists())

    def test_imported_functions_project_to_c(self):
        with tempfile.TemporaryDirectory(prefix='vexel-module-c-') as directory:
            root=Path(directory)
            (root/'main.vx').write_text('&^answer(x:#u8)->#u8{::part;f(x)}')
            (root/'part.vx').write_text('offset:#u8=40;&~f(x:#u8)->#u8{x+offset}')
            project=root/'project'
            result=subprocess.run([str(BINARY),str(root/'main.vx'),'--emit-msx',str(project)],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run(['cc','-shared','-fPIC','-std=c99','-I'+str(project/'runtime'),'-I'+str(project/'generated'),*[str(p) for p in (project/'generated').glob('*.c')],str(project/'runtime/vx_runtime.c'),'-o',str(root/'module.so')],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stderr)
            fn=ctypes.CDLL(str(root/'module.so')).vx_answer;fn.argtypes=[ctypes.c_uint8];fn.restype=ctypes.c_uint8
            self.assertEqual(fn(2),42)

if __name__=='__main__':unittest.main(verbosity=2)
