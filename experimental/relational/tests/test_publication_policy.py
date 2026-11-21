#!/usr/bin/env python3
"""Publication storage choices preserve values and obey scoped allowances."""
import ctypes
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support


class PublicationPolicyTests(unittest.TestCase):
    def test_global_and_function_choices(self):
        choices = [([], (True, True)),
                   (['calculation_publication_reuse=0'], (False, False)),
                   (['calculation_publication_reuse.vx_first=0'], (False, True)),
                   (['calculation_publication_reuse=0', 'calculation_publication_reuse.vx_first=1'], (False, False))]
        for preferences, reuse in choices:
            for budget in (0, 16):
                with self.subTest(preferences=preferences, budget=budget), tempfile.TemporaryDirectory(prefix='vexel-publication-policy-') as temp:
                    root = Path(temp)
                    source = root/'source.vx'
                    source.write_text('&^first(n:#u16){(n,n+1,n-1)}&^second(n:#u16){(n,n+2,n-2)}')
                    generated = root/'generated'
                    result = support.run([support.EXE, source, '--program', '--frame-ram-bytes', budget,
                                          *[x for p in preferences for x in ('--prefer', p)], '--emit-c', generated])
                    self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                    plan = json.loads(result.stdout)['frame_planning']
                    for function, enabled in zip(plan['functions'], reuse):
                        self.assertEqual(function['publication_reuse'], enabled)
                        self.assertEqual(function['publication_values'], 3)
                        self.assertEqual(function['publication_snapshot_bytes'], 2 if enabled else 6)
                        self.assertEqual(function['publication_policy'], 'calculation_publication_reuse.'+function['name'])
                    self.assertEqual(plan['static_local_bytes'], sum(2 if v else 6 for v in reuse) if budget else 0)
                    runtime = support.ROOT/'backends/msx-neo8/runtime'
                    library = root/'check.so'
                    result = support.run(['cc', '-O2', '-std=c11', '-shared', '-fPIC', '-I'+str(generated),
                                          '-I'+str(runtime), generated/'vx_bridges.c', runtime/'vx_runtime.c', '-o', library])
                    self.assertEqual(result.returncode, 0, result.stderr)
                    dll = ctypes.CDLL(str(library))
                    for name, delta in (('first', 1), ('second', 2)):
                        entry, read = getattr(dll, 'vx_'+name), getattr(dll, 'vx_'+name+'_read')
                        entry.argtypes = [ctypes.c_uint16]
                        read.argtypes = [ctypes.c_uint16]
                        read.restype = ctypes.c_uint32
                        for n in (0, 1, 255, 256, 32767, 32768, 65534, 65535, 0):
                            entry(n)
                            self.assertEqual([read(i) for i in range(3)], [n, (n+delta) & 65535, (n-delta) & 65535])

    def test_unknown_scope_is_not_silently_ignored(self):
        with tempfile.TemporaryDirectory(prefix='vexel-publication-scope-') as temp:
            source = Path(temp)/'source.vx'
            source.write_text('&^entry(n:#u16){n}')
            result = support.run([support.EXE, source, '--program', '--prefer', 'calculation_publication_reuse.missing=0'])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('unknown publication reuse function scope', result.stdout+result.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
