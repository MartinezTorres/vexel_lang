#!/usr/bin/env python3
"""Structural advice only reorders checked alternatives and survives rollback."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()
SOURCE='^answer:#u8=?{n=?(#u8,[1,2,3]);data:#u8[n]=[7,8,9];data[0]};'
LAST="v=next(v for v in r['variables'] if v['id']==r['fallback'])\nprint(json.dumps(dict(id=r['id'],variable=v['id'],value=v['domain'][-1])))"


class StructuralProposalTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='vexel-structural-advice-')
        self.base=Path(self.tmp.name);self.source=self.base/'main.vx';self.source.write_text(SOURCE)
        self.report=self.base/'search.json'

    def tearDown(self):
        self.tmp.cleanup()

    def provider(self,body=LAST):
        path=self.base/'provider'
        path.write_text('#!/usr/bin/env python3\nimport json,sys,time\nr=json.load(sys.stdin)\n'+body+'\n')
        path.chmod(0o700);return path

    def run_solver(self,*args):
        result=subprocess.run([str(BINARY),str(self.source),'--json','--search-report',str(self.report),*map(str,args)],capture_output=True,text=True,timeout=30)
        return result,json.loads(self.report.read_text())

    def test_preferred_alternative_does_not_remove_other_candidates(self):
        provider=self.provider()
        for schedule in ('fifo','lifo','random'):
            for order in ('forward','reverse'):
                result,report=self.run_solver('--heuristic',provider,'--schedule',schedule,'--branch-order',order)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertEqual(json.loads(result.stdout)['roots'][0]['value'],'7')
                search=report['puzzles'][0]['search']
                self.assertEqual((search['structural_trials'],search['structural_conflicts']),(3,2))
                self.assertEqual(report['advice'][0]['status'],'accepted')
                request=json.loads(report['advice'][0]['request'])
                self.assertEqual(request['kind'],'structural')
                self.assertEqual(request['variables'][0]['domain'],['1','2','3'])
                self.assertIn('data:#u8[n]',request['context']['expression'])
                assumptions=[e['detail'] for e in search['events'] if e['kind']=='assume-structure']
                self.assertTrue(assumptions[0].endswith('=3'))
                self.assertEqual(len(assumptions),3)

    def test_offline_replay_preserves_structural_exploration(self):
        provider=self.provider();result,report=self.run_solver('--heuristic',provider)
        self.assertEqual(result.returncode,0,result.stderr)
        saved=self.base/'saved.json';saved.write_text(self.report.read_text());provider.unlink()
        result,replayed=self.run_solver('--heuristic-replay',saved)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(replayed['advice'][0]['status'],'replayed')
        def trace(value):
            return [e for e in value['puzzles'][0]['search']['events'] if not e['kind'].startswith('heuristic')]
        self.assertEqual(trace(report),trace(replayed));self.assertEqual(report['work'],replayed['work'])

    def test_invalid_stale_failed_and_timed_out_proposals_fall_back(self):
        for body,status in (
            ("print('not JSON')",'rejected'),
            ("print(json.dumps(dict(id='stale',variable=0,value='3')))",'rejected'),
            ("print(json.dumps(dict(id=r['id'],variable=1,value='3')))",'rejected'),
            ("print(json.dumps(dict(id=r['id'],variable=0,value='9')))",'rejected'),
            ("print(json.dumps(dict(id=r['id'],variable=0,value='3',prune=['1','2'])))",'rejected'),
            ("sys.exit(7)",'provider-failed'),
            ("time.sleep(2)",'timeout'),
            ("print('x'*70000)",'response-too-large'),
        ):
            with self.subTest(status=status,body=body):
                result,report=self.run_solver('--heuristic',self.provider(body),'--heuristic-timeout','150')
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertEqual(report['advice'][0]['status'],status)
                self.assertEqual(report['puzzles'][0]['search']['structural_trials'],3)

    def test_nested_revisions_share_the_call_budget_and_keep_rejected_evidence(self):
        self.source.write_text('^answer:#u8=?{n=?(#u8,[1,2]);m=?(#u8,[1,2]);a:#u8[m]=[0,0];b:#u8[n]=[0,0];7};')
        provider=self.provider(LAST.replace("['domain'][-1]","['domain'][0]"))
        result,report=self.run_solver('--heuristic',provider)
        self.assertEqual(result.returncode,0,result.stderr)
        requests=[json.loads(p['request']) for p in report['advice']]
        self.assertGreaterEqual(len(requests),3)
        self.assertEqual([r['sequence'] for r in requests],list(range(len(requests))))
        self.assertEqual({a['value'] for r in requests for a in r['context']['assumptions']},{'1','2'})
        saved=self.base/'nested.json';saved.write_text(self.report.read_text())
        result,replayed=self.run_solver('--heuristic-replay',saved)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual([p['status'] for p in replayed['advice']],['replayed']*len(requests))
        self.assertEqual(report['work'],replayed['work'])
        result,bounded=self.run_solver('--heuristic',provider,'--max-proposals','1')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(len(bounded['advice']),1)

    def test_numeric_and_structural_search_share_one_puzzle_budget(self):
        self.source.write_text('^answer:#u8=?{n=?(#u8,[1,2]);data:#u8[n]=[0,0];'
            'x=?(#u8,[1,2]);y=?(#u8,[1,2]);z=?(#u8,[1,2]);?(x!=y);?(y!=z);?(z!=x);x};')
        provider=self.provider();result,report=self.run_solver('--heuristic',provider,'--max-proposals','2')
        self.assertEqual(result.returncode,1,result.stderr);self.assertIn('no solution',result.stderr)
        self.assertEqual([json.loads(p['request'])['kind'] for p in report['advice']],['structural','numeric'])
        saved=self.base/'mixed.json';saved.write_text(self.report.read_text())
        result,replayed=self.run_solver('--heuristic-replay',saved,'--max-proposals','2')
        self.assertEqual(result.returncode,1,result.stderr);self.assertIn('no solution',result.stderr)
        self.assertEqual([p['status'] for p in replayed['advice']],['replayed','replayed'])
        self.assertEqual(report['work'],replayed['work'])

    def test_distinct_puzzles_keep_separate_proposal_budgets(self):
        self.source.write_text(SOURCE+SOURCE.replace('^answer','^second'))
        result,report=self.run_solver('--heuristic',self.provider(),'--max-proposals','1')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual({r['name']:r['value'] for r in json.loads(result.stdout)['roots']},{'answer':'7','second':'7'})
        self.assertEqual(len({p['context'] for p in report['advice']}),2)
        self.assertEqual([json.loads(p['request'])['sequence'] for p in report['advice']],[0,0])

    def test_changed_source_and_target_inputs_do_not_reuse_requests(self):
        provider=self.provider();result,report=self.run_solver('--heuristic',provider)
        self.assertEqual(result.returncode,0,result.stderr)
        saved=self.base/'source.json';saved.write_text(self.report.read_text())
        self.source.write_text(SOURCE.replace('[7,8,9]','[7,8,0]'))
        result,changed=self.run_solver('--heuristic-replay',saved)
        self.assertEqual(result.returncode,0,result.stderr);self.assertEqual(changed['advice'][0]['status'],'replay-miss')
        self.source.write_text(SOURCE.replace('n=?','bytes=::data.bin;n=?').replace('data[0]','|bytes|'))
        resource=self.base/'data.bin';resource.write_bytes(b'abc')
        result,report=self.run_solver('--heuristic',provider)
        self.assertEqual(result.returncode,0,result.stderr)
        saved=self.base/'resource.json';saved.write_text(self.report.read_text());resource.write_bytes(b'abcd')
        result,changed=self.run_solver('--heuristic-replay',saved)
        self.assertEqual(result.returncode,0,result.stderr);self.assertEqual(changed['advice'][0]['status'],'replay-miss')
        self.source.write_text(SOURCE)
        result,report=self.run_solver('--heuristic',provider,'--emit-msx',self.base/'first','--msx-stack','1024')
        self.assertEqual(result.returncode,0,result.stderr)
        saved=self.base/'target.json';saved.write_text(self.report.read_text())
        result,changed=self.run_solver('--heuristic-replay',saved,'--emit-msx',self.base/'second','--msx-stack','2048')
        self.assertEqual(result.returncode,0,result.stderr);self.assertEqual(changed['advice'][0]['status'],'replay-miss')

    def test_advice_cannot_resolve_ambiguity_or_hide_exhaustion(self):
        self.source.write_text('^answer:#u8=?{n=?(#u8,[1,2]);a:#u8[n];n};')
        provider=self.provider()
        for flags,error in (((),'ambiguous'),(('--max-decisions','1'),'budget')):
            result,report=self.run_solver('--heuristic',provider,*flags)
            self.assertEqual(result.returncode,1,result.stderr);self.assertIn(error,result.stderr)
            self.assertEqual(report['advice'][0]['status'],'accepted')

    def test_oversized_requests_fall_back_and_corrupt_replay_is_diagnosed(self):
        self.source.write_text(SOURCE.replace('data:',(' '*70000)+'data:'))
        result,report=self.run_solver('--heuristic',self.provider())
        self.assertEqual(result.returncode,0,result.stderr);self.assertFalse(report['advice'])
        self.assertTrue(any(e['detail']=='request-too-large' for e in report['puzzles'][0]['search']['events']))
        self.source.write_text(SOURCE)
        invalid=self.base/'invalid.json';invalid.write_text('{"format":"wrong"}')
        result,report=self.run_solver('--heuristic-replay',invalid)
        self.assertEqual(result.returncode,1,result.stderr);self.assertIn('invalid heuristic replay',result.stderr)


if __name__=='__main__':unittest.main(verbosity=2)
