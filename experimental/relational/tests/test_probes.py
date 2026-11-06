#!/usr/bin/env python3
"""Presence proofs demand signatures and retain open or ambiguous obligations."""
import unittest
import test_tuples as support

class ProbeTests(unittest.TestCase):
    check=support.TupleTests.check
    execute=support.TupleTests.execute

    def test_partial_member_presence_without_execution(self):
        for source,expected in [
            ('#P(x,y);a=#P(1/0,missing()).?x;','1'),
            ('#P(x,y);a=#P(1/0,missing()).?z;','0'),
            ('#P(x:#u8,y:#Missing);a=#P(2,missing()).?x;','1'),
            ('#P(x:#u8);&!read()->#P;a=read().?x;','1'),
            ('a=(1,missing()).?__1;','1'),
            ('a=(1,missing()).?__2;','0'),
            ('a=(#u8)1 .?x;','0'),
            ('a={p:#u8;n:#[p.?x]=1;n};','1'),
        ]:self.check(source,expected)
        self.check('p:#Missing;a=p.?x;',status='unresolved')

    def test_method_signature_probes(self):
        pre='#P(x:#u8,y:#Missing);&(p)#P::add(v:#u8){missing()}'
        for source,expected in [
            ('a={p:#P;p.?add(1/0)};','1'),
            ('a=#P(1,missing()).?absent(1/0);','0'),
            ('a=#P(1,missing()).?add("wrong");','0'),
            ('a=#P(1,missing()).?add();','0'),
            ('a=|(#u8)(#P(1,missing()).?add(1/0))|;','1'),
        ]:self.check(pre+source,expected)
        self.check(pre+'&(p)#P::add(v:#u16){p.x}a=#P(1,missing()).?add(1);',status='ambiguous')
        self.check('&(x:#u16)next(){x+=1;x}a={y:#u8;y.?next()};','0')
        self.check('&(x:#u16)next(){x+=1;x}a=((#u8)1).?next();','1')
        self.check('&(x,y)merge(){x+y}a={x:#u8;y:#Missing;(x,y).?merge()};','1')

    def test_queries_and_runtime_do_not_borrow_values(self):
        self.check('#P(x:#u8,y:#Missing);&(p)#P::get(){p.x}a={v:#[#P(1,missing()).get()]=7;v};','7')
        self.check('#P(x:#u8,y:#Missing);&read(p:#P){p.x}&read(p:#u8){p}a={v:#[read(#P(1,missing()))]=7;v};','7')
        self.check('#P(x:#u8,y);&read(p:#P[1]){p[0].x}&read(p:#u8){p}a={v:#[read([#P(1,1/0)])]=7;v};','7')
        self.check('&f(p:(#u8,#u16)){1}&f(p:(#u16,#u16)){2}a=f(((#u8)1,(#u16)2));','1')
        self.execute('#P(x:#u8);&(p)#P::add(v:#u8){p.x+=v;} &^entry(n:#u8){p=#P(n);exists=p.?add(p.x=3);(p.x,(#u8)exists)}',lambda n:[n,1],2)
        self.check('&forward(item){item.?x ? (#u16)0 : item}a:#[forward(b)]=(#u8)1;b:#[forward(a)]=(#u16)2;answer=a+1;','2',query='value:answer')

if __name__=='__main__':unittest.main(verbosity=2)
