#!/usr/bin/env python3
"""Generated native filters are interchangeable with ordinary propagation."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()
REPO=Path(__file__).resolve().parent.parent
class KernelTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='vexel-kernels-');self.base=Path(self.tmp.name)
    def tearDown(self):self.tmp.cleanup()
    def check(self,source,*args):
        path=self.base/'main.vx';path.write_text(source)
        results=[]
        for options in ([],['--compiled-search',str(self.base/'kernels'),'--verify-search-kernel']):
            report=self.base/'report.json'
            run=subprocess.run([str(BINARY),str(path),'--search-report',str(report),*options,*args],capture_output=True,text=True,timeout=60)
            data=json.loads(report.read_text())['puzzles'][0]['search']
            results.append((run,data))
        ordinary,native=results
        self.assertEqual(native[0].returncode,ordinary[0].returncode,native[0].stderr)
        self.assertEqual(native[0].stdout,ordinary[0].stdout)
        self.assertEqual(native[1]['kernel'],'compiled-checked')
        for key in ('status','propagations','tuple_checks','decisions','conflicts','backtracks','solutions','events'):
            self.assertEqual(native[1][key],ordinary[1][key],key)
        directory=Path(native[1]['kernel_directory'])
        self.assertTrue((directory/'relations.cpp').is_file())
        self.assertTrue((directory/'Makefile').is_file())

    def test_solutions_ambiguity_and_conflicts(self):
        for source in [
            '^answer:#u8=?{x=?(#u8,1..5);?(x*x==9);x};',
            '^answer:#u8=?{x=?(#u8,[1,2]);x};',
            '^answer:#u8=?{x=?(#u8,[1,2]);y=?(#u8,[1,2]);z=?(#u8,[1,2]);?(x!=y);?(y!=z);?(z!=x);x};',
            '^answer:#u8=?{?(0);1};',
            '^answer:#u8=?{x=?(#u8,0..4);?(x!=0);8/x};',
        ]:self.check(source)

    def test_budget_and_sudoku(self):
        self.check('^answer:#u8=?{x=?(#u8,1..8);?(x*x==9);x};','--max-checks','10')
        self.check((REPO/'examples/sudoku.vx').read_text(),'--root','solution')

    def test_compilation_failure_uses_ordinary_solver(self):
        path=self.base/'main.vx';path.write_text('^answer:#u8=?{x=?(#u8,1..5);?(x*x==9);x};')
        report=self.base/'report.json';env=dict(os.environ,CXX='/does/not/exist')
        run=subprocess.run([str(BINARY),str(path),'--search-report',str(report),'--compiled-search',str(self.base/'kernels')],capture_output=True,text=True,env=env,timeout=60)
        self.assertEqual(run.returncode,0,run.stderr)
        data=json.loads(report.read_text())['puzzles'][0]['search']
        self.assertEqual(data['status'],'unique');self.assertEqual(data['kernel'],'compile-provider-failed')
        self.assertIn('= 3',run.stdout)

if __name__=='__main__':unittest.main(verbosity=2)
