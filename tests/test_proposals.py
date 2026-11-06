#!/usr/bin/env python3
"""Advice changes exploration order, never the accepted meaning or proof."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()
SOURCE = '^answer:#u8=?{x=?(#u8,[1,2]);y=?(#u8,[1,2]);z=?(#u8,[1,2]);?(x!=y);?(y!=z);?(z!=x);x};'

class ProposalTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='vexel-advice-')
        self.base = Path(self.tmp.name)
        self.source = self.base/'main.vx'
        self.source.write_text(SOURCE)
        self.report = self.base/'search.json'

    def tearDown(self):
        self.tmp.cleanup()

    def provider(self, body):
        path = self.base/'provider'
        path.write_text('#!/usr/bin/env python3\nimport json,sys,time\nr=json.load(sys.stdin)\n'+body+'\n')
        path.chmod(0o700)
        return path

    def run_solver(self, *args):
        run = subprocess.run([str(BINARY), str(self.source), '--search-report', str(self.report), *map(str,args)], capture_output=True, text=True, timeout=20)
        report = json.loads(self.report.read_text())['puzzles'][0]['search']
        return run,report

    def test_valid_advice_and_offline_replay(self):
        provider = self.provider("v=next(v for v in r['variables'] if v['id']==r['fallback'])\nprint(json.dumps(dict(id=r['id'],variable=v['id'],value=v['domain'][-1])))")
        run, report = self.run_solver('--heuristic', provider)
        self.assertEqual(run.returncode,1,run.stderr)
        self.assertEqual(report['status'],'unsatisfiable')
        self.assertEqual(report['proposals'][0]['status'],'accepted')
        saved = self.base/'saved.json'
        saved.write_text(self.report.read_text())
        provider.unlink()  # Replay must not try to start it.
        again, replay = self.run_solver('--heuristic-replay', saved)
        self.assertEqual(again.returncode,run.returncode)
        self.assertEqual(replay['status'],report['status'])
        self.assertEqual(replay['decisions'],report['decisions'])
        self.assertEqual(replay['proposals'][0]['status'],'replayed')
        self.assertEqual([e for e in replay['events'] if e['kind']!='heuristic'],[e for e in report['events'] if e['kind']!='heuristic'])

    def test_invalid_advice_falls_back(self):
        for body, status in [
            ("print('not JSON')",'rejected'),
            ("print(json.dumps(dict(id='stale',variable=r['fallback'],value='1')))",'rejected'),
            ("print(json.dumps(dict(id=r['id'],variable=999999,value='1')))",'rejected'),
            ("print(json.dumps(dict(id=r['id'],variable=r['fallback'],value='999999')))",'rejected'),
            ("print('['*1000+'0'+']'*1000)",'rejected'),
            ("print('x'*70000)",'response-too-large'),
            ("sys.exit(9)",'provider-failed'),
            ("time.sleep(2)",'timeout'),
        ]:
            with self.subTest(status=status,body=body):
                provider = self.provider(body)
                run,report = self.run_solver('--heuristic',provider,'--heuristic-timeout','150')
                self.assertEqual(run.returncode,1,run.stderr)
                self.assertEqual(report['status'],'unsatisfiable')
                self.assertEqual(report['proposals'][0]['status'],status)

    def test_missing_provider_and_call_budget(self):
        run,report = self.run_solver('--heuristic',self.base/'absent')
        self.assertEqual(report['status'],'unsatisfiable')
        self.assertIn(report['proposals'][0]['status'],('provider-failed','io-error'))
        self.source.write_text('^answer:#u8=?{a=?(#u8,[1,2]);b=?(#u8,[1,2]);c=?(#u8,[1,2]);a+b+c};')
        provider = self.provider("print('{}')")
        run,report = self.run_solver('--heuristic',provider,'--max-proposals','1')
        self.assertEqual(report['status'],'ambiguous')
        self.assertEqual(len(report['proposals']),1)

    def test_stale_replay_and_source_change(self):
        provider = self.provider("v=r['variables'][0]\nprint(json.dumps(dict(id=r['id'],variable=v['id'],value=v['domain'][-1])))")
        self.run_solver('--heuristic',provider)
        saved = self.base/'saved.json'; saved.write_text(self.report.read_text())
        self.source.write_text(SOURCE.replace('?(z!=x);',''))
        run,report = self.run_solver('--heuristic-replay',saved)
        self.assertEqual(report['status'],'ambiguous')
        self.assertEqual(report['proposals'][0]['status'],'replay-miss')

if __name__=='__main__': unittest.main(verbosity=2)
