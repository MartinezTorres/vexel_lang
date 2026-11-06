#!/usr/bin/env python3
"""Compare joint type/layout answers against a small independent integer oracle."""
import itertools
import json
from pathlib import Path
import subprocess
import sys
import unittest

EXE = Path(sys.argv.pop(1)).resolve()


class JointTests(unittest.TestCase):
    def solve(self, mode, budget, schedule="fifo", branch="forward", *flags):
        run = subprocess.run(
            [str(EXE), "--mode", mode, "--budget", str(budget), "--schedule", schedule,
             "--branch-order", branch, *flags], capture_output=True, text=True, timeout=30,
        )
        self.assertIn(run.returncode, (0, 1), run.stderr)
        return json.loads(run.stdout)

    def test_joint_type_and_storage_oracle(self):
        for mode, budget in itertools.product(("serial", "overlap", "retained", "recursive"), range(9)):
            valid = []
            for bits in (8, 16):
                size = 3 * (bits // 8)
                for a, b in itertools.product(range(budget + 1), repeat=2):
                    extent = max(a + size, b + size)
                    if extent > budget:
                        continue
                    if mode != "serial" and not (a + size <= b or b + size <= a):
                        continue
                    valid.append(((0 if bits == 16 else 1, extent, a, b), bits, size))
            for schedule, branch in itertools.product(("fifo", "lifo", "random"), ("forward", "reverse")):
                with self.subTest(mode=mode, budget=budget, schedule=schedule, branch=branch):
                    result = self.solve(mode, budget, schedule, branch)
                    if not valid:
                        self.assertEqual(result["status"], "contradictory", result)
                        self.assertNotIn("plan", result)
                        continue
                    cost, bits, size = min(valid)
                    self.assertEqual(result["status"], "resolved", result)
                    plan = result["plan"]
                    self.assertEqual(plan["element_bits"], bits)
                    self.assertEqual(plan["buffer_bytes"], size)
                    self.assertEqual(plan["extent"], cost[1])
                    self.assertEqual(plan["offsets"], list(cost[2:]))

    def test_open_boundaries_do_not_publish_a_plan(self):
        for schedule in ("fifo", "lifo", "random"):
            result = self.solve("open", 6, schedule)
            self.assertEqual(result["status"], "unresolved", result)
            self.assertNotIn("plan", result)

    def test_budget_exhaustion_does_not_publish_a_provisional_plan(self):
        for flag, value in (("--max-work", "1"), ("--max-decisions", "1")):
            result = self.solve("serial", 6, "fifo", "forward", flag, value)
            self.assertEqual(result["status"], "exhausted", result)
            self.assertNotIn("plan", result)


if __name__ == "__main__":
    unittest.main(verbosity=2)
