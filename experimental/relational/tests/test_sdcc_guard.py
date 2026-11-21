#!/usr/bin/env python3
"""Check host build limits without allocating compiler-sized workloads."""
import json
import os
from pathlib import Path
import subprocess
import signal
import time
import sys
import tempfile
import unittest

GUARD = Path(__file__).resolve().parents[3]/'backends/msx-neo8/tools/sdcc_guard.py'


class CompilerGuardTests(unittest.TestCase):
    def test_limits_and_exit_status(self):
        cases = [('print("ok")', 0, 'completed'),
                 ('x=bytearray(128*1024*1024)', 125, 'memory-limit'),
                 ('import time;time.sleep(5)', 125, 'timeout'),
                 ('raise SystemExit(7)', 7, 'compiler-failure')]
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp)/'resources.jsonl'
            # These children are tiny Python fixtures, not compilers. Give the
            # test its own temporary gate so real guarded SDCC work cannot use
            # up the fixture's ten-second assertion timeout.
            env = dict(os.environ, TMPDIR=tmp)
            for code, status, reason in cases:
                result = subprocess.run([sys.executable, GUARD, '--memory-mb', '64', '--timeout', '1',
                    '--log', log, sys.executable, '-c', code], capture_output=True, text=True, timeout=10, env=env)
                self.assertEqual(result.returncode, status, result.stderr)
                row = json.loads(log.read_text().splitlines()[-1])
                self.assertEqual(row['reason'], reason)
                self.assertGreaterEqual(row['queue_seconds'], 0)
                self.assertGreaterEqual(row['cpu_seconds'], 0)
                self.assertEqual(row['cpu_limit_seconds'], 1)
                self.assertLess(row['peak_rss_kb'], 64*1024)

    def test_shared_gate(self):
        with tempfile.TemporaryDirectory() as tmp:
            events = Path(tmp)/'events'
            code = ('import sys,time;f=open(sys.argv[1],"a",buffering=1);'
                    'f.write("begin\\n");time.sleep(.2);f.write("end\\n")')
            log = Path(tmp)/'resources.jsonl'
            command = [sys.executable, GUARD, '--log', log, sys.executable, '-c', code, events]
            children = [subprocess.Popen(command, env=dict(os.environ, TMPDIR=tmp)) for _ in range(2)]
            try:
                for child in children:
                    self.assertEqual(child.wait(timeout=10), 0)
            finally:
                for child in children:
                    if child.poll() is None:
                        child.terminate()
                    child.wait(timeout=10)
            self.assertEqual(events.read_text().splitlines(), ['begin','end','begin','end'])
            rows = [json.loads(line) for line in log.read_text().splitlines()]
            self.assertGreaterEqual(max(r['queue_seconds'] for r in rows), .15)



    @unittest.skipUnless(Path('/proc/self/stat').exists(), 'requires Linux process status')
    def test_lock_and_cpu_backstop_survive_supervisor_death(self):
        # An outer project's hard deadline must not allow another compiler to
        # overlap an orphaned job. The child keeps the gate and its CPU limit.
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            ready, log = root/'ready', root/'resources.jsonl'
            code = ('import os,pathlib,sys;pathlib.Path(sys.argv[1]).write_text(str(os.getpid()))\n'
                    'while True: pass\n')
            env = dict(os.environ, TMPDIR=tmp)
            guard = subprocess.Popen([sys.executable, GUARD, '--memory-mb', '64', '--timeout', '2',
                                      sys.executable, '-c', code, ready],
                                     env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            child_pid = None
            try:
                deadline = time.monotonic()+5
                while time.monotonic() < deadline:
                    if ready.exists() and ready.read_text().strip():
                        child_pid = int(ready.read_text())
                        break
                    time.sleep(.01)
                self.assertIsNotNone(child_pid, 'guarded child did not start')
                guard.kill()
                guard.wait(timeout=2)
                # This quick job must wait for the abandoned CPU-bound job's
                # inherited kernel limit, even though its supervisor is gone.
                result = subprocess.run([sys.executable, GUARD, '--timeout', '1', '--log', log,
                                         sys.executable, '-c', 'print("next")'],
                                        env=env, capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                state = Path(f'/proc/{child_pid}/stat')
                try:
                    state_name = state.read_text().split()[2]
                except FileNotFoundError:
                    state_name = None
                self.assertIn(state_name, (None, 'Z'), 'the next job overlapped a running orphan')
                row = json.loads(log.read_text())
                self.assertGreater(row['queue_seconds'], .2)
            finally:
                if guard.poll() is None:
                    guard.kill()
                guard.wait(timeout=2)
                if child_pid is not None:
                    try:
                        os.kill(child_pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass


if __name__ == '__main__':
    unittest.main(verbosity=2)
