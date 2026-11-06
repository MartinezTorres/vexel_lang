#!/usr/bin/env python3
"""Boundary occurrences separate execution ownership from retained results."""
import ctypes
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support

class OccurrenceTests(unittest.TestCase):
    def emit(self,directory,source,extra=(),names=('first','second'),schedule='fifo',branch='forward',status='resolved'):
        path=directory/'source.vx';path.write_text(source);out=directory/'generated'
        # A default is absent when the caller supplies a budget. Distinct
        # explicit requirements now conjoin; CLI order does not pick a winner.
        budget=[] if '--budget' in extra else ['--budget','8']
        args=[support.EXE,path,*[a for name in names for a in ('--occurrence',name+':entry')],
              '--mode','graph',*budget,'--schedule',schedule,'--branch-order',branch,*extra]
        if status=='resolved':args+=['--emit-c',out]
        r=support.run(args);self.assertIn(r.returncode,(0,1),r.stdout+r.stderr)
        report=json.loads(r.stdout);self.assertEqual(report['status'],status,r.stdout+r.stderr)
        return out,report

    def library(self,directory,out,bindings=''):
        bind=directory/'bindings.c';bind.write_text('#include "vexel.h"\n'+bindings)
        rt=support.ROOT/'backends/msx-neo8/runtime';lib=directory/'program.so'
        r=support.run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(out),'-I'+str(rt),out/'vx_bridges.c',bind,rt/'vx_runtime.c','-o',lib])
        self.assertEqual(r.returncode,0,r.stderr);dll=ctypes.CDLL(str(lib))
        for name in ('first','second','third'):
            if not hasattr(dll,'vx_'+name):continue
            getattr(dll,'vx_'+name).argtypes=[ctypes.c_uint8]
            getattr(dll,'vx_'+name+'_read').argtypes=[ctypes.c_uint16]
            getattr(dll,'vx_'+name+'_read').restype=ctypes.c_uint32
        return dll

    def test_retained_occurrences_use_distinct_results(self):
        source='&~advance(n:#u8)->#u8{n+1}&^entry(n:#u8){advance(n)}'
        for schedule,branch in support.ORDERS:
            with self.subTest(schedule=schedule,branch=branch),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);out,r=self.emit(d,source,schedule=schedule,branch=branch)
                self.assertEqual([s['source'] for s in r['sources']],['entry','entry'])
                self.assertNotEqual(r['sources'][0]['offset'],r['sources'][1]['offset'])
                self.assertFalse(r['activation_graph']['execution_exclusive'])
                dll=self.library(d,out)
                for n in range(256):
                    dll.vx_first(n);dll.vx_second(255-n)
                    self.assertEqual(dll.vx_first_read(0),(n+1)&255)
                    self.assertEqual(dll.vx_second_read(0),(-n)&255)

    def test_release_permits_reuse_but_access_order_retains_output(self):
        source='&~advance(n:#u8)->#u8{n+1}&^entry(n:#u8){advance(n)}'
        for release in (False,True):
            extra=['--frame-ram-bytes','64','--budget','2' if release else '4']
            extra+=['--event-order','first.release:second.begin'] if release else ['--access-order','first:second']
            for schedule,branch in support.ORDERS:
                with self.subTest(release=release,schedule=schedule,branch=branch),tempfile.TemporaryDirectory() as tmp:
                    d=Path(tmp);out,r=self.emit(d,source,extra,names=('second','first'),schedule=schedule,branch=branch)
                    self.assertEqual(r['sources'][0]['offset']==r['sources'][1]['offset'],release)
                    self.assertTrue(r['activation_graph']['execution_exclusive'])
                    self.assertGreater(r['frame_planning']['static_local_bytes'],0)
                    dll=self.library(d,out)
                    for n in range(256):
                        dll.vx_first(n)
                        if release:self.assertEqual(dll.vx_first_read(0),(n+1)&255)
                        dll.vx_second(255-n)
                        if not release:self.assertEqual(dll.vx_first_read(0),(n+1)&255)
                        self.assertEqual(dll.vx_second_read(0),(-n)&255)

    def test_partial_release_graph(self):
        source='&^entry(n:#u8){n}'
        for schedule,branch in support.ORDERS:
            with self.subTest(schedule=schedule,branch=branch),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);out,r=self.emit(d,source,['--event-order','first.release:third.begin','--budget','4'],
                                         names=('first','second','third'),schedule=schedule,branch=branch)
                offsets={s['name']:s['offset'] for s in r['sources']}
                self.assertEqual(offsets['first'],offsets['third']);self.assertNotEqual(offsets['first'],offsets['second'])
                dll=self.library(d,out)
                for n in range(256):
                    dll.vx_first(n);dll.vx_second(255-n)
                    self.assertEqual(dll.vx_first_read(0),n)
                    dll.vx_third(n^85)
                    self.assertEqual(dll.vx_second_read(0),255-n)
                    self.assertEqual(dll.vx_third_read(0),n^85)

    def test_persistent_writes_require_execution_order(self):
        source='counter:#u8;&^entry(n:#u8){counter=n;counter}'
        for ordered in (False,True):
            for schedule,branch in support.ORDERS:
                with self.subTest(ordered=ordered,schedule=schedule,branch=branch),tempfile.TemporaryDirectory() as tmp:
                    d=Path(tmp);out,r=self.emit(d,source,['--access-order','first:second'] if ordered else [],
                                             schedule=schedule,branch=branch,status='resolved' if ordered else 'unresolved')
                    if ordered:
                        dll=self.library(d,out)
                        for n in range(256):
                            dll.vx_first(n);dll.vx_second(255-n)
                            self.assertEqual(dll.vx_first_read(0),n);self.assertEqual(dll.vx_second_read(0),255-n)

    def test_callback_uses_another_occurrence_of_the_same_source(self):
        source='&!enter(k:#u8)->#u8;&~worker(n:#u8)->#u8{v=n+1;r=enter(n);v+r}&^entry(n:#u8){worker(n)}'
        extra=['--event-order','first.begin:second.begin','--event-order','second.produce:first.produce',
               '--resource-read','enter:clock','--frame-ram-bytes','64']
        bindings='void vx_external_enter(uint32_t *out,uint8_t k){if(k)vx_second(0);out[0]=(uint8_t)(k+3);}'
        for schedule,branch in support.ORDERS:
            with self.subTest(schedule=schedule,branch=branch),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);out,r=self.emit(d,source,extra,schedule=schedule,branch=branch)
                self.assertEqual(r['frame_planning']['static_local_bytes'],0)
                dll=self.library(d,out,bindings)
                for n in range(1,256):
                    dll.vx_first(n)
                    self.assertEqual(dll.vx_first_read(0),(2*n+4)&255)
                    self.assertEqual(dll.vx_second_read(0),4)

    def test_parameterless_occurrences_keep_distinct_effects(self):
        source='&!tick()->#u8;&^entry(){tick()}'
        for schedule,branch in support.ORDERS:
            with self.subTest(schedule=schedule,branch=branch),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);out,r=self.emit(d,source,['--access-order','first:second'],schedule=schedule,branch=branch)
                dll=self.library(d,out,'static unsigned count;void vx_external_tick(uint32_t *out){out[0]=(uint8_t)++count;}')
                dll.vx_first.argtypes=[];dll.vx_second.argtypes=[]
                for n in range(256):
                    dll.vx_first();dll.vx_second()
                    self.assertEqual(dll.vx_first_read(0),(2*n+1)&255)
                    self.assertEqual(dll.vx_second_read(0),(2*n+2)&255)

    def test_invalid_and_cyclic_occurrence_graphs(self):
        source='&^entry(n:#u8){n}'
        for schedule,branch in support.ORDERS:
            with tempfile.TemporaryDirectory() as tmp:
                self.emit(Path(tmp),source,['--event-order','first.produce:second.begin',
                          '--event-order','second.produce:first.begin'],schedule=schedule,branch=branch,status='contradictory')
        for args,message in (
            (['--occurrence','bad-name:entry'],'identifier'),
            (['--occurrence','first:entry','--occurrence','first:entry'],'duplicate'),
            (['--occurrence','first:entry','--event-order','first.end:first.release'],'unknown activation event'),
            (['--occurrence','first:missing'],'unknown or overloaded source root'),
        ):
            with self.subTest(args=args),tempfile.TemporaryDirectory() as tmp:
                path=Path(tmp)/'source.vx';path.write_text(source)
                r=support.run([support.EXE,path,'--mode','graph',*args]);self.assertEqual(r.returncode,2,r.stdout+r.stderr)
                self.assertIn(message,r.stderr)

if __name__=='__main__':unittest.main(verbosity=2)
