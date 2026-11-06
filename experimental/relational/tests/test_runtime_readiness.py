#!/usr/bin/env python3
"""Initialization proofs must not become speculatively executed guards."""
import unittest
import test_tuples as support

class RuntimeReadinessTests(unittest.TestCase):
    execute=support.TupleTests.execute
    check=support.TupleTests.check

    def test_guarded_partial_operations_across_loops(self):
        for guard,condition in (
            ('n!=0&&1/n==1',lambda n:n==1),
            ('n!=0&&255%n==0',lambda n:n!=0 and 255%n==0),
            ('n<8&&((#u8)1<<n)==1',lambda n:n==0),
            ('n<2&&((#u8[2])[1,2])[n]==1',lambda n:n==0),
        ):
            self.execute('&^entry(n:#u8){x:#u8;[1]!@{('+guard+')?{x=n;};};'
                         '[1]!@{('+guard+')?{x+=1;};};('+guard+')?x:(#u8)0}',
                         lambda n:[(n+1)&255 if condition(n) else 0])

    def test_nested_loops_preserve_guarded_readiness(self):
        self.execute('&^entry(n:#u8){x:#u8;[1]!@{[1]!@{(n!=0&&1/n==1)?{x=n;};};};'
                     '[1]!@{(n!=0&&1/n==1)?{x+=1;};};(n!=0&&1/n==1)?x:(#u8)0}',lambda n:[2 if n==1 else 0])

    def test_inline_capture_and_branch_merges(self):
        for call in ('set();','n<128?{set();}:{set();};'):
            self.execute('&^entry(n:#u8){x:#u8;&set(){[1]!@{(n!=0&&1/n==1)?{x=n;};};}'
                         +call+'[1]!@{(n!=0&&1/n==1)?{x+=1;};};(n!=0&&1/n==1)?x:(#u8)0}',lambda n:[2 if n==1 else 0])

    def test_capability_guard_preserves_existing_readiness(self):
        self.execute('&available(k:#u8){k}&^entry(n:#u8){x:#u8;'
                     '[1]!@{(n!=0&&1/n==1)?{x=n;};};'
                     '?available(n)?{[1]!@{(n!=0&&1/n==1)?{x+=1;};};};'
                     '?absent(n)?{x=99;};(n!=0&&1/n==1)?x:(#u8)0}',lambda n:[2 if n==1 else 0])

    def test_guarded_native_results_and_captured_updates(self):
        self.execute('&~reciprocal(k:#u8)->#u8{1/k}&^entry(n:#u8){x:#u8;'
                     'g=n!=0&&reciprocal(n)==1;[1]!@{g?{x=n;};};'
                     '&~bump(){x+=1;}[1]!@{g?{bump();};};g?x:(#u8)0}',lambda n:[2 if n==1 else 0])

    def test_real_partial_operations_still_trap(self):
        self.execute('&^entry(n:#u8){x:#u8;[1]!@{(1/n==1)?{x=n;};};'
                     '[1]!@{(1/n==1)?{x+=1;};};(1/n==1)?x:(#u8)0}',lambda n:(1,) if n==0 else [2 if n==1 else 0])

    def test_readiness_does_not_prove_uninitialized_reads(self):
        self.check('&^entry(n:#u8){x:#u8;[1]!@{(n!=0&&1/n==1)?{x=n;};};'
                   '[1]!@{x+=1;};x}',status='unresolved',query='value:entry')

if __name__=='__main__':unittest.main(verbosity=2)
