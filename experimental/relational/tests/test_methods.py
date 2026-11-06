#!/usr/bin/env python3
"""Logical receiver aliases, designation order and native shared-root frames."""
import unittest
import test_tuples as support

class MethodTests(unittest.TestCase):
    check=support.TupleTests.check
    execute=support.TupleTests.execute

    def test_locations_temporaries_and_order(self):
        pre='#P(x:#u8,y:#u8);&(p)#P::add(v:#u8){p.x+=v;p.x}'
        for body,expected in [
            ('a={p=#P(1,2);p.add(3)+p.x};','8'),
            ('a=#P(1,2).add(3);','4'),
            ('a={p=#P(1,2);q=p;q.add(3);p.x};','1'),
            ('#Q(p:#P);a={q=#Q(#P(1,2));q.p.add(3);q.p.x};','4'),
            ('a={arr=[#P(1,2),#P(3,4)];i:#u8=0;arr[i+=1].add(5);arr[1].x+i};','9'),
        ]:self.check(pre+body,expected)
        self.check('#P(x:#u8);&(p)#P::get(ignored){p.x}a={p=#P(1);p.get(p.x=7)};','7')
        self.check('&(x:#u8)invert(){~x}a=(1).invert();','254')
        self.check('&(x:#u16)next(){x+=1;x}a=((#u8)255).next();','256')
        self.check('&(x:#u16)next(){x+=1;x}a={y:#u8=255;y.next()};',status='contradictory')

    def test_overlap_initialization_and_captures(self):
        self.check('&(a,b)merge(){a+=1;b+=a;a+b}a={x:#u8=2;(x,x).merge()+x};','18')
        self.check('#P(x:#u8);&(a:#P,b:#u8)change(){a.x+=1;b+=a.x;b}a={p=#P(2);(p,p.x).change()+p.x};','12')
        self.check('&(x:#u8)init(){x=7;}a={x:#u8;x.init();x};','7')
        self.check('&(x:#u8,y:#u8)init(){x,y=(2,3);}a={x:#u8;y:#u8;(x,y).init();x+y};','5')
        self.check('&(x:#u8)put(v:#u8){x=v;}a={x:#u8=1;&nested(){x.put(3);}nested();x};','3')
        self.check('&(x:#u8)put(v:#u8){&nested(){x=v;}nested();}a={x:#u8=1;x.put(3);x};','3')

    def test_native_aliases_and_dynamic_designations(self):
        self.execute('&~(a:#u8,b:#u8)merge(){a+=1;b+=a;a+b}&^entry(n:#u8){x:#u8=n;(x,x).merge()+x}',lambda n:[6*(n+1)%256])
        self.execute('#P(x:#u8);&~(a:#P,b:#u8)change(){a.x+=1;b+=a.x;b}&^entry(n:#u8){p=#P(n);(p,p.x).change()+p.x}',lambda n:[4*(n+1)%256])
        self.execute('#P(x:#u8);&~(p)#P::add(v:#u8){p.x+=v;}&^entry(n:#u8){a=[#P(n),#P(n+1)];i:#u8=n%2;a[i].add(3);(a[0].x,a[1].x)}',
                     lambda n:[(n+(3 if n%2==0 else 0))%256,(n+1+(3 if n%2 else 0))%256],2)

    def test_native_output_only_nested_captures_and_recursion(self):
        self.execute('&~(x:#u16)init(n:#u8){x=(#u16)n+300;}&^entry(n:#u8){x:#u16;x.init(n);x}',lambda n:[n+300])
        self.execute('&(x:#u8)put(v:#u8){&~nested(){x=v;}nested();}&^entry(n:#u8){x:#u8=1;x.put(n);x}',lambda n:[n])
        self.execute('&~(p:#u8)walk(n:#u8)->#u8{n==0 ? p : {p+=1;p.walk(n-1)}}&^entry(n:#u8){x:#u8=n;x.walk(3)}',lambda n:[(n+3)%256])

if __name__=='__main__':unittest.main(verbosity=2)
