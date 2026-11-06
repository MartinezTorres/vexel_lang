#!/usr/bin/env python3
"""Numeric loop state and initialization close as a finite safety relation."""
import unittest
import test_tuples as support

class NumericLoopTests(unittest.TestCase):
    execute=support.TupleTests.execute
    check=support.TupleTests.check
    def test_first_and_later_iteration_writes(self):
        for assignment in ('x=n;', 'i==1?{x=n;};'):
            self.execute('&^entry(n:#u8){x:#u8;i:#u8=0;(i<2)!@{'+assignment+'i+=1;};x}',lambda n:[n])

    def test_count_dependent_output_readiness(self):
        self.execute('&^entry(n:#u8){count=n&7;x:#u8;i:#u8=0;(i<count)!@{x=i;i+=1;};count>0?x:(#u8)0}',lambda n:[max((n&7)-1,0)])

    def test_reads_after_an_earlier_iteration(self):
        self.execute('&^entry(n:#u8){x:#u8;i:#u8=0;sum:#u8=0;'
                     '(i<3)!@{i>0?{sum+=x;};x=n+i;i+=1;};sum}',lambda n:[(2*n+1)&255])

    def test_continue_and_break_edges(self):
        self.execute('&^entry(n:#u8){x:#u8;i:#u8=0;'
                     '(i<3)!@{i+=1;i<3?{->>;};x=n;};x}',lambda n:[n])
        self.execute('&^entry(n:#u8){x:#u8;i:#u8=0;'
                     '(1)!@{i==1?{x=n;->|;};i+=1;};x}',lambda n:[n])

    def test_signed_counters(self):
        self.execute('&^entry(n:#u8){x:#u8;i:#i8=-2;'
                     '(i<0)!@{i==-1?{x=n;};i+=1;};x}',lambda n:[n])

    def test_nested_loops_and_native_writes(self):
        self.execute('&^entry(n:#u8){x:#u8;i:#u8=0;'
                     '(i<2)!@{j:#u8=0;(j<2)!@{j==1?{x=n+i+j;};j+=1;};i+=1;};x}',lambda n:[(n+2)&255])
        self.execute('&^entry(n:#u8){x:#u8;&~set(k:#u8){x=k;}i:#u8=0;'
                     '(i<2)!@{i==1?{set(n);};i+=1;};x}',lambda n:[n])

    def test_result_types_do_not_require_argument_values(self):
        for body,argument in (
            ('x:#u8;i:#u8=0;(i<2)!@{x=k;i+=1;};x','z:#u8=z;'),
            ('x:#u8;i:#u8=0;(i<2)!@{x=k;i+=1;};x','z:#u8=missing();'),
            ('x:#u8;i:#u8=0;(i<k)!@{x=i;i+=1;};k>0?x:k','z:#u8=z;'),
        ):
            source='&~f(k:#u8){'+body+'}a:#[f(z)];'+argument+'b=f(z);'
            self.check(source,'#u8',query='type:a')
            self.check(source,status='unresolved',query='value:b')

    def test_uninitialized_paths_are_still_rejected(self):
        for source in (
            '&^entry(n:#u8){x:#u8;i:#u8=255;(i<2)!@{x=n;i+=1;};x}',
            '&^entry(n:#u8){x:#u8;i:#u8=0;(i<2)!@{i==0?{x+=n;};x=n;i+=1;};x}',
            '&^entry(n:#u8){x:#u8;i:#u8=0;(i<2)!@{n<128?{x=n;};i+=1;};x}',
        ):self.check(source,status='unresolved',query='value:entry')

if __name__=='__main__':unittest.main(verbosity=2)
