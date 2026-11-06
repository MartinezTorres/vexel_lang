#!/usr/bin/env python3
"""Edit live inputs; retained answers and emitted artifacts must match fresh solves."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXE = Path(sys.argv.pop(1)).resolve()


class Session:
    def __init__(self, verify=True):
        self.process = subprocess.Popen(
            [EXE, '--session', *(['--verify-incremental'] if verify else [])],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    def request(self, *args):
        return self.line(' '.join(json.dumps(str(arg), ensure_ascii=False) for arg in args))

    def line(self, text):
        self.process.stdin.write(text + '\n')
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            raise AssertionError('session exited: ' + self.process.stderr.read())
        return json.loads(line)

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=10)
            errors = self.process.stderr.read()
            if self.process.returncode or errors:
                raise AssertionError(f'session exit {self.process.returncode}: {errors}')
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait()
            self.process.stdout.close()
            self.process.stderr.close()


class IncrementalTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='vexel incremental ')
        self.root = Path(self.temp.name)
        self.source = self.root / 'source.vx'
        self.session = Session()

    def tearDown(self):
        self.session.close()
        self.temp.cleanup()

    def solve(self, *args, reused=False, answer=None, source=None):
        source = source or self.source
        report = self.session.request('solve', source, *args)
        fresh = subprocess.run([EXE, source, *map(str, args)], text=True,
                               capture_output=True, timeout=60)
        if fresh.returncode == 2:
            self.assertEqual(report['status'], 'error', report)
            self.assertEqual(report['error'], fresh.stderr.strip())
            return report
        self.assertIn(fresh.returncode, (0, 1), fresh.stderr)
        evidence = report.pop('incremental')
        self.assertEqual(report, json.loads(fresh.stdout))
        self.assertEqual(evidence['reused'], reused, evidence)
        if reused:
            self.assertEqual(evidence['solver_work'], 0)
            self.assertTrue(evidence['verified'])
        if answer is not None:
            self.assertEqual(report['queries'][0]['answer'], answer)
        report['incremental'] = evidence
        return report

    def test_disjoint_resources_and_exact_content_changes(self):
        self.source.write_text('a=::a.bin[0];b=::b.bin[0];')
        first, second = self.root / 'a.bin', self.root / 'b.bin'
        first.write_bytes(b'A'); second.write_bytes(b'B')
        original = self.solve('--query', 'value:a', answer='65')
        self.solve('--query', 'value:b', answer='66')
        self.solve('--query', 'value:a', reused=True, answer='65')
        stamp = first.stat()
        first.write_bytes(b'C')
        os.utime(first, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
        self.solve('--query', 'value:b', reused=True, answer='66')
        updated = self.solve('--query', 'value:a', answer='67')
        self.assertIn(str(first), updated['incremental']['invalidated_inputs'])
        self.assertNotEqual(original['incremental']['revision'], updated['incremental']['revision'])
        # An unrelated entry source is another group, even in this workspace.
        other = self.root / 'other.vx'; other.write_text('a=7;')
        self.solve('--query', 'value:a', source=other, answer='7')
        self.source.write_text('a=::a.bin[0]+1;b=::b.bin[0];')
        self.solve('--query', 'value:a', source=other, reused=True, answer='7')
        self.solve('--query', 'value:a', answer='68')

    def test_declaration_changes_negative_capabilities_and_recovery(self):
        module = self.root / 'part.vx'
        self.source.write_text('::part;a=?f((#u8)1);')
        for schedule, branch in [('fifo', 'forward'), ('fifo', 'reverse'),
                                 ('lifo', 'forward'), ('lifo', 'reverse'),
                                 ('random', 'forward'), ('random', 'reverse')]:
            args = ('--query', 'value:a', '--schedule', schedule, '--branch-order', branch)
            module.write_text('&f(x:#s){0}')
            self.solve(*args, answer='0'); self.solve(*args, reused=True, answer='0')
            module.write_text('&f(x:#u8){missing_in_body()}')
            self.solve(*args, answer='1')
            module.write_text('&f(x:#u8){1}&f(x:#u8){2}')
            self.assertEqual(self.solve(*args)['status'], 'ambiguous')
            self.assertEqual(self.solve(*args)['status'], 'ambiguous')
            module.write_text('&f(x:#u8){')
            self.assertEqual(self.solve(*args)['status'], 'error')
            module.write_text('&f(x:#s){0}')
            self.solve(*args, answer='0')
        # Even an unused body's import is parsed, so a newly available module
        # cannot sneak a syntax error past an old cached answer.
        directory = self.root / 'nested.vx'; directory.mkdir()
        self.source.write_text('&unused(){::nested;0}a=1;')
        self.solve('--query', 'value:a', answer='1')
        directory.rmdir(); directory.write_text('broken syntax')
        self.assertEqual(self.solve('--query', 'value:a')['status'], 'error')

    def test_precedence_missing_files_and_symlink_retargeting(self):
        sub = self.root / 'sub'; sub.mkdir()
        fallback = sub / 'data.bin'; fallback.write_bytes(b'R')
        (sub / 'module.vx').write_text('&read(){::data.bin[0]}')
        self.source.write_text('::sub::module;a=read();')
        self.solve('--query', 'value:a', answer='82')
        preferred = self.root / 'data.bin'; preferred.write_bytes(b'P')
        self.solve('--query', 'value:a', answer='80')
        preferred.unlink()
        self.solve('--query', 'value:a', answer='82')
        fallback.unlink()
        self.assertEqual(self.solve('--query', 'value:a')['status'], 'error')
        fallback.write_bytes(b'R')
        self.solve('--query', 'value:a', answer='82')
        # The content is identical but provenance has changed.
        identical = self.root / 'identical.bin'; identical.write_bytes(b'R')
        preferred.symlink_to(fallback)
        self.solve('--query', 'value:a', answer='82')
        preferred.unlink(); preferred.symlink_to(identical)
        self.solve('--query', 'value:a', answer='82')
        other = self.root / 'other.vx'; other.write_text('a=99;')
        entry = self.root / 'entry.vx'; entry.symlink_to(self.source)
        self.solve('--query', 'value:a', source=entry, answer='82')
        entry.unlink(); entry.symlink_to(other)
        self.solve('--query', 'value:a', source=entry, answer='99')

    def test_directory_membership_and_only_demanded_content(self):
        assets = self.root / 'assets'
        self.source.write_text('a=::assets;b=::unused.bin;')
        self.solve('--query', 'length:a', answer='0')
        assets.mkdir(); data = assets / 'a.bin'; data.write_bytes(b'A')
        self.solve('--query', 'length:a', answer='1')
        self.solve('--query', 'value:a')
        data.write_bytes(b'B')
        self.solve('--query', 'length:a', reused=True, answer='1')
        self.solve('--query', 'value:a')
        (assets / 'b.bin').write_bytes(b'C')
        self.solve('--query', 'length:a', answer='2')
        (assets / 'b.bin').unlink()
        self.solve('--query', 'length:a', answer='1')
        # A file's type does not read its contents. Creating it changes a path
        # selection once; subsequent byte edits leave its type group valid.
        self.solve('--query', 'type:b', answer='#s')
        unused = self.root / 'unused.bin'; unused.write_bytes(b'D')
        self.solve('--query', 'type:b', answer='#s')
        unused.write_bytes(b'EE')
        self.solve('--query', 'type:b', reused=True, answer='#s')

    def test_target_contracts_and_cached_artifacts(self):
        self.source.write_text('&~step(n:#u8){n+1}&^first(n:#u8){step(n)}&^second(n:#u8){step(n+1)}')
        args = ('--source', 'first', '--source', 'second', '--budget', '4')
        baseline = self.solve(*args)
        self.solve(*args, '--frame-ram-bytes', '128')
        self.solve(*args, '--mode', 'overlap')
        self.solve(*args, '--sink-no-feedback', 'first')
        self.solve(*args, '--budget', '0')
        self.solve(*args, reused=True)
        cached, fresh = self.root / 'cached', self.root / 'fresh'
        report = self.session.request('solve', self.source, *args, '--emit-c', cached)
        self.assertTrue(report['incremental']['reused'])
        built = subprocess.run([EXE, self.source, *args, '--emit-c', fresh], capture_output=True, text=True, timeout=60)
        self.assertEqual(built.returncode, 0, built.stderr)
        self.assertEqual({p.name: p.read_bytes() for p in cached.iterdir()},
                         {p.name: p.read_bytes() for p in fresh.iterdir()})
        self.assertEqual(json.loads((cached / 'source-plan.json').read_text())['stats'], baseline['stats'])
        (cached / 'vexel.h').write_text('user edit')
        report = self.session.request('solve', self.source, *args, '--emit-c', cached)
        self.assertEqual(report['status'], 'error')
        self.assertEqual((cached / 'vexel.h').read_text(), 'user edit')

    def test_unfinished_answers_and_budget_changes(self):
        for source, status in [('a=missing;', 'unresolved'),
                               ('a=?{x=?(#u8,0..2);x};', 'ambiguous'),
                               ('a=?{x=?(#u8,0..2);?(x==3);x};', 'contradictory')]:
            self.source.write_text(source)
            for _ in range(2):
                report = self.solve('--query', 'value:a')
                self.assertEqual(report['status'], status)
                self.assertEqual(report['incremental']['reason'], 'not-resolved')
        self.source.write_text('a=42;')
        self.solve('--query', 'value:a', answer='42')
        for _ in range(2):
            self.assertEqual(self.solve('--query', 'value:a', '--max-work', '0')['status'], 'exhausted')
        self.solve('--query', 'value:a', reused=True, answer='42')

    def test_process_requests_are_new_revisions_without_duplicate_verification(self):
        self.source.write_text('::"printf x >> calls; cat data" -> a;')
        data = self.root / 'data'; data.write_bytes(b'A')
        self.solve('--query', 'type:a', answer='#s')
        self.solve('--query', 'type:a', reused=True, answer='#s')
        self.assertFalse((self.root / 'calls').exists())
        for contents, expected in [(b'A', 'Bytes(65)'), (b'B', 'Bytes(66)'), (b'B', 'Bytes(66)')]:
            data.write_bytes(contents)
            report = self.session.request('solve', self.source, '--query', 'value:a', '--allow-processes')
            self.assertEqual(report['queries'][0]['answer'], expected)
            self.assertFalse(report['incremental']['reused'])
            self.assertEqual(report['incremental']['reason'], 'process-input')
        self.assertEqual((self.root / 'calls').read_bytes(), b'xxx')

    def test_protocol_clear_and_bounded_retention(self):
        self.source.write_text('a=42;')
        for text in ['unknown', 'solve', 'solve "unterminated', 'solve "" --query value:a']:
            self.assertEqual(self.session.line(text)['status'], 'error')
        for budget in range(33):
            result = self.session.request('solve', self.source, '--query', 'value:a', '--budget', budget)
            self.assertLessEqual(result['incremental']['cache_entries'], 32)
        self.solve('--query', 'value:a', '--budget', '32', reused=True, answer='42')
        self.solve('--query', 'value:a', '--budget', '0', answer='42')
        self.assertEqual(self.session.request('clear')['status'], 'cleared')
        self.solve('--query', 'value:a', '--budget', '32', answer='42')


if __name__ == '__main__':
    unittest.main(verbosity=2)
