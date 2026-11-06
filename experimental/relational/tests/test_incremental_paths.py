#!/usr/bin/env python3
"""Absent paths still depend on how their parent directories resolve."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_incremental import EXE, Session


class IncrementalPathTests(unittest.TestCase):
    def check(self, source_text, query, expected):
        with tempfile.TemporaryDirectory(prefix='vexel-parent-links-') as tmp:
            root = Path(tmp)
            first, second = root / 'first', root / 'second'
            first.mkdir(); second.mkdir()
            link = root / 'linked'; link.symlink_to(first)
            source = root / 'source.vx'; source.write_text(source_text)
            session = Session()
            try:
                for stage in range(3):
                    result = session.request('solve', source, '--query', query)
                    self.assertEqual(result['status'], 'resolved', result)
                    incremental = result.pop('incremental')
                    self.assertEqual(incremental['reused'], stage == 2)
                    self.assertEqual(result['queries'][0]['answer'], expected)
                    fresh = subprocess.run([EXE, source, '--query', query], text=True,
                                           capture_output=True, timeout=60)
                    self.assertEqual(fresh.returncode, 0, fresh.stderr)
                    self.assertEqual(result, json.loads(fresh.stdout))
                    if stage == 0:
                        link.unlink(); link.symlink_to(second)
            finally:
                session.close()

    def test_absent_resource_parent_destination(self):
        self.check('a=::linked::absent;', 'length:a', '0')

    def test_absent_unused_import_parent_destination(self):
        self.check('&unused(){::linked::absent;0}a=7;', 'value:a', '7')


if __name__ == '__main__':
    unittest.main(verbosity=2)
