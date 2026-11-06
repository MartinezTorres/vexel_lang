#!/usr/bin/env python3
"""Fixed layouts retain their field relationships across construction and writes."""
import ctypes
import itertools
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXE = Path(sys.argv.pop(1)).resolve()
REPO = Path(__file__).resolve().parents[3]
ORDERS = list(itertools.product(('fifo', 'lifo', 'random'), ('forward', 'reverse')))
SCHEMA = '#Packet(count:#u8,data:#u8[count]);'


def run(args):
    return subprocess.run([str(arg) for arg in args], capture_output=True, text=True, timeout=120)


class SchemaTests(unittest.TestCase):
    def check(self, source, expected=None, status='resolved'):
        with tempfile.TemporaryDirectory(prefix='vexel-schema-') as temporary:
            path = Path(temporary)/'source.vx'
            path.write_text(source)
            for schedule, branch in ORDERS:
                result = run([EXE, path, '--query', 'value:answer', '--schedule', schedule, '--branch-order', branch])
                report = json.loads(result.stdout)
                self.assertEqual(report['status'], status, result.stdout+result.stderr)
                if expected is not None:
                    self.assertEqual(report['queries'][0]['answer'], expected)

    def test_constant_relationships_and_partial_queries(self):
        self.check('#P(n,data:#u8[n]);answer={p=#P(3,[1,2,3]);p.data[0]};', '1')
        self.check('#P(n,data:#u8[n+1]);answer={p=#P(2,[1,2,3]);p.data[0]};', '1')
        self.check('#P(x:#b[2]);answer={p=#P([0,1]);p.x[1]};', '1')
        self.check('#P(x:#u8);#Q(data:#P[2]);answer={q=#Q([#P(1),#P(2)]);q.data[1].x};', '2')
        self.check('#P(rows:#u8,cols:#u8,data:#u8[cols][rows]);answer={p=#P(2,3,[[1,2,3],[4,5,6]]);p.data[1][2]};', '6')
        self.check(SCHEMA+'answer={p=#Packet(3,[1,2,3]);p.count+p.data[2]};', '6')
        self.check(SCHEMA+'answer={p=#Packet(2,[1,2,3]);7};', status='contradictory')
        self.check(SCHEMA+'answer={n:#u8;v:#[#Packet(n,[1,2,3]).data]=[4,5,6];v[1]};', '5')
        self.check(SCHEMA+'answer={p=#Packet(3,[1,2,3]);p.count=2;7};', status='contradictory')
        self.check(SCHEMA+'answer={p=#Packet(3,[1,2,3]);p.data[0]=7;p.count};', '3')
        self.check('answer={offset:#u8=1;#P(count:#u8,data:#u8[count+offset]);p=#P(2,[1,2,3]);offset=2;p.count=2;p.data[2]};', '3')

    def test_runtime_construction_mutation_and_discard(self):
        cases = [
            (SCHEMA+'&^entry(n:#u8){p=#Packet(n,[4,5,6]);p.count+p.data[0]}', 3, 7),
            (SCHEMA+'&^entry(n:#u8){p=#Packet(3,[4,5,6]);p.count=n;p.data[2]}', 3, 6),
            (SCHEMA+'&^entry(n:#u8){p=#Packet(n,[4,5,6]);7}', 3, 7),
            ('&extent(count:#u8){count+=1;count}#P(count:#u8,data:#u8[extent(count)]);&^entry(n:#u8){p=#P(n,[4,5,6]);p.data[0]}', 2, 4),
            ('&^entry(n:#u8){offset:#u8=1;#P(count:#u8,data:#u8[count+offset]);p=#P(2,[4,5,6]);offset=2;p.count=n;p.data[0]}', 2, 4),
            ('&extent(n:#u8){i:#u8=0;result:#u8=0;(i<n)!@{result+=1;i+=1;};result}#P(n:#u8,data:#u8[extent(n)]);&^entry(n:#u8){p=#P(n,[4,5,6]);p.data[0]}',3,4),
            ('&~extent(n:#u8)->#u8{i:#u8=0;result:#u8=0;(i<n)!@{result+=1;i+=1;};result}#P(n:#u8,data:#u8[extent(n)]);&^entry(n:#u8){p=#P(n,[4,5,6]);p.data[0]}',3,4),
        ]
        cases = [(source, admitted, expected, '', '') for source, admitted, expected in cases]
        cases += [
            (SCHEMA+'&!get(n:#u8)->#[#Packet(3,[1,2,3])];&^entry(n:#u8){get(n);7}',3,7,
             'void vx_external_get(uint32_t *out,uint8_t n){out[0]=n;out[1]=4;out[2]=5;out[3]=6;}',''),
            (SCHEMA+'!device:#[#Packet(3,[1,2,3])];&^entry(n:#u8){device;7}',3,7,
             'volatile uint8_t vx_external_device_0,vx_external_device_1=4,vx_external_device_2=5,vx_external_device_3=6;',
             'vx_external_device_0=n;'),
        ]
        for source, admitted, expected, bindings, setup in cases:
            for schedule, branch in ORDERS:
                with self.subTest(source=source, schedule=schedule, branch=branch), tempfile.TemporaryDirectory(prefix='vexel-schema-c-') as temporary:
                    directory = Path(temporary)
                    path, generated = directory/'source.vx', directory/'generated'
                    path.write_text(source)
                    result = run([EXE, path, '--source', 'entry', '--schedule', schedule, '--branch-order', branch, '--emit-c', generated])
                    self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                    harness = directory/'harness.c'
                    harness.write_text('''#include "vexel.h"
#include <setjmp.h>
'''+bindings+'''
static jmp_buf trapped;
void vx_trap(uint8_t reason) {longjmp(trapped,reason);}
uint32_t checked(uint8_t n) {
    int reason=setjmp(trapped);
    if(reason) return 65536UL+(uint32_t)reason;
    '''+setup+'''
    vx_entry(n);
    return vx_entry_read(0);
}
''')
                    library = directory/'execution.so'
                    built = run(['cc', '-std=c11', '-O2', '-shared', '-fPIC', '-I'+str(generated),
                                 '-I'+str(REPO/'backends/msx-neo8/runtime'), generated/'vx_bridges.c', harness, '-o', library])
                    self.assertEqual(built.returncode, 0, built.stderr)
                    dll = ctypes.CDLL(str(library))
                    dll.checked.argtypes, dll.checked.restype = [ctypes.c_uint8], ctypes.c_uint32
                    for n in range(256):
                        self.assertEqual(dll.checked(n), expected if n==admitted else 65536+19, n)

    def test_schema_helpers_cannot_modify_captures_or_call_sinks(self):
        self.check('answer={offset:#u8=1;&extent(n:#u8){offset+=1;n}#P(n:#u8,data:#u8[extent(n)]);p=#P(3,[1,2,3]);p.n};', status='contradictory')
        self.check('&!extent(n:#u8)->#u8;#P(n:#u8,data:#u8[extent(n)]);answer={p=#P(3,[1,2,3]);p.n};', status='contradictory')
        self.check('&!read(n:#u8)->#u8;&~extent(n:#u8)->#u8{read(n)}#P(n:#u8,data:#u8[extent(n)]);&^answer(n:#u8){extent(n);p=#P(n,[1,2,3]);p.n}', status='contradictory')
    def test_captures_cover_future_field_values_and_separate_instances(self):
        self.check('answer={lo:#u8=2;hi:#u8=3;#P(n:#u8,data:#u8[n==0?lo:hi]);first=#P(0,[1,2]);hi=2;second=#P(0,[1,2]);second.n=1;second.data[0]};', '1')
        self.check('answer={lo:#u8=2;hi:#u8=2;#P(n:#u8,data:#u8[n==0?lo:hi]);first=#P(0,[1,2]);hi=3;second=#P(0,[1,2]);second.n=1;second.data[0]};', status='contradictory')
        self.check('answer={lo:#u8=2;hi:#u8;#P(n:#u8,data:#u8[n==0?lo:hi]);p=#P(0,[1,2]);p.data[0]};', status='unresolved')
        self.check('&make(t){lo:#u8=1;#P(n:#u8,v:#[n==0?lo:t]);#P(0,7)}answer={first=make((#u16)2);second=make((#u8)3);second.n=1;second.v};', '7')
        self.check('&make(t){lo:#u8=1;#P(n:#u8,v:#[n==0?lo:t]);#P(0,7)}answer={first=make((#u8)2);second=make((#u16)3);second.n=1;second.v};', status='contradictory')


if __name__ == '__main__':
    unittest.main(verbosity=2)
