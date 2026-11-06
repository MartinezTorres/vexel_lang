#!/usr/bin/env python3
"""Numeric reachability is proved with shared native bits, not guessed guards."""
import unittest
import test_tuples as support

class NumericControlTests(unittest.TestCase):
    check=support.TupleTests.check
    execute=support.TupleTests.execute

    def test_arithmetic_dead_branches(self):
        for zero in ('n-n', 'n*3-n-n-n', '(n^n)', '(n&15)-(n&15)'):
            self.execute('&^entry(n:#u8){r=n;{(('+zero+')!=0)?missing();r+=1;};r}',lambda n:[(n+1)&255])

    def test_ordered_guards_and_wrapping(self):
        self.execute('&^entry(n:#u8){r=n;n<128?{{n<129?{r+=1;}:missing();};};r}',lambda n:[n+1 if n<128 else n])
        self.check('&^entry(n:#u8){r=n;{((n+1)>n)?{r+=1;}:missing();};r}',status="unresolved",query="value:entry")
        self.execute('&^entry(n:#u8){r=n;n<255?{{((n+1)>n)?{r+=1;}:missing();};};r}',lambda n:[n+1 if n<255 else n])

    def test_signed_guards_and_type_queries(self):
        self.execute('&^entry(n:#u8){r=n;n<128?{{((#i8)n)>=0?{r+=1;}:missing();};};r}',lambda n:[n+1 if n<128 else n])
        for marker in ('&','&~'):
            self.execute(marker+'f(k:#u8){k<129?(#u8)3:missing()}'
                         '&^entry(n:#u8){r=n;n<128?{{v:#[f(n)]=n;r=v+1;};};r}',lambda n:[n+1 if n<128 else n])

    def test_numeric_initialization_and_prefix_traps(self):
        self.execute('&^entry(n:#u8){r:#u8;n<129?{r=n;};n<128?r:n}',lambda n:[n])
        self.execute('&^entry(n:#u8){r=n;{r=1/n;(n-n)!=0?missing();};r}',lambda n:[1//n] if n else (1,))
        self.execute('&^entry(n:#u8){r=n;{((1/n)-(1/n))!=0?missing();r+=1;};r}',lambda n:[(n+1)&255] if n else (1,))

    def test_first_loop_iteration_uses_initial_values(self):
        self.check('a={x:#u8=0;{[1,2]!@{x+=1;x==1?missing();};};x};',status='unresolved')
        self.check('a={data:#u8[2]=[1,2];x:#u8=0;{data!@{_==1?missing();x+=1;};};x};',status='unresolved')

    def test_unknown_external_results_cannot_discard_a_branch(self):
        # No relationship between an external result and its input is supplied.
        # A runtime result cannot erase an invalid reachable body.
        self.check('&!read(k:#u8)->#u8;&^entry(n:#u8){'
                   'r=n;{read(n)!=n?missing();r+=1;};r}',status='unresolved',query='value:entry')

if __name__=='__main__':unittest.main(verbosity=2)
