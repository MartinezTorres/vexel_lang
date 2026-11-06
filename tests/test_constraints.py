#!/usr/bin/env python3
"""Independent finite oracles and source-level constraint/search acceptance."""
import ast
import itertools
import json
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1) if len(sys.argv) > 1 else "build/vexel").resolve()
REPO = Path(__file__).resolve().parent.parent


class ConstraintTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="vexel-constraints-")
        self.directory = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def run_source(self, source, *args, schedule="fifo", branch="forward"):
        path = self.directory / "main.vx"
        path.write_text(source)
        report = self.directory / "search.json"
        run = subprocess.run(
            [str(BINARY), str(path), "--search-report", str(report),
             "--schedule", schedule, "--branch-order", branch, *args],
            capture_output=True, text=True, timeout=60,
        )
        data = json.loads(report.read_text()) if report.exists() else {}
        return run, data

    def value(self, source, expected):
        for schedule, branch in itertools.product(("fifo", "lifo", "random"), ("forward", "reverse")):
            run, report = self.run_source(source, schedule=schedule, branch=branch)
            self.assertEqual(run.returncode, 0, (schedule, branch, run.stderr))
            actual = run.stdout.splitlines()[0].split(" = ", 1)[1]
            self.assertEqual(actual, expected)
            self.assertEqual(report["puzzles"][-1]["search"]["status"], "unique")

    def test_simultaneous_arithmetic(self):
        self.value((REPO / "examples/constraints.vx").read_text(), "3")

    def test_aliases_and_observable_uniqueness(self):
        self.value("^answer:#u8=?{x=?(#u8,1..10);x-x};", "0")
        self.value("^answer:#u8=?{x=?(#u8,1..10);y=?(#u8,1..10);?(x+y==10);x+y};", "10")
        self.value("^answer:#u8=?{unused=?(#u8,1..10);42};", "42")

    def test_ambiguity_and_impossibility_are_distinct(self):
        cases = [
            ("^answer:#u8=?{x=?(#u8,1..3);x};", "ambiguous", "ambiguous"),
            ("^answer:#u8=?{x=?(#u8,1..3);?(x==3);x};", "unsatisfiable", "no solution"),
        ]
        for source, status, message in cases:
            for schedule in ("fifo", "lifo", "random"):
                run, report = self.run_source(source, schedule=schedule)
                self.assertEqual(run.returncode, 1, run.stderr)
                self.assertIn(message, run.stderr)
                self.assertEqual(report["puzzles"][0]["search"]["status"], status)

    def test_odd_cycle_requires_backtracking(self):
        source = """^answer:#u8=?{
            x=?(#u8,[1,2]); y=?(#u8,[1,2]); z=?(#u8,[1,2]);
            ?(x!=y); ?(y!=z); ?(z!=x); x
        };"""
        run, report = self.run_source(source)
        self.assertEqual(run.returncode, 1, run.stderr)
        stats = report["puzzles"][0]["search"]
        self.assertEqual(stats["status"], "unsatisfiable")
        self.assertGreater(stats["decisions"], 0)
        self.assertGreater(stats["backtracks"], 0)
        self.assertGreater(stats["conflicts"], 0)

    def test_budget_exhaustion_is_not_impossibility(self):
        source = "^answer:#u8=?{x=?(#u8,1..10); y=?(#u8,1..10); ?(x!=y);x};"
        run, report = self.run_source(source, "--max-checks", "1")
        self.assertEqual(run.returncode, 1)
        self.assertIn("budget exhausted", run.stderr)
        self.assertNotIn("no solution", run.stderr)
        self.assertEqual(report["puzzles"][0]["search"]["status"], "exhausted")

    def test_wrapping_and_unary_relations(self):
        self.value("^answer:#u8=?{x=?(#u8,[0,255]);?(x+1==0);x};", "255")
        self.value("^answer:#i8=?{x=?(#i8,[-3,2]);?(-x==3);x};", "-3")
        self.value("^answer:#b=?{x=?(#b,[0,1]);?(!x);x};", "0")

    def test_conditional_constraints_and_traps(self):
        self.value("""^answer:#u8=?{
            x=?(#u8,[0,1]); (x==0)?{?(0);}; x
        };""", "1")
        self.value("""^answer:#u8=?{
            x=?(#u8,[0,1]); result=(x==0)?7:(1/0); result
        };""", "7")
        self.value("""^answer:#u8=?{
            x=?(#u8,[0,1]); ?((x==0)||(10/x==10)); ?(x==0); x
        };""", "0")
        self.value("""^answer:#u8=?{
            x=?(#u8,[0,1]); value:#u8=0;
            (x==0)?{value=10;}:{value=20;};
            ?(value==20); x
        };""", "1")

    def test_successful_backtracking_restores_domains(self):
        source = """^answer=?{
            q=?(#u8[4],0..4);
            0..3@{i=_;(i+1)..4@{j=_;
                ?(q[i]!=q[j]);
                ?(|(#i8)q[i]-(#i8)q[j]|!=j-i);
            };};
            ?(q[0]<q[3]); q
        };"""
        self.value(source, "[1, 3, 0, 2]")
        run, report = self.run_source(source)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertGreater(report["puzzles"][0]["search"]["conflicts"], 0)

    def test_symbolic_indices_and_updates(self):
        self.value("^answer:#u8=?{i=?(#u8,0..5);a=[7,11,13];?(a[i]==11);i};", "1")
        self.value("^answer:#u8[3]=?{i=?(#u8,0..3);a=[4,5,6];a[i]=9;?(a[1]==9);a};", "[4, 9, 6]")
        self.value("#P(x:#u8,y:#u8);^answer=?{i=?(#u8,[0,1]);a=[#P(1,2),#P(3,4)];?(a[i].y==4);a[i]};", "[3, 4]")
        self.value("^answer:#u8=?{i=?(#u8,[0,1]);a=[8];x=(i==0)?a[0]:a[7];x};", "8")
        self.value('^answer:#u8=?{i=?(#u8,0..5);?("abcd"[i]==\'c\');i};', "2")

    def test_candidate_failures_do_not_hide_type_errors(self):
        run, _ = self.run_source('^answer=?{x=?(#u8,[1,2]);x+"bad"};')
        self.assertEqual(run.returncode, 1)
        self.assertIn("incompatible types", run.stderr)
        self.assertNotIn("no solution", run.stderr)

    def test_choice_freshness_and_guarded_call_cache(self):
        self.value('&pick(){?(#u8,[1,2])} ^answer:#u8=?{a=pick();b=pick();?(a!=b);a+b};','3')
        source='&f(x:#u8){8/x;9} ^answer:#u8=?{x=?(#u8,[0,1]);(x==1)?{f(x);};f(x);?(x==0);9};'
        for schedule in ('fifo','lifo','random'):
            run,report=self.run_source(source,schedule=schedule)
            self.assertEqual(run.returncode,1,run.stderr)
            self.assertIn('no solution',run.stderr)
            self.assertEqual(report['puzzles'][0]['search']['status'],'unsatisfiable')

    def test_decision_budget_can_finish_at_limit(self):
        run, report = self.run_source("^answer:#u8=?{x=?(#u8,[1,2]);9};", "--max-decisions", "1")
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(report["puzzles"][0]["search"]["status"], "unique")

    def test_budgets_are_shared_across_solve_regions(self):
        source='a=?{x=?(#u8,[1,2]);9};b=?{y=?(#u8,[1,2]);8};^answer:#u8=a+b;'
        run, report=self.run_source(source,'--max-decisions','1')
        self.assertEqual(run.returncode,1,run.stderr)
        self.assertIn('budget exhausted',run.stderr)
        self.assertEqual(report['work']['decisions'],1)

    def test_finite_oracle_and_constraint_permutations(self):
        randomizer = random.Random(473)
        for _ in range(16):
            target_sum = randomizer.randrange(3, 14)
            target_product = randomizer.randrange(1, 37)
            answers = {x for x, y in itertools.product(range(1, 7), repeat=2)
                       if x + y == target_sum and x * y == target_product}
            constraints = [f"?(x+y=={target_sum});", f"?(x*y=={target_product});"]
            for ordering in itertools.permutations(constraints):
                source = "^answer:#u8=?{x=?(#u8,1..7);y=?(#u8,1..7);" + "".join(ordering) + "x};"
                run, report = self.run_source(source)
                actual = report["puzzles"][0]["search"]["status"]
                expected = "unsatisfiable" if not answers else "unique" if len(answers) == 1 else "ambiguous"
                self.assertEqual(actual, expected, (source, run.stderr))
                if len(answers) == 1:
                    self.assertEqual(run.returncode, 0, run.stderr)
                    self.assertIn(f"= {next(iter(answers))}\n", run.stdout)

    def test_sudoku(self):
        source = (REPO / "examples/sudoku.vx").read_text()
        clues = ast.literal_eval(source.split("clues = ", 1)[1].split(";", 1)[0])
        solutions = []
        for schedule, branch in (("fifo", "forward"), ("lifo", "reverse"), ("random", "forward")):
            run, report = self.run_source(source, "--root", "solution", schedule=schedule, branch=branch)
            self.assertEqual(run.returncode, 0, run.stderr)
            grid = ast.literal_eval(run.stdout.splitlines()[0].split(" = ", 1)[1])
            self.assertEqual(len(grid), 81)
            for index, clue in enumerate(clues):
                if clue:
                    self.assertEqual(grid[index], clue)
            groups = [grid[row*9:row*9+9] for row in range(9)]
            groups += [grid[col::9] for col in range(9)]
            groups += [[grid[(br+r)*9+bc+c] for r in range(3) for c in range(3)]
                       for br in (0, 3, 6) for bc in (0, 3, 6)]
            self.assertTrue(all(set(group) == set(range(1, 10)) for group in groups))
            self.assertEqual(report["puzzles"][0]["search"]["status"], "unique")
            solutions.append(grid)
        self.assertEqual(solutions[0], solutions[1])
        self.assertEqual(solutions[0], solutions[2])


if __name__ == "__main__":
    unittest.main(verbosity=2)
