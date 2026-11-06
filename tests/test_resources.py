#!/usr/bin/env python3
"""File/directory snapshots and explicitly enabled build-process inputs."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()
class ResourceTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='vexel-resources-');self.base=Path(self.tmp.name)
    def tearDown(self):self.tmp.cleanup()
    def run_source(self,source,*args):
        path=self.base/'main.vx';path.write_text(source)
        return subprocess.run([str(BINARY),str(path),'--json',*args],capture_output=True,text=True,timeout=15)
    def value(self,run):
        self.assertEqual(run.returncode,0,run.stderr);return json.loads(run.stdout)['roots'][0]['value']

    def test_directory_order_contents_and_dependencies(self):
        assets=self.base/'assets';assets.mkdir()
        (assets/'z.bin').write_bytes(b'ZZ');(assets/'a.bin').write_bytes(b'A\xff')
        (assets/'nested').mkdir();(assets/'nested/ignored').write_text('ignored')
        source='^answer:#u16={a=::assets;|a|+(#u16)a[0].__1[1]+|a[1].__1|+(#u16)(a[0].__0=="a.bin")};'
        for schedule in ('fifo','lifo','random'):
            result=self.run_source(source,'--schedule',schedule)
            self.assertEqual(self.value(result),'260')
            dependencies=json.loads(result.stdout)['dependencies']
            for path in (assets,assets/'a.bin',assets/'z.bin'):self.assertIn(str(path),dependencies)
            self.assertNotIn(str(assets/'nested/ignored'),dependencies)
        result=self.run_source(source,'--max-elements','1')
        self.assertEqual(result.returncode,1);self.assertIn('directory resource exceeds',result.stderr)

    def test_missing_directory_and_file_distinction(self):
        self.assertEqual(self.value(self.run_source('^answer:#u8=|::absent|;')),'0')
        result=self.run_source('^answer:#s=::missing.bin;')
        self.assertEqual(result.returncode,1);self.assertIn('cannot read file',result.stderr)

    def test_process_opt_in_and_type_only_demand(self):
        (self.base/'input.bin').write_bytes(b'A\0\xff')
        source='::"cat input.bin" -> generated; ^answer:#u16=|generated|+(#u16)generated[2];'
        result=self.run_source(source)
        self.assertEqual(result.returncode,1);self.assertIn('--allow-processes',result.stderr)
        self.assertEqual(self.value(self.run_source(source,'--allow-processes')),'258')
        command=json.dumps('printf x > touched; printf bad')
        result=self.run_source('::'+command+' -> generated; ^answer:#u8={x:#[generated]="safe";|x|};')
        self.assertEqual(self.value(result),'4');self.assertFalse((self.base/'touched').exists())

    def test_process_snapshot_survives_rejection_and_bounds(self):
        command=json.dumps('printf x >> calls; printf token')
        result=self.run_source('::'+command+' -> generated; ^answer:#u8={#{data=generated;missing();};|generated|};','--allow-processes')
        self.assertEqual(self.value(result),'5');self.assertEqual((self.base/'calls').read_text(),'x')
        result=self.run_source('::"exit 7" -> generated; ^answer:#s=generated;','--allow-processes')
        self.assertEqual(result.returncode,1);self.assertIn('process import failed',result.stderr)
        result=self.run_source('::"sleep 2" -> generated; ^answer:#s=generated;','--allow-processes','--process-timeout','30')
        self.assertEqual(result.returncode,1);self.assertIn('timeout',result.stderr)

if __name__=='__main__':unittest.main(verbosity=2)
