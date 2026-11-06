#!/usr/bin/env python3
"""Finite-region source semantics, including observable uniqueness and guards."""
import itertools
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
EXE=Path(sys.argv.pop(1)).resolve()
ROOT=Path(__file__).resolve().parents[3]
ORDERS=list(itertools.product(('fifo','lifo','random'),('forward','reverse')))
class FiniteTests(unittest.TestCase):
    def check(self,source,status,answer=None,flags=()):
        with tempfile.TemporaryDirectory(prefix='vexel-finite-') as temp:
            path=Path(temp)/'puzzle.vx';path.write_text(source)
            for schedule,branch in ORDERS:
                with self.subTest(schedule=schedule,branch=branch,source=source):
                    r=subprocess.run([str(EXE),str(path),'--query','value:a','--schedule',schedule,'--branch-order',branch,*flags],capture_output=True,text=True,timeout=120)
                    self.assertIn(r.returncode,(0,1),r.stdout+r.stderr)
                    report=json.loads(r.stdout);self.assertEqual(report['status'],status,report)
                    if answer is not None:self.assertEqual(report['queries'][0]['answer'],answer)
    def test_scalar_and_array_constraints(self):
        self.check('a=?{x=?(#u8,1..10);y=?(#u8,1..10);?(x+y==10);?(x*y==21);?(x<y);x};','resolved','3')
        self.check('a=?{x=?(#u8[3],1..4);0..3@{?(x[_]==_+1);};x};','resolved','Values(1,2,3)')
        self.check('&f(x){?(x!=1);x}a=?{f(?(#u8,1..3))};','resolved','2')
        self.check('a=?{x=?(#u8,1..4);?(x==4);x};','contradictory')
        self.check('a=?{x=?(#u8,1..4);x};','ambiguous')
        self.check('a=?{x=?(#u8,1..4);7};','resolved','7')
        self.check('a=?{x=?(#u8,1..4);missing};','unresolved')
        self.check('a=?{x=?(#u8,1..4);x};','exhausted',flags=('--max-decisions','0'))
    def test_guarded_requirements_and_invalid_candidates(self):
        self.check('a=?{x=?(#u8,0..3);x==0 ? {?(0);} : {?(x==2);};x};','resolved','2')
        self.check('a=?{x=?(#u8,0..3);0 ? {?(x==17);};?(x==1);x};','resolved','1')
        self.check('a=?{x=?(#u8,0..3);?(10/x==5);x};','resolved','2')
        self.check('a=?{x=?(#u8,0..3);?(x==2);x+255};','resolved','1')
    def test_control_refinement_preserves_partial_values(self):
        # These equations determine every leaf directly. A control-value
        # simplifier must not wait for guessed values before exposing them.
        self.check('a=?{x=?(#u8,1..10);?(x==7);x};','resolved','7',flags=('--max-decisions','0'))
        self.check('a=?{c=?(#u8[4],1..5);0..4@{?(c[_]==_+1);};c};',
                   'resolved','Values(1,2,3,4)',flags=('--max-decisions','0'))
    def test_independent_small_latin_square_oracle(self):
        # Four cells with row/column uniqueness; enumerate independently in Python.
        solutions=[x for x in itertools.product((1,2),repeat=4) if x[0]==1 and x[0]!=x[1] and x[2]!=x[3] and x[0]!=x[2] and x[1]!=x[3]]
        self.assertEqual(solutions,[(1,2,2,1)])
        source='a=?{c=?(#u8[4],1..3);?(c[0]==1);0..2@{i=_;?(c[i*2]!=c[i*2+1]);?(c[i]!=c[i+2]);};c};'
        self.check(source,'resolved','Values('+','.join(map(str,solutions[0]))+')')
if __name__=='__main__':unittest.main(verbosity=2)
