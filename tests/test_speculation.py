#!/usr/bin/env python3
"""Scoped failure restores semantic state while retaining premises of absence."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1) if len(sys.argv) > 1 else "build/vexel").resolve()


class SpeculationTests(unittest.TestCase):
    def check(self, source, expected=None, error=None, resource=False):
        with tempfile.TemporaryDirectory(prefix="vexel-speculation-") as scratch:
            path = Path(scratch) / "main.vx"
            path.write_text(source)
            (Path(scratch) / "data.bin").write_bytes(b"abc")
            for schedule in ("fifo", "lifo", "random"):
                run = subprocess.run([str(BINARY), str(path), "--json", "--schedule", schedule],
                                     capture_output=True, text=True, timeout=30)
                if error:
                    self.assertEqual(run.returncode, 1, (schedule, run.stderr))
                    self.assertIn(error, run.stderr)
                else:
                    self.assertEqual(run.returncode, 0, (schedule, run.stderr))
                    data = json.loads(run.stdout)
                    self.assertEqual({root["name"]: root["value"] for root in data["roots"]}, expected)
                    self.assertEqual(any(p.endswith("data.bin") for p in data["dependencies"]), resource)

    def test_field_probe_is_non_evaluating(self):
        self.check("""#Pair(left,right); &!read()->#Pair;
            ^a:#b=#Pair(1,2).?left;
            ^b:#b=#Pair(1,2).?missing;
            ^c:#b=#Pair(1/0,2).?left;
        """, {"a": "1", "b": "0", "c": "1"})

    def test_failed_block_restores_state(self):
        self.check("^answer:#u8={x:#u8=1;#{x=2;missing();};x};", {"answer": "1"})

    def test_successful_block_commits_state(self):
        self.check("^answer:#u8={x:#u8=1;#{x=2;};x};", {"answer": "2"})

    def test_failed_block_retains_observed_input_without_committing_state(self):
        self.check("^answer:#u8={#{data=::data.bin;missing();};42};", {"answer": "42"}, resource=True)
        self.check("^answer:#u8={x:#u8=0;#{data=::data.bin;x=|data|;};x};", {"answer": "3"}, resource=True)

    def test_type_mismatch_is_not_optional_absence(self):
        self.check('^answer:#u8={#{x:#u8="bad";};42};', error="cannot convert")
        self.check("^answer:#u8={#{1/0;};42};", error="division by zero")

    def test_optional_constraints_are_transactional(self):
        self.check("""^answer:#u8=?{
            x=?(#u8,[1,2]);
            #{?(x==1);missing();};
            ?(x==2); x
        };""", {"answer": "2"})

    def test_nested_optional_blocks(self):
        self.check("""^answer:#u8={
            x:#u8=1;
            #{x=2;#{x=3;missing();};};
            x
        };""", {"answer": "2"})


if __name__ == "__main__":
    unittest.main(verbosity=2)
