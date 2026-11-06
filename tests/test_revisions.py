#!/usr/bin/env python3
"""Candidate-dependent structure closes only after every revision is checked."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()
class RevisionTests(unittest.TestCase):
    def check(self,source,value=None,error=None,flags=()):
        with tempfile.TemporaryDirectory(prefix='vexel-revisions-') as scratch:
            path=Path(scratch)/'main.vx';path.write_text(source)
            for schedule in ('fifo','lifo','random'):
                for branch in ('forward','reverse'):
                    report=Path(scratch)/'report.json'
                    run=subprocess.run([str(BINARY),str(path),'--root','answer','--json','--schedule',schedule,'--branch-order',branch,'--search-report',str(report),*flags],capture_output=True,text=True,timeout=30)
                    if error:
                        self.assertEqual(run.returncode,1,run.stderr);self.assertIn(error,run.stderr)
                    else:
                        self.assertEqual(run.returncode,0,(schedule,branch,run.stderr))
                        self.assertEqual(json.loads(run.stdout)['roots'][0]['value'],value)
                        self.assertEqual(json.loads(report.read_text())['puzzles'][0]['search']['status'],'unique')

    def test_choice_determines_array_type(self):
        self.check('answer=?{n=?(#u8,1..5);a:#u8[n]=[7,8,9];n};','3')
        self.check('answer=?{n=?(#u8,[3]);a:#u8[n]=[7,8,9];n};','3')
        self.check('answer=?{n=?(#u8,[2,3]);a:#u8[n+1]=[7,8,9];n};','2')

    def test_ambiguous_shapes_and_hidden_choices(self):
        self.check('answer=?{n=?(#u8,1..4);a=0..n;a};',error='ambiguous')
        self.check('answer=?{n=?(#u8,1..4);a=0..n;7};','7')
        self.check('answer=?{n=?(#u8,1..4);a=0..n;?(|a| == 9);a};',error='no solution')

    def test_range_constraints_follow_late_shape(self):
        self.check('answer=?{n=?(#u8,1..6);a=0..n;total:#u8=0;a@{total+=_;};?(total==6);n};','4')
        self.check('&pick(){?(#u8,[1,2])} answer=?{n=pick();m=pick();a=0..n;b=0..m;?(n<m);n+m};','3')

    def test_symbolic_repeat_and_sorted_iteration(self):
        self.check('answer=?{n=?(#u8,1..4);i:#u8=0;(i<n)@{i+=1;};?(i==2);n};','2')
        self.check('answer=?{a=?(#u8[2],1..3);sum:#u8=0;a@@{sum+=_;};?(sum==2);a};','[1, 1]')

    def test_other_roots_and_prior_puzzles_survive(self):
        self.check('first=?{x=?(#u8,1..4);?(x==2);x};answer={before=first;after=?{n=?(#u8,1..5);a:#u8[n]=[1,2,3];n};before+after};','5')

    def test_conditional_types_and_guarded_semantic_failure(self):
        self.check('#A(x:#u8);#B(y:#u8);answer=?{b=?(#b,[0,1]);p=b?#A(7):#B(8);?(b);p.x};','7')
        self.check('answer=?{b=?(#b,[0,1]);b?{unavailable();};9};','9')
        self.check('answer=?{b=?(#b,[0,1]);b?[1,2]:[3]};',error='ambiguous')
        self.check('answer=?{n=?(#u8,1..4);#{a=0..n;?(n==2);};n};','2')

    def test_diagnostic_wording_does_not_control_backtracking(self):
        self.check('answer=?{b=?(#b,[0,1]);b?{unavailable_budget();};9};','9')
        self.check('answer=?{b=?(#b,[0,1]);b?{x:#i0.1=0;};9};','9')
        self.check('answer=?{b=?(#b,[0,1]);b?{x:#u8[-1]=[];};9};','9')

    def test_field_and_state_properties_survive_revisions(self):
        self.check('&make(offset:#u8){#P(count:#u8,data:#u8[count+offset]);#P(2,[1,2,3])}'
                   'answer=?{n=?(#u8,[1,2]);p=make(n);p.count};','2')
        self.check('&shape(n:#u8,flag:#b){flag?{}:{};data:#u8[n];data}'
                   'answer=?{n=?(#u8,[2,3]);flag:#b;data:#[shape(n,flag)]=[7,8,9];n};','3')

    def test_exhaustion_is_not_evidence_against_a_revision(self):
        self.check('answer=?{n=?(#u8,[2,4]);a:#u8[n]=[7,8];n};',error='element budget',flags=('--max-elements','3'))
        self.check('answer=?{n=?(#u8,[2,9]);a=1<<n;?(n==2);n};',error='bit budget',flags=('--max-bits','8'))
        self.check('answer=?{b=?(#b,[0,1]);b?{x:#u9=0;};9};',error='bit budget',flags=('--max-bits','8'))
        self.check('answer=?{b=?(#b,[0,1]);b?{#{x:#u8[4]=[1,2,3,4];};};9};',error='element budget',flags=('--max-elements','3'))

if __name__=='__main__':unittest.main(verbosity=2)
