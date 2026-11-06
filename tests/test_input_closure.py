from pathlib import Path
import json
import os
import subprocess
import tempfile
import time
import unittest
import sys

EXE=Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()

class InputTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='vexel-input-closure-')
        self.root=Path(self.temp.name);(self.root/'build').mkdir()
        (self.root/'Makefile').write_text('all: build/result.json\n'
            'build/result.json: main.vx\n'
            f'\t{EXE} $< --json --depfile build/result.d --target $@ > $@\n'
            '-include build/result.d\n')

    def tearDown(self):self.temp.cleanup()

    def write(self,name,text):
        path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True)
        time.sleep(.01)
        if isinstance(text,bytes):path.write_bytes(text)
        else:path.write_text(text)
        return path

    def make(self,*args):
        env=os.environ.copy()
        for name in ('MAKEFLAGS','MFLAGS','MAKEOVERRIDES','MAKELEVEL','BUILD'):env.pop(name,None)
        return subprocess.run(['make',*args],cwd=self.root,env=env,capture_output=True,text=True,timeout=30)

    def value(self,expected):
        result=self.make();self.assertEqual(result.returncode,0,result.stderr)
        data=json.loads((self.root/'build/result.json').read_text())
        self.assertEqual(data['roots'][0]['value'],expected)
        self.assertEqual(self.make('-q').returncode,0,'unchanged build must stay fresh')
        return data

    def test_optional_module_creation_and_deletion(self):
        self.write('main.vx','^answer:#u8={r:#u8=7;#{::optional;r=chosen;};r};')
        self.value('7')
        module=self.write('optional.vx','chosen=9;')
        self.assertEqual(self.make('-q').returncode,1)
        self.value('9');module.unlink()
        self.assertEqual(self.make('-q').returncode,1)
        self.value('7')

    def test_project_module_overrides_relative_fallback(self):
        self.write('main.vx','::sub::entry;^answer:#u8=chosen;')
        self.write('sub/entry.vx','::part;');self.write('sub/part.vx','chosen=2;')
        self.value('2');preferred=self.write('part.vx','chosen=3;');self.value('3')
        self.write('sub/part.vx','chosen=4;')
        self.assertEqual(self.make('-q').returncode,0)
        preferred.unlink();self.value('4')

    def test_new_directory_can_supply_bundled_override(self):
        self.write('main.vx','::std::bits;^answer:#u32=f32_as_u32((#f32)1);')
        self.value('1065353216')
        self.write('std/bits.vx','&f32_as_u32(value:#f32)->#u32{7}')
        self.value('7');(self.root/'std/bits.vx').unlink();self.value('1065353216')

    def test_rejected_optional_keeps_observed_resource(self):
        self.write('main.vx','^answer:#u8={r:#u8=7;#{(|::asset.bin| == 0)?missing():{r=9;};};r};')
        resource=self.write('asset.bin',b'')
        data=self.value('7');self.assertIn(str(resource),data['dependencies'])
        self.write('asset.bin',b'x');self.value('9')

    def test_rejected_optional_keeps_absent_directory(self):
        self.write('main.vx','^answer:#u8={r:#u8=7;#{(|::assets| == 0)?missing():{r=9;};};r};')
        self.value('7');self.write('assets/item.bin',b'x');self.value('9')

    def test_missing_nested_import_retracks_nearest_parent(self):
        self.write('main.vx','^answer:#u8={r:#u8=7;#{::optional::part;r=chosen;};r};')
        self.value('7');(self.root/'optional').mkdir();self.value('7')
        self.write('optional/part.vx','chosen=9;');self.value('9')

    def test_rejected_structural_candidate_keeps_resource(self):
        self.write('main.vx','^answer:#u8=?{n=?(#u8,[0,1]);x:#u8[n+1];'
            '(n!=0)?{?(|::asset.bin| == 0);(#u8)9}:(#u8)7};')
        resource=self.write('asset.bin',b'x')
        data=self.value('7');self.assertIn(str(resource),data['dependencies'])
        self.write('asset.bin',b'');result=self.make()
        self.assertNotEqual(result.returncode,0);self.assertIn('ambiguous',result.stderr)

if __name__=='__main__':unittest.main(verbosity=2)
