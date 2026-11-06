#!/usr/bin/env python3
"""Adapter symbol restrictions do not become Vexel identifier restrictions."""
import ctypes
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support

PREFIXES=('flow_','ensure_','load_','store_','invoke_external_','external_')
class BoundaryNameTests(unittest.TestCase):
    def test_generated_names_are_rejected_before_c_emission(self):
        for prefix in PREFIXES:
            with self.subTest(prefix=prefix),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);p=d/'source.vx';p.write_text('&^entry(n:#u8){n}');out=d/'generated'
                r=support.run([support.EXE,p,'--occurrence',prefix+'1:entry','--emit-c',out])
                self.assertEqual(r.returncode,2,r.stdout+r.stderr)
                self.assertIn('generated function namespace',r.stderr);self.assertIn('--occurrence',r.stderr)
                self.assertFalse(out.exists())

    def test_source_names_can_be_exported_under_another_occurrence(self):
        source=''.join('&^'+prefix+'1(n:#u8){n+1}' for prefix in PREFIXES)
        for schedule,branch in support.ORDERS:
            with self.subTest(schedule=schedule,branch=branch),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);p=d/'source.vx';p.write_text(source);out=d/'generated'
                args=[a for i,prefix in enumerate(PREFIXES) for a in ('--occurrence','entry'+str(i)+':'+prefix+'1')]
                r=support.run([support.EXE,p,*args,'--mode','retained','--budget','12','--emit-c',out,
                               '--schedule',schedule,'--branch-order',branch])
                self.assertEqual(r.returncode,0,r.stdout+r.stderr)
                self.assertEqual([s['source'] for s in json.loads(r.stdout)['sources']],[prefix+'1' for prefix in PREFIXES])
                lib=d/'test.so';rt=support.ROOT/'backends/msx-neo8/runtime'
                built=support.run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(out),'-I'+str(rt),out/'vx_bridges.c',rt/'vx_runtime.c','-o',lib])
                self.assertEqual(built.returncode,0,built.stderr);dll=ctypes.CDLL(str(lib))
                for i in range(len(PREFIXES)):
                    entry=getattr(dll,'vx_entry'+str(i));entry.argtypes=[ctypes.c_uint8]
                    read=getattr(dll,'vx_entry'+str(i)+'_read');read.argtypes=[ctypes.c_uint16];read.restype=ctypes.c_uint32
                    for n in range(256):entry(n);self.assertEqual(read(0),(n+1)&255)

    def test_runtime_and_reader_names_still_conflict(self):
        for args in (['--occurrence','payload:entry'],['--occurrence','one:entry','--occurrence','one_read:entry']):
            with self.subTest(args=args),tempfile.TemporaryDirectory() as tmp:
                p=Path(tmp)/'source.vx';p.write_text('&^entry(n:#u8){n}')
                r=support.run([support.EXE,p,*args]);self.assertEqual(r.returncode,2,r.stdout+r.stderr)
                self.assertIn('C name',r.stderr)

if __name__=='__main__':unittest.main(verbosity=2)
