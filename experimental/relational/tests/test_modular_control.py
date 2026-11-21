#!/usr/bin/env python3
"""Modular certificates close pure relationships without erasing execution."""
import unittest
import test_tuples as support


class ModularControlTests(unittest.TestCase):
    check = support.TupleTests.check
    execute = support.TupleTests.execute

    def test_full_width_identities(self):
        for scalar in ('#u32','#i32'):
            for condition in ('a*b!=b*a', 'a*(b+c)!=a*b+a*c', 'a*b-b*a!=0'):
                self.check('&^entry(a:'+scalar+',b:'+scalar+',c:'+scalar+'){r=a;{'+condition+'?missing();r+=1;};r}',
                           query='value:entry')
        self.check('&^entry(a:#i32,b:#i32){r=a;{(a*b-b*a)-1 != -1?missing();r+=1;};r}',
                   query='value:entry')

    def test_native_calls_keep_captured_writes(self):
        self.execute('count:#u8;&~product(k:#u32,l:#u32){count+=1;k*l}'
                     '&^entry(n:#u8){count=0;x=(#u32)n;y=x+1;r=n;'
                     '{product(x,y)!=product(y,x)?missing();r+=1;};(r,count)}',
                     lambda n:[(n+1)&255,2],2)

    def test_native_prefix_trap_is_still_executed(self):
        self.execute('&~product(k:#u32,l:#u32){1/k;k*l}'
                     '&^entry(n:#u8){x=(#u32)n;y=x+1;r=n;'
                     '{product(x,y)!=y*x?missing();r+=1;};r}',
                     lambda n:[(n+1)&255] if n else (1,))

    def test_mixed_width_wrapping_and_sign_extension(self):
        self.execute('&^entry(n:#u8){a=(#u16)(n+1);b=(#u16)n+1;'
                     '(a==b,(#i16)(#i8)n==(#i16)n)}',
                     lambda n:[int(n!=255),int(n<128)],2)
        self.check('&^entry(n:#u8){r=n;{((#u16)(n+1)!=(#u16)n+1)?missing();r+=1;};r}',
                   status='unresolved',query='value:entry')

    def test_distinct_external_results_are_not_correlated(self):
        self.check('&!read()->#u32;&^entry(n:#u8){x=(#u32)n;r=n;'
                   '{read()*x!=x*read()?missing();r+=1;};r}',
                   status='unresolved',query='value:entry')

    def test_identity_does_not_initialize_its_inputs(self):
        self.check('&^entry(n:#u8){x:#u32;y=(#u32)n;r=n;'
                   '{x*y!=y*x?missing();r+=1;};r}',
                   status='unresolved',query='value:entry')


if __name__=='__main__':
    unittest.main(verbosity=2)
