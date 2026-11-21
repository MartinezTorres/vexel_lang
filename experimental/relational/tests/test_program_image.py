#!/usr/bin/env python3
"""One observation closure, multiple entries, and a lossless publication boundary."""
import ctypes
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import test_result_demand as support

spec = importlib.util.spec_from_file_location('program_image', Path(__file__).parents[1] / 'program_image.py')
images = importlib.util.module_from_spec(spec)
spec.loader.exec_module(images)


class ProgramImageTests(unittest.TestCase):
    project = support.ResultDemandTests.project
    execute = support.ResultDemandTests.execute

    def test_imported_exports_share_one_image_and_native_definition(self):
        for order in ((), ('--schedule', 'lifo', '--branch-order', 'reverse')):
            with self.subTest(order=order), tempfile.TemporaryDirectory() as tmp:
                d = Path(tmp)
                (d/'library.vx').write_text('&~shared(n:#u8)->#u8{n/7}'
                    '&^other(n:#u8){shared(n+1)}^asset=("bank",18446744073709551616);'
                    '&private_unused(){missing_dependency}')
                generated, report = self.project(d, '::library;&^entry(n:#u8){shared(n)}', '--program', '--mode', 'retained', *order)
                image = images.load(generated, report)
                self.assertEqual({r['name'] for r in image['roots']}, {'entry', 'other'})
                self.assertEqual(image['observations'], ['value:asset'])
                self.assertIn('18446744073709551616', json.dumps(report['queries'][0]['term']))
                native = [u for u in image['units'] if u['kind'] == 'native']
                self.assertEqual(len(native), 1)
                self.assertTrue(native[0].get('support'))
                self.assertEqual(native[0]['owners'], ['vx_entry', 'vx_other'])
                # Compile the emitted units as independent translation units.
                units = []
                fragments = [(image['support'], None), *[
                    (u['fragment'], u.get('support')) for u in image['units']]]
                for i, (name, private_support) in enumerate(fragments):
                    path = d/f'unit_{i}.c'
                    includes = '#include "vexel.h"\n'
                    if private_support:
                        includes += '#include "' + private_support + '"\n'
                    path.write_text(includes+(generated/name).read_text())
                    units.append(path)
                rt = support.support.ROOT/'backends/msx-neo8/runtime'
                library = d/'program.so'
                run = support.support.run(['cc', '-std=c11', '-O2', '-shared', '-fPIC', '-I'+str(generated), '-I'+str(rt),
                                          *units, rt/'vx_runtime.c', '-o', library])
                self.assertEqual(run.returncode, 0, run.stderr)
                dll = ctypes.CDLL(str(library))
                for name in ('entry', 'other'):
                    getattr(dll,'vx_'+name).argtypes = [ctypes.c_uint8]
                    getattr(dll,'vx_'+name+'_read').restype = ctypes.c_uint32
                for n in range(256):
                    dll.vx_entry(n);dll.vx_other(n)
                    self.assertEqual(dll.vx_entry_read(0), n//7)
                    self.assertEqual(dll.vx_other_read(0), ((n+1)&255)//7)
                fragment = generated/native[0]['fragment']
                fragment.write_text(fragment.read_text()+'/* changed */')
                with self.assertRaisesRegex(ValueError, 'changed after publication'):
                    images.load(generated)

    def test_whole_program_does_not_silently_omit_an_open_export(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp);path = d/'source.vx'
            path.write_text('&^entry(n:#u8){n+1}&^broken(n:#u8){unwritten(n)}')
            selective = support.support.run([support.support.EXE, path, '--source', 'entry'])
            self.assertEqual(selective.returncode, 0, selective.stderr)
            whole = support.support.run([support.support.EXE, path, '--program', '--emit-c', d/'generated'])
            self.assertNotEqual(whole.returncode, 0)
            self.assertFalse((d/'generated/program-image.json').exists())

    def test_boundary_units_and_named_occurrences(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            generated, report = self.project(d, '&!sink(n:#u8)->#u8;&^entry(n:#u8){sink(n)}', '--program')
            image = images.load(generated)
            boundary = next(u for u in image['units'] if u['kind']=='boundary')
            self.assertEqual(boundary['owners'], ['vx_entry'])
            self.assertNotIn('void vx_invoke_external_sink', (generated/image['support']).read_text())
            self.execute(d, generated, report, lambda n: n,
                         'void vx_external_sink(uint32_t *out,uint8_t n){out[0]=n;}\n')
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp);path = d/'source.vx';path.write_text('&^entry(n:#u8){n+1}')
            result = support.support.run([support.support.EXE, path, '--program', '--occurrence', 'left:entry',
                                          '--occurrence', 'right:entry', '--mode', 'graph', '--emit-c', d/'generated'])
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual({r['name'] for r in images.load(d/'generated')['roots']}, {'left','right'})


if __name__ == '__main__':
    unittest.main(verbosity=2)
