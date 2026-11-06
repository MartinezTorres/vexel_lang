#!/usr/bin/env python3
"""Operator selection, bound receivers, builtin fallback and logical order."""
import unittest
import test_tuples as support

class OperatorMethodTests(unittest.TestCase):
    check=support.TupleTests.check
    execute=support.TupleTests.execute

    def test_scalar_operators_and_compound_designation(self):
        add='#P(x:#u8);&(p)#P::+(q:#P){(#P(p.x+q.x))}'
        self.check(add+'a={p=#P(2)+#P(3);p.x+1};','6')
        self.check(add+'a={arr=[#P(2),#P(3)];i:#u8=0;arr[i+=1]+=#P(4);arr[1].x+i};','8')
        self.check('#P(x:#u8);&(p)#P::==(q:#P){p.x==q.x}a=#P(2)==#P(2);','1')
        self.check('#P(x:#u8);&(p)#P::-(){(#P(0-p.x))}a={q=-#P(2);q.x};','254')
        self.check('#P(x:#u8);&(p)#P::+(q:#P){p.x+=q.x;(#P(p.x+1))}a={p=#P(2);p+=#P(3);p.x};','6')
        self.check(add+'a={x:#u8=1;x+{x=7;1}};','2')

    def test_logical_method_eagerness_and_builtin_short_circuit(self):
        logical='#P(x:#u8);&(p)#P::&&(q:#P){(#P(p.x+q.x))}'
        for suffix,expected in [
            ('a=(#P(2)&&#P(3)).x;','5'),
            ('a=0 && missing;','0'),
            ('a=1 || (1/0);','1'),
            ('a={p=#P(2);p&&=#P(3);p.x};','5'),
            ('#Q(p:#P);a={q=#Q(#P(2));q.p&&=#P(4);q.p.x};','6'),
            ('a={arr=[#P(1),#P(2)];i:#u8=0;arr[i+=1]&&=#P(4);arr[1].x+i};','7'),
            ('a={p=#P(2);q=p && {p.x=7;(#P(3))};q.x};','10'),
        ]:self.check(logical+suffix,expected)
        self.check('&(p:#b)&&(q:#s){p}a=(#b)0 && (1/0);','0')
        self.check('&(p:#b)&&(q:#u8){p}a=(#b)0 && 300;','0')
        self.check('&(p:#b)&&(q:#u8){p}a=(#b)0 && (1/0);',status='contradictory')
        self.check('#P(x:#u8);&(p)#P::&&(q:#P){p.x+q.x}a={p:#P;q:#P;v:#[p&&q]=7;v};','7')

    def test_materialized_operator_state_and_dynamic_places(self):
        self.execute('#P(x:#u8);&~(p)#P::+(q:#P){p.x+=q.x;(#P(p.x+1))}&^entry(n:#u8){p=#P(n);p+=#P(n);p.x}',lambda n:[(2*n+1)%256])
        self.execute('#P(x:#u8);&~(p)#P::&&(q:#P){(#P(p.x+q.x))}&^entry(n:#u8){p=#P(n);q=p&&{p.x=7;(#P(n))};(q.x,p.x)}',lambda n:[(n+7)%256,7],2)
        self.execute('#P(x:#u8);&~(p)#P::+(q:#P){(#P(p.x+q.x))}&^entry(n:#u8){arr=[#P(n),#P(n+1)];i:#u8=n%2;arr[i]+=#P(3);(arr[0].x,arr[1].x)}',
                     lambda n:[(n+(3 if n%2==0 else 0))%256,(n+1+(3 if n%2 else 0))%256],2)

if __name__=='__main__':unittest.main(verbosity=2)
