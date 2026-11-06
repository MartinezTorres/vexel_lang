#!/usr/bin/env python3
"""Opaque metadata, qualified spelling and optional literal diagnostics."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()
class SurfaceTests(unittest.TestCase):
    def run_source(self,source,*flags):
        with tempfile.TemporaryDirectory(prefix='vexel-surface-') as scratch:
            path=Path(scratch)/'main.vx';path.write_text(source)
            return subprocess.run([str(BINARY),str(path),'--json',*flags],capture_output=True,text=True,timeout=30)

    def test_metadata_is_preserved_without_evaluation(self):
        source='''[[record(tag)]] #R([[field]] x:#u8);
        [[function]] [[opaque(unbound,"a\\n",-7)]] &f([[parameter]] x:#u8)->#u8 {
            [[statement]] y:#u8=x+1;y
        }
        [[global]] ^answer:#u8=[[expression]] (f(2)+1);
        '''
        for schedule in ('fifo','lifo','random'):
            result=self.run_source(source,'--schedule',schedule)
            self.assertEqual(result.returncode,0,result.stderr)
            data=json.loads(result.stdout)
            self.assertEqual(data['roots'][0]['value'],'4')
            tags=[tag for site in data['annotations'] for tag in site['text']]
            for tag in ('[[record(tag)]]','[[field]]','[[function]]','[[parameter]]','[[statement]]','[[global]]','[[expression]]','[[opaque(unbound,"a\\n",-7)]]'):
                self.assertIn(tag,tags)

    def test_array_prefix_and_qualified_calls(self):
        result=self.run_source('&f(x:#u8)->#u8{x+1} ^answer:#u8={x:#u8=4;a=[[x]];a[0][0]+pkg::mod::f(2)};')
        self.assertEqual(result.returncode,0,result.stderr);self.assertEqual(json.loads(result.stdout)['roots'][0]['value'],'7')
        result=self.run_source('^answer:#u8={x:#u8=4;[[x]][0][0]};')
        self.assertEqual(result.returncode,0,result.stderr);self.assertEqual(json.loads(result.stdout)['roots'][0]['value'],'4')

    def test_strictness_levels(self):
        result=self.run_source('^answer:#u8={x=1;x};','--strictness','1')
        self.assertEqual(result.returncode,1);self.assertIn('explicit type',result.stderr)
        result=self.run_source('&f(x){x} ^answer:#u8=f(1);','--strictness','1')
        self.assertEqual(result.returncode,0,result.stderr)
        result=self.run_source('&f(x){x} ^answer:#u8=f(1);','--strictness','2')
        self.assertEqual(result.returncode,1);self.assertIn('concrete arguments',result.stderr)
        result=self.run_source('&f(x){x} ^answer:#u8=f((#u8)1);','--strictness','2')
        self.assertEqual(result.returncode,0,result.stderr)
        result=self.run_source('^answer:#u8=1;','--strictness','3')
        self.assertEqual(result.returncode,1);self.assertIn('0, 1, or 2',result.stderr)

    def test_metadata_survives_backend_export(self):
        with tempfile.TemporaryDirectory(prefix='vexel-metadata-') as scratch:
            path=Path(scratch)/'main.vx';path.write_text('[[bank_hint(3)]] &^f(x:#u8)->#u8{x+1}')
            project=Path(scratch)/'project'
            result=subprocess.run([str(BINARY),str(path),'--emit-msx',str(project)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            tags=json.loads((project/'generated/annotations.json').read_text())
            self.assertIn('[[bank_hint(3)]]',[tag for site in tags['sites'] for tag in site['text']])

if __name__=='__main__':unittest.main(verbosity=2)
