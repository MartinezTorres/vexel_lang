#!/usr/bin/env python3
"""Storage selection follows actual recursive and boundary activation paths."""
import ctypes
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support

PROGRAM='''&~leaf(n:#u8)->#u16 {total:#u16=0;i:#u8=0;(i<3)!@{total+=(#u16)n;i+=1;};total}
&~sum(n:#u8)->#u16 {n==0 ? (#u16)0 : leaf(n)+sum(n-1)}
&^entry(n:#u8){sum(n%8)+leaf(n)}'''

class FramePlanTests(unittest.TestCase):
    def test_recursive_ancestor_and_exclusive_descendant(self):
        for schedule,branch in support.ORDERS:
            for budget in (0,1024):
                with self.subTest(schedule=schedule,branch=branch,budget=budget),tempfile.TemporaryDirectory(prefix='vexel-frame-plan-') as temp:
                    d=Path(temp);path=d/'source.vx';generated=d/'generated';path.write_text(PROGRAM)
                    r=support.run([support.EXE,path,'--source','entry','--budget','2','--frame-ram-bytes',budget,
                                   '--schedule',schedule,'--branch-order',branch,'--emit-c',generated])
                    self.assertEqual(r.returncode,0,r.stdout+r.stderr);report=json.loads(r.stdout);plan=report['frame_planning']
                    self.assertLessEqual(plan['static_local_bytes'],budget);self.assertIsNone(plan['stack_bound'])
                    functions=plan['functions'];recursive=[fn for fn in functions if fn['recursive']]
                    self.assertEqual(len(recursive),1);self.assertEqual(recursive[0]['frame_policy'],'automatic')
                    if budget:
                        self.assertGreater(plan['static_local_bytes'],0)
                        self.assertTrue(any(fn['name'].startswith('vx_flow_') and fn['frame_policy']=='static-exclusive' for fn in functions))
                    else:self.assertTrue(all(fn['frame_policy']=='automatic' for fn in functions))
                    rt=support.ROOT/'backends/msx-neo8/runtime'
                    for split in (False,True):
                        files=[generated/'vx_bridges.c'] if not split else [generated/'vx_banked.c',*[generated/(m['module']+'.c') for m in report['bank_modules']]]
                        library=d/('split.so' if split else 'whole.so')
                        built=support.run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(generated),'-I'+str(rt),*files,rt/'vx_runtime.c','-o',library])
                        self.assertEqual(built.returncode,0,built.stderr);dll=ctypes.CDLL(str(library))
                        dll.vx_entry.argtypes=[ctypes.c_uint8];dll.vx_entry_read.argtypes=[ctypes.c_uint16];dll.vx_entry_read.restype=ctypes.c_uint32
                        for n in [*range(256),*range(255,-1,-1)]:
                            dll.vx_entry(n);self.assertEqual(dll.vx_entry_read(0),3*((n%8)*(n%8+1)//2+n),n)

    def test_open_edges_overlap_and_ram_budget(self):
        sources=[
            ('&^entry(n:#u8){n+1}',1,'serial',0),
            ('&^entry(n:#u8){n+1}',4,'serial',4),
            ('&^entry(n:#u8){n+1}',4,'retained',4),
            ('&^entry(n:#u8){n+1}',1024,'overlap',0),
            ('&^entry(n:#u8){n+1}',1024,'open',0),
            ('&!read(n:#u8)->#u8;&~wrap(n:#u8)->#u8{read(n)}&^entry(n:#u8){wrap(n)}',1024,'serial',0),
            ('counter:#u8;&~bump(n:#u8){counter+=n;counter}&^entry(n:#u8){counter=1;bump(n)}',1024,'serial',0),
            ('&~a(n:#u8)->#u8{n==0?(#u8)0:b(n-1)}&~b(n:#u8)->#u8{n==0?(#u8)0:a(n-1)}&^entry(n:#u8){a(n)}',1024,'serial',None),
        ]
        for source,budget,mode,size in sources:
            with self.subTest(source=source,mode=mode),tempfile.TemporaryDirectory(prefix='vexel-frame-facts-') as temp:
                path=Path(temp)/'source.vx';path.write_text(source)
                r=support.run([support.EXE,path,'--source','entry','--mode',mode,'--frame-ram-bytes',budget])
                if mode=='open':
                    self.assertEqual(json.loads(r.stdout)['status'],'unresolved',r.stdout+r.stderr)
                    continue
                self.assertEqual(r.returncode,0,r.stdout+r.stderr);plan=json.loads(r.stdout)['frame_planning']
                if size is not None:self.assertEqual(plan['static_local_bytes'],size)
                else:
                    recursive=[fn for fn in plan['functions'] if fn['recursive']]
                    self.assertEqual(len(recursive),2);self.assertTrue(all(fn['frame_policy']=='automatic' for fn in recursive))
                if 'read(' in source or 'counter:' in source:self.assertTrue(all(fn['open_boundary'] for fn in plan['functions']))

if __name__=='__main__':unittest.main(verbosity=2)
