#!/usr/bin/env python3
"""Lazy initializer calls must participate in activation ownership proofs."""
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support
import test_occurrences as occurrences

class InitializerFrameTests(unittest.TestCase):
    library=occurrences.OccurrenceTests.library
    def emit(self,directory,source,schedule,branch):
        path=directory/'source.vx';path.write_text(source);out=directory/'generated'
        r=support.run([support.EXE,path,'--source','first','--source','second','--mode','graph',
                       '--frame-ram-bytes','1024','--budget','4','--resource-read','initial:clock',
                       '--sink-no-feedback','initial','--schedule',schedule,'--branch-order',branch,'--emit-c',out])
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)
        return out,json.loads(r.stdout)

    def test_read_and_first_store_share_initializer_callees(self):
        prefix='&!initial()->#u8;&~sum(n:#u8)->#u16{total:#u16=0;[1,2]!@{total+=(#u16)n+_;};total}'
        prefix+='&~outer(n:#u8)->#u16{sum(n)+1}counter:#u16=outer(initial());'
        for store in (False,True):
            # Enter the same outer interface from both activations. Calling
            # sum directly from one root may legitimately specialize it for
            # a different enclosing interface, removing physical sharing.
            source=prefix+'&^first(n:#u8){'+('counter=n;counter' if store else 'counter+(#u16)n')+'}&^second(n:#u8){outer(n)}'
            for schedule,branch in support.ORDERS:
                with self.subTest(store=store,schedule=schedule,branch=branch),tempfile.TemporaryDirectory() as tmp:
                    d=Path(tmp);out,r=self.emit(d,source,schedule,branch);functions=r['frame_planning']['functions']
                    first=next(f for f in functions if f['name']=='vx_first')
                    self.assertTrue(any(name.startswith('vx_ensure_') for name in first['calls']))
                    ensure=[f for f in functions if f['name'].startswith('vx_ensure_')]
                    self.assertEqual(len(ensure),1);self.assertEqual(ensure[0]['source_occurrences'],['vx_first'])
                    self.assertEqual(ensure[0]['frame_policy'],'automatic')
                    shared=[f for f in functions if f['source_occurrences']==['vx_first','vx_second'] and f['projected_local_bytes']]
                    self.assertTrue(shared,'the fixture must exercise physically shared locals')
                    for function in shared:
                        self.assertTrue(function['overlapping_occurrences']);self.assertEqual(function['frame_policy'],'automatic')
                    dll=self.library(d,out,'void vx_external_initial(uint32_t *out){out[0]=7;}')
                    for n in range(256):
                        dll.vx_first(n);dll.vx_second(255-n)
                        self.assertEqual(dll.vx_first_read(0),n if store else n+18)
                        self.assertEqual(dll.vx_second_read(0),2*(255-n)+4)

    def test_initializer_cycles_are_visible_and_keep_runtime_traps(self):
        source='&!initial()->#u8;a:#u8=b+initial();b:#u8=a;&^first(n:#u8){a}&^second(n:#u8){n}'
        for schedule,branch in support.ORDERS:
            with self.subTest(schedule=schedule,branch=branch),tempfile.TemporaryDirectory() as tmp:
                d=Path(tmp);out,r=self.emit(d,source,schedule,branch)
                ensure=[f for f in r['frame_planning']['functions'] if f['name'].startswith('vx_ensure_')]
                self.assertEqual(len(ensure),2)
                self.assertTrue(all(f['recursive'] and f['frame_policy']=='automatic' for f in ensure))
                harness=d/'main.c';harness.write_text('#include "vexel.h"\n#include <stdlib.h>\nvoid vx_trap(uint8_t reason){exit(reason);}\n'
                    'void vx_external_initial(uint32_t *out){out[0]=1;}\nint main(void){vx_first(0);return 0;}\n')
                exe=d/'test';rt=support.ROOT/'backends/msx-neo8/runtime'
                built=support.run(['cc','-std=c11','-O2','-I'+str(out),'-I'+str(rt),out/'vx_bridges.c',harness,'-o',exe])
                self.assertEqual(built.returncode,0,built.stderr);self.assertEqual(support.run([exe]).returncode,18)

if __name__=='__main__':unittest.main(verbosity=2)
