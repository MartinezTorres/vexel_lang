#!/usr/bin/env python3
"""Dimension facts remain independent of unrelated element schemas."""
import unittest
import test_tuples as support

class ShapeTests(unittest.TestCase):
    check=support.TupleTests.check
    execute=support.TupleTests.execute

    def test_partial_broadcast_shapes_and_rank_alignment(self):
        prefix='#P(x:#Missing);'
        for declarations in ('p:#P[2][3];q:#P[2];','q:#P[2];p:#P[2][3];'):
            self.check(prefix+declarations+'a=p .+ q;','3',query='length:a')
        cases=[
            ('p:#P[1][3];q:#P[2];a=p .+ q;','3'),
            ('p:#P[2][1];q:#P[1][3];a=p .+ q;','3'),
            ('p:#P[2][0];q:#P[2];a=p .+ q;','0'),
            ('p:#P[2][3];q:#[p];a=p .+ q;','3'),
            ('p:#P[2][3];q:#P[2];&wrap(x){x}a=wrap(p) .+ wrap(q);','3'),
            ('#Box(data:#P[2][3],other:#Missing);box:#Box;q:#P[2];a=box.data .+ q;','3'),
            ('p:#P[2][3][4];q:#P[2];a=p[missing()] .+ q;','3'),
        ]
        for source,answer in cases:self.check(prefix+source,answer,query='length:a')
        self.check(prefix+'p:#P[2][3];q:#P[3];a=p .+ q;',status='contradictory',query='length:a')
        self.check(prefix+'p:#P[2][3];q:#P[2];a=p .+ q;',status='unresolved',query='type:a')

    def test_explicit_methods_math_and_frozen_schema_dimensions(self):
        self.check('#P(x:#u8);&(a:#P[2]).+(b:#P[2]){[(#u8)1,2,3,4]}p:#P[2];q:#P[2];a=p .+ q;',
                   '4',query='length:a')
        self.check('::std::math;values:#f32[2][3];exponents:#u8[2];a=pow(values,exponents);',
                   '3',query='length:a')
        self.check('#P(x:#Missing);p:#P[2][3];q:#P[2];a={#Box(data:#u8[|p .+ q|]);#Box([1,2,3])};',
                   '3',query='length:a.data')
        self.execute('&^entry(n:#u8){rows:#u8[2][3]=[[n,n],[n,n],[n,n]];weights:#u8[2]=[1,2];'
                     '#Packet(data:#u8[|rows .+ weights|]);p=#Packet([3,4,5]);p.data[0]+n}',
                     lambda n:[(n+3)%256])

if __name__=='__main__':unittest.main(verbosity=2)
