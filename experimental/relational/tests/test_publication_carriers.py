#!/usr/bin/env python3
"""Wide/narrow publication carriers preserve signed images and storage facts."""
import ctypes
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support


class PublicationCarrierTests(unittest.TestCase):
    def test_carriers_and_lifetimes_are_independent_choices(self):
        choices = [([], 16), (['calculation_publication_narrow.vx_entry=0'], 32),
                   (['calculation_publication_narrow=0', 'calculation_publication_narrow.vx_entry=1'], 32)]
        for preferences, bits in choices:
            for reuse in (0, 1):
                for budget in (0, 64):
                    with self.subTest(preferences=preferences, reuse=reuse, budget=budget), tempfile.TemporaryDirectory(prefix='vexel-carrier-') as temp:
                        root = Path(temp)
                        source = root/'source.vx'
                        source.write_text('&^entry(n:#i16){(n,n+257,n-257)}')
                        generated = root/'generated'
                        result = support.run([support.EXE, source, '--program', '--frame-ram-bytes', budget,
                                              '--prefer', 'calculation_publication_reuse='+str(reuse),
                                              *[x for p in preferences for x in ('--prefer', p)], '--emit-c', generated])
                        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                        report = json.loads(result.stdout)
                        self.assertEqual(report['sources'][0]['element_bits'], 16)
                        entry, = report['frame_planning']['functions']
                        expected_bytes = bits//8*(1 if reuse else 3)
                        self.assertEqual(entry['publication_carrier_bits'], bits)
                        self.assertEqual(entry['publication_snapshot_bytes'], expected_bytes)
                        self.assertEqual(entry['projected_local_bytes'], expected_bytes)
                        self.assertEqual(entry['publication_width_policy'], 'calculation_publication_narrow.vx_entry')
                        self.assertEqual(report['frame_planning']['static_local_bytes'], expected_bytes if budget else 0)
                        runtime = support.ROOT/'backends/msx-neo8/runtime'
                        library = root/'check.so'
                        result = support.run(['cc', '-O2', '-std=c11', '-shared', '-fPIC', '-I'+str(generated),
                                              '-I'+str(runtime), generated/'vx_bridges.c', runtime/'vx_runtime.c', '-o', library])
                        self.assertEqual(result.returncode, 0, result.stderr)
                        dll = ctypes.CDLL(str(library))
                        dll.vx_entry.argtypes = [ctypes.c_int16]
                        dll.vx_entry_read.argtypes = [ctypes.c_uint16]
                        dll.vx_entry_read.restype = ctypes.c_uint32
                        for n in (-32768, -32767, -257, -1, 0, 1, 257, 32766, 32767, -32768):
                            dll.vx_entry(n)
                            expected = [((v+32768) % 65536-32768) & 0xffffffff for v in (n, n+257, n-257)]
                            self.assertEqual([dll.vx_entry_read(i) for i in range(3)], expected)

    def test_byte_payload_needs_no_carrier_and_unknown_scope_fails(self):
        with tempfile.TemporaryDirectory(prefix='vexel-byte-publication-') as temp:
            source = Path(temp)/'source.vx'
            source.write_text('&^entry(n:#u8){(n,n+1,n-1)}')
            result = support.run([support.EXE, source, '--program', '--budget', 3,
                                  '--prefer', 'calculation_publication_narrow=0'])
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            report = json.loads(result.stdout)
            self.assertEqual(report['sources'][0]['element_bits'], 8)
            entry, = report['frame_planning']['functions']
            self.assertEqual((entry['publication_carrier_bits'], entry['publication_snapshot_bytes']), (0, 0))
            result = support.run([support.EXE, source, '--program', '--prefer', 'calculation_publication_narrow.missing=0'])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('unknown publication width function scope', result.stdout+result.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
