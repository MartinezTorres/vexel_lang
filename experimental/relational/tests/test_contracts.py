#!/usr/bin/env python3
"""Boundary resource assumptions, execution order and aggregate sink interfaces."""
import ctypes
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support

class ContractTests(unittest.TestCase):
    def check(self,source,args,expected='resolved',sources=('first','second')):
        for schedule,branch in support.ORDERS:
            with self.subTest(source=source,args=args,schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-contract-') as temp:
                path=Path(temp)/'source.vx';path.write_text(source)
                r=support.run([support.EXE,path,*[a for s in sources for a in ['--source',s]],'--mode','overlap','--budget','8',
                               '--schedule',schedule,'--branch-order',branch,*args])
                report=json.loads(r.stdout);self.assertEqual(report['status'],expected,r.stdout+r.stderr)
                if expected=='resolved':
                    self.assertEqual(len(report['sources']),len(sources));self.assertGreaterEqual(report['extent'],len(sources))

    def test_resource_aliases_and_unknown_footprints(self):
        source='&!get()->#u8;&!put(n:#u8);&^first(n:#u8){get()+n}&^second(n:#u8){put(n);n}'
        self.check(source,[],'unresolved')
        self.check(source,['--resource-read','get:screen','--resource-write','put:sound'])
        self.check(source,['--resource-read','get:screen','--resource-write','put:screen'],'unresolved')
        self.check(source,['--resource-read','get:screen','--resource-read','put:screen'])
        self.check(source,['--resource-read','get:screen'],'unresolved')
        self.check(source,['--sink-no-feedback','get','--sink-no-feedback','put'],'unresolved')
        transitive='&!get()->#u8;&!put(n:#u8);&~read(n:#u8)->#u8{get()+n}&~write(n:#u8){put(n);}'+\
                   '&^first(n:#u8){read(n)}&^second(n:#u8){write(n);n}'
        self.check(transitive,['--resource-read','get:bus','--resource-write','put:bus'],'unresolved')

    def test_access_order_is_separate_from_output_release(self):
        source='&!get()->#u8;&!put(n:#u8);&^first(n:#u8){get()+n}&^second(n:#u8){put(n);n}'
        for edge in ('first:second','second:first'):
            self.check(source,['--access-order',edge])
        self.check(source,['--access-order','first:second','--access-order','second:first'],'contradictory')
        source='state:#u8;&^first(n:#u8){state=n;n}&^middle(n:#u8){n}&^second(n:#u8){state+n}'
        self.check(source,['--access-order','first:middle','--access-order','middle:second'],sources=('first','middle','second'))
        self.check(source,['--access-order','first:middle'],'unresolved',sources=('first','middle','second'))

    def test_no_feedback_closes_activation_but_never_purity(self):
        source='&!put(n:#u8);&~send(n:#u8)->#u8{put(n);n+1}&^entry(n:#u8){send(n)}'
        for closed in (False,True):
            for schedule,branch in support.ORDERS:
                with self.subTest(closed=closed,schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-sink-frame-') as temp:
                    d=Path(temp);path=d/'source.vx';path.write_text(source);out=d/'generated'
                    facts=['--sink-no-feedback','put'] if closed else []
                    r=support.run([support.EXE,path,'--source','entry','--frame-ram-bytes','64','--emit-c',out,
                        '--schedule',schedule,'--branch-order',branch,*facts])
                    self.assertEqual(r.returncode,0,r.stdout+r.stderr);report=json.loads(r.stdout)
                    self.assertEqual(report['frame_planning']['static_local_bytes']>0,closed)
                    self.assertEqual(report['source_accesses']['entry'],[{'resource':'external-world','write':True}])
                    bindings=d/'bindings.c';bindings.write_text('#include "vexel.h"\nunsigned count,stored;void vx_external_put(uint32_t *out,uint8_t n){(void)out;++count;stored=n;}\n')
                    rt=support.ROOT/'backends/msx-neo8/runtime';lib=d/'run.so'
                    built=support.run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(out),'-I'+str(rt),out/'vx_bridges.c',bindings,rt/'vx_runtime.c','-o',lib])
                    self.assertEqual(built.returncode,0,built.stderr);dll=ctypes.CDLL(str(lib))
                    dll.vx_entry.argtypes=[ctypes.c_uint8];dll.vx_entry_read.argtypes=[ctypes.c_uint16];dll.vx_entry_read.restype=ctypes.c_uint32
                    for n in range(256):
                        dll.vx_entry(n);self.assertEqual(dll.vx_entry_read(0),(n+1)%256)
                        self.assertEqual(ctypes.c_uint.in_dll(dll,'stored').value,n)
                        self.assertEqual(ctypes.c_uint.in_dll(dll,'count').value,n+1)
        with tempfile.TemporaryDirectory(prefix='vexel-sink-pure-') as temp:
            path=Path(temp)/'source.vx';path.write_text('&!get()->#u8;a=?{get();7};')
            r=support.run([support.EXE,path,'--query','value:a','--sink-no-feedback','get','--resource-read','get:clock'])
            self.assertEqual(json.loads(r.stdout)['status'],'contradictory',r.stdout+r.stderr)
            r=support.run([support.EXE,path,'--query','type:a','--sink-no-feedback','missing'])
            self.assertEqual(r.returncode,2);self.assertIn('external function declaration',r.stderr)

    def test_nested_and_empty_external_arrays(self):
        source='#P(n:#u8,items:#[((#u8)0,(#i16)0)][2]);&!exchange(a:#u8[2],p:#P,empty:#u8[0])->(#u8[2],#i16[2]);'
        source+='&^entry(n:#u8){exchange([n,n+1],#P(n,[(n,(#i16)n-128),(n+1,(#i16)n+300)]),(#u8[0])[])}'
        bindings='''void vx_external_exchange(uint32_t *out,uint8_t a,uint8_t b,uint8_t count,uint8_t x,int16_t y,uint8_t z,int16_t w){
 out[0]=(uint8_t)(a+x);out[1]=(uint8_t)(b+z+count);out[2]=(uint32_t)(int32_t)y;out[3]=(uint32_t)(int32_t)w;}'''
        for schedule,branch in support.ORDERS:
            with self.subTest(schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-sink-aggregate-') as temp:
                d=Path(temp);path=d/'source.vx';path.write_text(source);out=d/'generated'
                r=support.run([support.EXE,path,'--source','entry','--budget','8','--emit-c',out,'--schedule',schedule,'--branch-order',branch])
                self.assertEqual(r.returncode,0,r.stdout+r.stderr)
                bind=d/'bindings.c';bind.write_text('#include "vexel.h"\n'+bindings)
                rt=support.ROOT/'backends/msx-neo8/runtime';lib=d/'run.so'
                built=support.run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(out),'-I'+str(rt),out/'vx_bridges.c',bind,rt/'vx_runtime.c','-o',lib])
                self.assertEqual(built.returncode,0,built.stderr);dll=ctypes.CDLL(str(lib))
                dll.vx_entry.argtypes=[ctypes.c_uint8];dll.vx_entry_read.argtypes=[ctypes.c_uint16];dll.vx_entry_read.restype=ctypes.c_uint32
                for n in range(256):
                    dll.vx_entry(n);self.assertEqual([dll.vx_entry_read(i) for i in range(4)],[(2*n)%256,(3*n+2)%256,(n-128)&0xffffffff,n+300])

if __name__=='__main__':unittest.main(verbosity=2)
