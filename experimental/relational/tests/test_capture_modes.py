#!/usr/bin/env python3
"""Captured type facts, incoming values and returned writes are distinct."""
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support

READ='&^entry(n:#u8){a:#u8=n;b:#u16=(#u16)n+300;&~read(k:#u8){(#u16)a+b+k}read(n)}'
WRITE='&^entry(n:#u8){a:#u16;n<128?{a=1;};&~set(k:#u8){a=(#u16)k+300;}set(n);a}'
WRITE_LOOP='&^entry(n:#u8){a:#u16;&~set(k:#u8){[0]!@{a=(#u16)k+300;};}set(n);a}'
TYPE_PREFIX='&^entry(n:#u8){size:#u8=2;size=3;data:#u8[size]=[n,n,n];'
TYPE_ONLY=TYPE_PREFIX+'&~helper(k:#u8){copy:#[data]=[k,k,k];|copy|}helper(n)}'
TYPE_NESTED=TYPE_PREFIX+'&~leaf(k:#u8){copy:#[data]=[k,k,k];|copy|}&~helper(k:#u8){leaf(k)}helper(n)}'
READ_LOOP='&^entry(n:#u8){a:#u16=(#u16)n;&~helper(k:#u8){i:#u8=0;s:#u16=0;(i<3)!@{s+=a;i+=1;};s+k}helper(n)}'

class CaptureModeTests(unittest.TestCase):
    execute=support.TupleTests.execute

    def test_read_only_and_loop_captures(self):
        self.execute(READ,lambda n:[3*n+300])
        self.execute(READ_LOOP,lambda n:[4*n])
        self.execute('#P(tag:#s,x:#u8);&^entry(n:#u8){p=#P("abc",n);'
                     '&~read(k:#u8){(#u16)p.x+k+|p.tag|}read(n)}',lambda n:[2*n+3])

    def test_overwrite_without_incoming_initialization(self):
        self.execute(WRITE,lambda n:[n+300])
        self.execute(WRITE_LOOP,lambda n:[n+300])
        self.execute('&^entry(n:#u8){a:#u16;&~set(k:#u8){a=(#u16)k+300;}'
                     '&~outer(k:#u8){set(k);a+1}outer(n)}',lambda n:[n+301])
        self.execute('&^entry(n:#u8){a:#u8[2];&~set(k:#u8){a=[k,k+1];}set(n);a}',
                     lambda n:[n,(n+1)&255],2)

    def test_type_only_captures_keep_reaching_types(self):
        # Re-evaluating data's lexical declaration in an isolated frame would
        # incorrectly use size=2. Its actual reaching type has length three.
        self.execute(TYPE_ONLY,lambda n:[3])
        self.execute(TYPE_NESTED,lambda n:[3])

    def test_read_write_and_partial_updates(self):
        self.execute('&^entry(n:#u8){a:#u16=(#u16)n;b:#u16=7;'
                     '&~update(k:#u8){a+=k;b=(#u16)k+300;a+b}x=update(n);(x,a,b)}',
                     lambda n:[3*n+300,2*n,n+300],3)
        self.execute('&^entry(n:#u8){a:#u8[2]=[n,n+1];&~set(k:#u8){a[1]=k;}set(n);a}',
                     lambda n:[n,n],2)

    def test_inspectable_capture_modes(self):
        for source,expected in ((READ,[(2,2,0)]),(WRITE,[(1,0,1)]),(WRITE_LOOP,[(1,0,1)]),
                                (TYPE_ONLY,[(1,0,0)]),(TYPE_NESTED,[(1,0,0),(1,0,0)]),(READ_LOOP,[(1,1,0)])):
            for schedule,branch in support.ORDERS:
                with self.subTest(source=source,schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-capture-mode-') as temp:
                    path=Path(temp)/'source.vx';path.write_text(source)
                    r=support.run([support.EXE,path,'--source','entry','--schedule',schedule,'--branch-order',branch])
                    self.assertEqual(r.returncode,0,r.stdout+r.stderr);report=json.loads(r.stdout)
                    self.assertEqual(sorted((f['captures'],f['input_captures'],f['output_captures']) for f in report['runtime_functions']),expected)

    def test_missing_values_are_not_fabricated(self):
        for source in (
            '&^entry(n:#u8){a:#u8;&~read(k:#u8){a+k}read(n)}',
            '&^entry(n:#u8){a:#u8;&~set(k:#u8){k<128?{a=k;};}set(n);a}',
            '&^entry(n:#u8){a:#u8[2];&~set(k:#u8){a[1]=k;}set(n);a}',
        ):
            for schedule,branch in support.ORDERS:
                with self.subTest(source=source,schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-capture-mode-uninitialized-') as temp:
                    path=Path(temp)/'source.vx';path.write_text(source)
                    r=support.run([support.EXE,path,'--source','entry','--schedule',schedule,'--branch-order',branch])
                    self.assertEqual(r.returncode,1,r.stdout+r.stderr)
                    self.assertEqual(json.loads(r.stdout)['status'],'unresolved',r.stdout)

if __name__=='__main__':unittest.main(verbosity=2)
