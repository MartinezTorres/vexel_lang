#!/usr/bin/env python3
"""Source publication keeps its solved wire width without changing evaluation."""
import ctypes
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support


class PublicationWidthsTests(unittest.TestCase):
    def test_snapshot_width_and_frame_budget(self):
        for bits in (16, 32):
            for budget in (0, bits // 8):
                with self.subTest(bits=bits, budget=budget), tempfile.TemporaryDirectory(prefix='vexel-publication-') as temp:
                    root = Path(temp)
                    source = root/'source.vx'
                    source.write_text('&^entry(n:#u'+str(bits)+'){n}')
                    generated = root/'generated'
                    result = support.run([support.EXE, source, '--program', '--frame-ram-bytes', budget,
                                          '--emit-c', generated])
                    self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                    report = json.loads(result.stdout)
                    self.assertEqual(report['sources'][0]['element_bits'], bits)
                    plan = report['frame_planning']
                    self.assertEqual(plan['static_local_bytes'], budget)
                    entry, = plan['functions']
                    self.assertEqual(entry['projected_local_bytes'], bits // 8)
                    self.assertEqual(entry['frame_policy'], 'static-exclusive' if budget else 'automatic')
                    code = (generated/'vx_bridges.c').read_text()
                    self.assertIn('uint'+str(bits)+'_t vxo_0', code)

    def test_signed_images_and_effects_are_evaluated_once(self):
        with tempfile.TemporaryDirectory(prefix='vexel-publication-effects-') as temp:
            root = Path(temp)
            source = root/'source.vx'
            source.write_text('&!take(n:#i16)->#i16;&^entry(n:#i16){a=take(n);(a,a+257,a-257)}')
            generated = root/'generated'
            result = support.run([support.EXE, source, '--program', '--emit-c', generated])
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            report = json.loads(result.stdout)
            self.assertEqual(report['sources'][0]['element_bits'], 16)
            # A native callback is an open edge; the smaller carrier must not
            # turn its surrounding activation into exclusive static storage.
            self.assertTrue(all(f['frame_policy'] == 'automatic' for f in report['frame_planning']['functions']))
            harness = root/'client.c'
            harness.write_text('''#include "vexel.h"
static unsigned calls;
void vx_external_take(uint32_t *out,int16_t n){++calls;out[0]=(uint32_t)(int32_t)n;}
unsigned invocation_count(void){return calls;}
''')
            runtime = support.ROOT/'backends/msx-neo8/runtime'
            library = root/'check.so'
            result = support.run(['cc', '-O2', '-std=c11', '-shared', '-fPIC',
                                  '-I'+str(generated), '-I'+str(runtime), harness,
                                  generated/'vx_bridges.c', runtime/'vx_runtime.c', '-o', library])
            self.assertEqual(result.returncode, 0, result.stderr)
            dll = ctypes.CDLL(str(library))
            dll.vx_entry.argtypes = [ctypes.c_int16]
            dll.vx_entry_read.argtypes = [ctypes.c_uint16]
            dll.vx_entry_read.restype = ctypes.c_uint32
            values = (-32768, -32767, -256, -1, 0, 1, 255, 256, 32767)
            for count, value in enumerate((*values, *reversed(values)), 1):
                dll.vx_entry(value)
                expected = [((v+32768) % 65536 - 32768) & 0xffffffff
                            for v in (value, value+257, value-257)]
                self.assertEqual([dll.vx_entry_read(i) for i in range(3)], expected)
                self.assertEqual(dll.invocation_count(), count)


if __name__ == '__main__':
    unittest.main(verbosity=2)
