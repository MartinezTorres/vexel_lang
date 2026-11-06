#!/usr/bin/env python3
"""Stateful boundary execution with independent host observations."""
import ctypes
import itertools
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
EXE=Path(sys.argv.pop(1)).resolve()
ROOT=Path(__file__).resolve().parents[3]
ORDERS=list(itertools.product(('fifo','lifo','random'),('forward','reverse')))
def run(args):return subprocess.run([str(a) for a in args],capture_output=True,text=True,timeout=120)
class BoundaryTests(unittest.TestCase):
    def emit(self,temp,source,schedule='fifo',branch='forward',budget=8):
        source_path=temp/'source.vx';source_path.write_text(source);out=temp/'generated'
        r=run([EXE,source_path,'--source','entry','--budget',budget,'--emit-c',out,'--schedule',schedule,'--branch-order',branch])
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)
        return out,json.loads(r.stdout)
    def library(self,temp,out,bindings=''):
        path=temp/'bindings.c';path.write_text('#include "vexel.h"\n'+bindings)
        lib=temp/'execution.so';r=run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(out),'-I'+str(ROOT/'backends/msx-neo8/runtime'),out/'vx_bridges.c',path,ROOT/'backends/msx-neo8/runtime/vx_runtime.c','-o',lib])
        self.assertEqual(r.returncode,0,r.stderr)
        dll=ctypes.CDLL(str(lib));dll.vx_entry.argtypes=[ctypes.c_uint8];dll.vx_entry_read.argtypes=[ctypes.c_uint16];dll.vx_entry_read.restype=ctypes.c_uint32
        return dll
    def test_fresh_reads_calls_and_persistent_activations(self):
        cases=[
            ('counter:#u8;&^entry(n:#u8){counter=n;counter+=1;counter}', '', lambda n,total:(n+1)%256),
            ('&!get()->#u8;counter:#u8=get();&~bump(n:#u8)->#u8{counter+=n;counter}&^entry(n:#u8){bump(n)}', 'void vx_external_get(uint32_t *out){out[0]=7;}', lambda n,total:(7+total)%256),
            ('&size(){x:#u8=0;[1,2,3]!@{x+=1;};x}counter:#u8=size();&^entry(n:#u8){counter+=n;counter}', '', lambda n,total:(3+total)%256),
            ('&!get()->#u8;&twice($x){a:#u8=$x;$x+a}&^entry(n:#u8){twice(get())}', 'static unsigned count;void vx_external_get(uint32_t *out){out[0]=++count%256;}', lambda n,total:(4*n+3)%256),
            ('counter:#u16;&^entry(n:#u8){counter=0;i:#u8=0;(i<n)!@{counter+=1;i+=1;};counter}', '',lambda n,total:n),
            ('[[msx_address(0xC100)]]!!device:#u8;&^entry(n:#u8){device=n;device+device}', '',lambda n,total:(2*n)%256),
            ('&^entry(n:#u8){[[msx_port(0x98)]]!!device:#u8;device=n;device}', '',lambda n,total:n),
        ]
        for source,bindings,oracle in cases:
            for schedule,branch in ORDERS:
                with self.subTest(source=source,schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-boundary-') as temp:
                    temp=Path(temp);out,_=self.emit(temp,source,schedule,branch,budget=2);dll=self.library(temp,out,bindings);total=0
                    for n in range(256):
                        total+=n;dll.vx_entry(n);self.assertEqual(dll.vx_entry_read(0),oracle(n,total),n)
    def test_lazy_order_unused_initializers_and_void_sinks(self):
        source='&!get(tag:#u8)->#u8;&!put(n:#u8);counter:#u8=get(1);unused:#u8=get(9);&^entry(n:#u8){counter=get(2);put(counter);}'
        for schedule,branch in ORDERS:
            with self.subTest(schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-boundary-order-') as temp:
                temp=Path(temp);out,report=self.emit(temp,source,schedule,branch,budget=0)
                self.assertEqual(report['extent'],0)
                dll=self.library(temp,out,'unsigned log_value,stored;void vx_external_get(uint32_t *out,uint8_t tag){log_value=log_value*10+tag;out[0]=tag*10;}void vx_external_put(uint32_t *out,uint8_t n){(void)out;stored=n;}')
                dll.vx_entry(0);self.assertEqual(ctypes.c_uint.in_dll(dll,'log_value').value,21);self.assertEqual(ctypes.c_uint.in_dll(dll,'stored').value,20)
                dll.vx_entry(0);self.assertEqual(ctypes.c_uint.in_dll(dll,'log_value').value,212)
    def test_aggregate_places_preserve_rhs_updates(self):
        source='&!get(tag:#u8)->#u8;a:#u8[2]=[get(1),get(2)];&change(){a[0]=10;a[1]=20;3}&^entry(n:#u8){a[0]+=change();a}'
        for schedule,branch in ORDERS:
            with self.subTest(schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-boundary-aggregate-') as temp:
                temp=Path(temp);out,_=self.emit(temp,source,schedule,branch,budget=4);dll=self.library(temp,out,'void vx_external_get(uint32_t *out,uint8_t tag){out[0]=tag;}')
                for expected in (4,7,10):
                    dll.vx_entry(0);self.assertEqual([dll.vx_entry_read(i) for i in range(2)],[expected,20])
    def test_activation_resources_and_binding_obligations(self):
        cases=[
            ('x:#u8;&~put(n:#u8){x=n;} &^first(n:#u8){put(n);n}&^second(n:#u8){x+n}', 'unresolved'),
            ('&!get()->#u8;x:#u8=get();&^first(n:#u8){x+n}&^second(n:#u8){x+n}', 'unresolved'),
            ('&!get()->#u8;&^first(n:#u8){get()+n}&^second(n:#u8){get()+n}', 'unresolved'),
            ('x:#u8;y:#u8;&^first(n:#u8){x=n;x}&^second(n:#u8){y=n;y}', 'resolved'),
        ]
        for source,expected in cases:
            for mode in ('serial','retained','overlap'):
                with self.subTest(source=source,mode=mode),tempfile.TemporaryDirectory(prefix='vexel-boundary-access-') as temp:
                    path=Path(temp)/'source.vx';path.write_text(source)
                    r=run([EXE,path,'--source','first','--source','second','--mode',mode,'--budget','4'])
                    report=json.loads(r.stdout);self.assertEqual(report['status'],expected if mode=='overlap' else 'resolved',r.stdout+r.stderr)
                    if report['status']=='resolved':self.assertTrue(report['source_accesses']['first'])
        for binding in ('!!x:#u8;', '[[msx_port(256)]]!!x:#u8;', '[[msx_port(1)]]!!x:#u16;', '[[msx_address(65535)]]!!x:#u16;', '[[msx_address(65533)]]!!x:#u24;', '[[msx_port(1)]][[msx_port(2)]]!!x:#u8;'):
            with self.subTest(binding=binding),tempfile.TemporaryDirectory(prefix='vexel-boundary-binding-') as temp:
                path=Path(temp)/'source.vx';path.write_text(binding+'&^entry(n:#u8){x}')
                r=run([EXE,path,'--source','entry','--budget','2']);self.assertEqual(json.loads(r.stdout)['status'],'contradictory',r.stdout+r.stderr)
    def test_traps_and_pure_equations(self):
        for source,expected in [
            ('x:#u8;&^entry(n:#u8){x}',17),
            ('&!get()->#u8;a:#u8=get()+b;b:#u8=a;&^entry(n:#u8){a}',18),
            ('&!get()->#u8;a:#u8=get()+a;&^entry(n:#u8){a}',18),
            ('a:#u8[2];&^entry(n:#u8){a[0]=n;0}',17),
            ('&!get()->#u8;a:#u8=b+get();b:#u8=a;&^entry(n:#u8){a}',18),
        ]:
            for schedule,branch in ORDERS:
                with self.subTest(source=source,schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-boundary-trap-') as temp:
                    temp=Path(temp);out,_=self.emit(temp,source,schedule,branch,budget=2)
                    harness=temp/'main.c';harness.write_text('#include "vexel.h"\n#include <stdlib.h>\nvoid vx_trap(uint8_t reason){exit(reason);}\nvoid vx_external_get(uint32_t *out){out[0]=1;}\nint main(void){vx_entry(0);return 0;}\n')
                    executable=temp/'run';r=run(['cc','-std=c11','-O2','-I'+str(out),'-I'+str(ROOT/'backends/msx-neo8/runtime'),out/'vx_bridges.c',harness,'-o',executable]);self.assertEqual(r.returncode,0,r.stderr)
                    self.assertEqual(run([executable]).returncode,expected)
        for source,status,query in [
            ('x:#u8=3;&^entry(n:#u8){x=n;x}','contradictory','value:entry'),
            ('&!get()->#u8;x=?{get();42};','contradictory','value:x'),
            ('x=?{n:#u8=0;[1,2,3]!@{n+=1;};n};','contradictory','value:x'),
            ('a:#u8=b;b:#u8=a;','unresolved','value:a'),
            ('&!get()->#u8;a:#u8=get();','resolved','type:a'),
        ]:
            for schedule,branch in ORDERS:
                with self.subTest(source=source,schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-boundary-query-') as temp:
                    path=Path(temp)/'source.vx';path.write_text(source);r=run([EXE,path,'--query',query,'--schedule',schedule,'--branch-order',branch]);self.assertEqual(json.loads(r.stdout)['status'],status,r.stdout+r.stderr)
if __name__=='__main__':unittest.main(verbosity=2)
