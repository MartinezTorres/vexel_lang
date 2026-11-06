#!/usr/bin/env python3
"""Receiver certificates preserve logical aliases and designated paths."""
import unittest
import test_tuples as support

class NativeReceiverControlTests(unittest.TestCase):
    execute=support.TupleTests.execute
    check=support.TupleTests.check

    def test_scalar_read_and_write(self):
        self.execute('&~(x:#u8)read(){x}&^entry(n:#u8){x=n;r=n;{x.read()!=n?missing();r+=1;};r}',lambda n:[(n+1)&255])
        self.execute('&~(x:#u8)set(k:#u8){x=k;}&^entry(n:#u8){x:#u8;r=n;{x.set(n);x!=n?missing();r+=1;};(r,x)}',lambda n:[(n+1)&255,n],2)

    def test_overlapping_roots(self):
        self.execute('&~(a:#u8,b:#u8)merge(){a+=1;b+=a;a+b}'
                     '&^entry(n:#u8){x=n;r=n;{v=(x,x).merge();(v!=4*(n+1)||x!=2*(n+1))?missing();r+=1;};(r,x)}',lambda n:[(n+1)&255,(2*(n+1))&255],2)
        self.execute('#P(x:#u8);&~(a:#P,b:#u8)change(){a.x+=1;b+=a.x;b}'
                     '&^entry(n:#u8){p=#P(n);r=n;{v=(p,p.x).change();(v!=2*(n+1)||p.x!=v)?missing();r+=1;};(r,p.x)}',lambda n:[(n+1)&255,(2*(n+1))&255],2)

    def test_temporary_and_nested_receiver(self):
        self.execute('&~(x:#u16)next(){x+=1;x}&^entry(n:#u8){r=n;{((#u8)n).next()!=(#u16)n+1?missing();r+=1;};r}',lambda n:[(n+1)&255])
        self.execute('#P(x:#u8);#Q(p:#P);&~(p)#P::add(k:#u8){p.x+=k;p.x}'
                     '&^entry(n:#u8){q=#Q(#P(n));r=n;{v=q.p.add(n);(v!=n+n||q.p.x!=v)?missing();r+=1;};(r,q.p.x)}',lambda n:[(n+1)&255,(2*n)&255],2)

    def test_dynamic_designation_precedes_argument_effects(self):
        self.execute('&~(x:#u8)put(k:#u8){x=k;}'
                     '&^entry(n:#u8){data:#u8[2]=[n,n+1];i=n&1;r=n;'
                     '{data[i].put({i^=1;n+2});'
                     '((n&1)==0?data[0]!=n+2:data[1]!=n+2)?missing();r+=1;};(r,data[0],data[1],i)}',
                     lambda n:[(n+1)&255,(n+2)&255 if n%2==0 else n,(n+2)&255 if n%2 else (n+1)&255,1-n%2],4)

    def test_receiver_readiness_and_traps(self):
        self.check('&~(x:#u8)read(){x}&^entry(n:#u8){x:#u8;r=n;{x.read()!=n?missing();r+=1;};r}',status='unresolved',query='value:entry')
        self.execute('&~(x:#u8)set(k:#u8){x=k;1/k;}&^entry(n:#u8){x:#u8;r=n;{x.set(n);x!=n?missing();r+=1;};(r,x)}',lambda n:[(n+1)&255,n] if n else (1,),2)

if __name__=='__main__':unittest.main(verbosity=2)
