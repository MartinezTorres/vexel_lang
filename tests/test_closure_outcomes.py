from pathlib import Path
import json
import subprocess
import tempfile
import unittest
import sys

EXE=Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()

class OutcomeTests(unittest.TestCase):
    def check(self,body,status,value=None,flags=()):
        with tempfile.TemporaryDirectory(prefix='vexel-closure-') as directory:
            path=Path(directory)/'main.vx';path.write_text('a=b;b=a;'+body)
            report=path.with_suffix('.json')
            for schedule in ('fifo','lifo','random'):
                for branch in ('forward','reverse','random'):
                    result=subprocess.run([str(EXE),str(path),'--json','--search-report',str(report),
                        '--schedule',schedule,'--branch-order',branch,*flags],capture_output=True,text=True,timeout=30)
                    evidence=json.loads(report.read_text())
                    self.assertEqual(evidence['status'],status,(schedule,branch,result.stderr,evidence))
                    if value is not None:
                        self.assertEqual(result.returncode,0,result.stderr)
                        self.assertEqual(json.loads(result.stdout)['roots'][0]['value'],value)
                    else:self.assertEqual(result.returncode,1,result.stderr)
                    if status=='unresolved':
                        self.assertIn('unresolved dependency cycle',result.stderr)
                        for puzzle in evidence['puzzles']:
                            self.assertNotEqual(puzzle['search']['status'],'unsatisfiable')
                    if status=='ambiguous':self.assertIn('ambiguous',result.stderr)

    def test_unfinished_branch_does_not_prove_unique(self):
        self.check('^answer:#u8=?{n=?(#u8,[0,1]);x:#u8[n+1];(n!=0)?a:(#u8)7};','unresolved')

    def test_pending_guard_triggers_remaining_search(self):
        self.check('^answer:#u8=?{n=?(#b,[0,1]);?(n==0);n?a:(#u8)7};','resolved','7')
        self.check('^answer:#u8=?{n=?(#b,[0,1]);n?a:(#u8)7};','unresolved')

    def test_all_unfinished_is_not_unsatisfiable(self):
        self.check('^answer:#u8=?{n=?(#u8,[0,1]);x:#u8[n+1];a+n};','unresolved')

    def test_distinct_completed_answers_prove_ambiguity(self):
        self.check('^answer:#u8=?{n=?(#u8,[0,1,2]);x:#u8[n+1];(n==2)?a:n};','ambiguous')

    def test_optional_does_not_treat_unfinished_as_absence(self):
        self.check('^answer:#u8={r:#u8=7;#{r=a;};r};','unresolved')

    def test_plain_cycle_has_structured_unresolved_status(self):
        self.check('^answer:#u8=a;','unresolved')

    def test_exhaustion_is_separate_from_unfinished(self):
        self.check('^answer:#u8=?{n=?(#u8,[0,1,2]);x:#u8[n+1];(n==2)?a:n};','exhausted',flags=('--max-decisions','1'))

if __name__=='__main__':unittest.main(verbosity=2)
