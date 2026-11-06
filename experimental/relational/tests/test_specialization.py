#!/usr/bin/env python3
"""Known leaf facts survive a runtime materialization boundary."""
import unittest
import test_tuples as support

class SpecializationTests(unittest.TestCase):
    execute=support.TupleTests.execute

    def test_strings_wide_constants_and_dependent_interfaces(self):
        self.execute('&~lookup(text:#s,n:#u8)->#u8{text[n%|text|]}'
                     '&^entry(n:#u8){lookup("AZ",n)+lookup("BCD",n)}',
                     lambda n:[(b'AZ'[n%2]+b'BCD'[n%3])%256])
        self.execute('&~choose(n:#u8,mode:#b){mode ? n : (#u16)n+300}'
                     '&^entry(n:#u8){(choose(n,1),choose(n,0))}',lambda n:[n,n+300],2)
        self.execute('&~sized(n:#u8,count:#u8){values:#u8[count]=[n,n+1];values[1]}'
                     '&^entry(n:#u8){sized(n,2)}',lambda n:[(n+1)%256])
        self.execute('&~add(n:#u8,wide:#u64){n+(#u8)(wide>>32)}'
                     '&^entry(n:#u8){add(n,0x1200000000)}',lambda n:[(n+18)%256])

    def test_mixed_aggregate_leaves_and_recursive_static_facts(self):
        self.execute('#Packet(tag:#s,items:#u8[2]);&~pick(p:#Packet,n:#u8){p.items[n%2]+p.tag[n%2]}'
                     '&^entry(n:#u8){pick(#Packet("AZ",[n,n+1]),n)}',
                     lambda n:[(n+n%2+b'AZ'[n%2])%256])
        self.execute('&~sum(n:#u8,text:#s)->#u16{n==0 ? (#u16)text[0] : (#u16)text[n%|text|]+sum(n-1,text)}'
                     '&^entry(n:#u8){sum(n,"ABC")}',lambda n:[sum(b'ABC'[i%3] for i in range(n+1))])

if __name__=='__main__':unittest.main(verbosity=2)
