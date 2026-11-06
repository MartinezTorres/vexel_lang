#!/usr/bin/env python3
"""A function's activation owners decide whether its locals may be shared."""
import ctypes
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support
import test_occurrences as occurrences

class OccurrenceFrameTests(unittest.TestCase):
    library=occurrences.OccurrenceTests.library
    def emit(self,directory,source,schedule,branch,targets=('entry','entry'),edges=()):
        path=directory/'source.vx';path.write_text(source);out=directory/'generated'
        r=support.run([support.EXE,path,'--occurrence','first:'+targets[0],'--occurrence','second:'+targets[1],
                       '--mode','graph','--frame-ram-bytes','1024','--budget','4',
                       *[arg for edge in edges for arg in ('--event-order',edge)],
                       '--schedule',schedule,'--branch-order',branch,'--emit-c',out])
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)
        return out,json.loads(r.stdout)

    def test_private_wrappers_and_shared_callee(self):
        source='&~sum(n:#u8)->#u16{total:#u16=0;[1,2]!@{total+=(#u16)n+_;};total}&^entry(n:#u8){sum(n)}'
        for ordered in (False,True):
            for schedule,branch in support.ORDERS:
                with self.subTest(ordered=ordered,schedule=schedule,branch=branch),tempfile.TemporaryDirectory() as tmp:
                    d=Path(tmp);out,r=self.emit(d,source,schedule,branch,edges=['first.produce:second.begin'] if ordered else [])
                    self.assertEqual(r['activation_graph']['execution_exclusive'],ordered)
                    functions=r['frame_planning']['functions'];shared=[f for f in functions if len(f['source_occurrences'])==2]
                    self.assertEqual(len(shared),1);self.assertGreater(shared[0]['projected_local_bytes'],0)
                    self.assertEqual(shared[0]['frame_policy'],'static-exclusive' if ordered else 'automatic')
                    self.assertEqual(shared[0]['overlapping_occurrences'],not ordered)
                    for name in ('vx_first','vx_second'):
                        node=next(f for f in functions if f['name']==name)
                        self.assertEqual(node['source_occurrences'],[name]);self.assertEqual(node['frame_policy'],'static-exclusive')
                    dll=self.library(d,out)
                    for n in range(256):
                        dll.vx_first(n);dll.vx_second(255-n)
                        self.assertEqual(dll.vx_first_read(0),2*n+3)
                        self.assertEqual(dll.vx_second_read(0),2*(255-n)+3)

    def test_disjoint_call_closures_can_share_their_own_locals(self):
        source='&~left(n:#u8)->#u16{s:#u16=0;[1,2]!@{s+=(#u16)n+_;};s}'
        source+='&~right(n:#u8)->#u16{s:#u16=0;[3,4]!@{s+=(#u16)n+_;};s}'
        source+='&^entry(n:#u8){left(n)}&^other(n:#u8){right(n)}'
        for schedule,branch in support.ORDERS:
            with self.subTest(schedule=schedule,branch=branch),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);out,r=self.emit(d,source,schedule,branch,targets=('entry','other'))
                self.assertFalse(r['activation_graph']['execution_exclusive'])
                for node in r['frame_planning']['functions']:
                    self.assertEqual(len(node['source_occurrences']),1)
                    if node['projected_local_bytes']:self.assertEqual(node['frame_policy'],'static-exclusive')
                dll=self.library(d,out)
                for n in range(256):
                    dll.vx_first(n);dll.vx_second(255-n)
                    self.assertEqual(dll.vx_first_read(0),2*n+3)
                    self.assertEqual(dll.vx_second_read(0),2*(255-n)+7)

    def test_common_descendant_and_distinct_ancestors(self):
        source='&~common(n:#u8)->#u16{s:#u16=0;[1,2]!@{s+=(#u16)n+_;};s}'
        source+='&~left(n:#u8)->#u16{common(n)+1}&~right(n:#u8)->#u16{common(n)+2}'
        source+='&^entry(n:#u8){left(n)}&^other(n:#u8){right(n)}'
        for schedule,branch in support.ORDERS:
            with self.subTest(schedule=schedule,branch=branch),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);out,r=self.emit(d,source,schedule,branch,targets=('entry','other'))
                # Reuse may keep a common helper or specialize it for each
                # enclosing interface. Check lifetime safety in either plan;
                # the following execution oracle checks both call closures.
                for node in r['frame_planning']['functions']:
                    if len(node['source_occurrences'])==2:
                        self.assertEqual(node['frame_policy'],'automatic')
                    if len(node['source_occurrences'])==1 and node['projected_local_bytes']:
                        self.assertEqual(node['frame_policy'],'static-exclusive')
                dll=self.library(d,out)
                for n in range(256):
                    dll.vx_first(n);dll.vx_second(255-n)
                    self.assertEqual(dll.vx_first_read(0),2*n+4)
                    self.assertEqual(dll.vx_second_read(0),2*(255-n)+5)

if __name__=='__main__':unittest.main(verbosity=2)
