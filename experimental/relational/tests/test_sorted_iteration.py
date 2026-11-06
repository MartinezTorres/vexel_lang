#!/usr/bin/env python3
"""Stable ordering through ordinary pure comparison methods."""
import unittest
import test_tuples as support

SCHEMA='#P(key:#u8,tag:#u8);'
LESS='&(p)#P::<(q:#P){p.key<q.key}'

class SortedIterationTests(unittest.TestCase):
    check=support.TupleTests.check
    execute=support.TupleTests.execute

    def test_stability_snapshots_and_builtin_fallback(self):
        source='a={items=[#P(2,1),#P(1,2),#P(2,3)];s:#u16=0;items@@{s=s*10+_.tag;};s+(#u16)items[0].key*1000};'
        self.check(SCHEMA+LESS+source,'2213')
        self.check(SCHEMA+'&(p)#P::<(q:#P){key=p.key;p.key=0;key<q.key}'+source,'2213')
        self.check(SCHEMA+LESS+'a={s:#u16=0;[3,1,2]@@{s=s*10+(#u16)_;};s};','123')
        self.check('a={s:#u16=0;[(2,1),(1,2),(2,3)]@@{s=s*10+(#u16)_.__1;};s};','213')
        self.check('a={s:#u16=0;["b","a","aa"]@@{s=s*10+(#u16)|_|;};s};','121')
        self.check(SCHEMA+LESS+'a={s:#u8=0;items:#P[0]=[];items@@{s+=1;};s};','0')

    def test_captures_and_transitive_purity(self):
        self.check(SCHEMA+'a={descending:#b=1;&(p)#P::<(q:#P){descending ? p.key>q.key : p.key<q.key}'
                   's:#u16=0;[#P(2,1),#P(1,2),#P(2,3)]@@{s=s*10+_.tag;};s};','132')
        for update in ('count+=1;','&bump(){count+=1;}bump();',
                       '&(x)#u8::bump(){x+=1;}count.bump();'):
            self.check(SCHEMA+'a={count:#u8=0;&(p)#P::<(q:#P){'+update+'p.key<q.key}'
                       '[#P(2,1),#P(1,2)]@@{};count};',status='contradictory')
        self.check(SCHEMA+'&!sink();&(p)#P::<(q:#P){sink();p.key<q.key}'
                   'a={[#P(2,1),#P(1,2)]@@{};0};',status='contradictory')
        self.check(SCHEMA+'&!input()->#u8;'+LESS+'a={[#P(input(),1),#P(1,2)]@@{};0};',status='unresolved')

    def test_runtime_payload_and_caller_completion(self):
        self.execute(SCHEMA+LESS+'&^entry(n:#u8){s:#u16=0;items=[#P(2,n),#P(1,n+1),#P(2,n+2)];'
                     'items@@{s=s*3+_.tag;};s}',lambda n:[9*((n+1)%256)+3*n+(n+2)%256])
        self.execute(SCHEMA+LESS+'&^entry(n:#u8)->#u8{items=[#P(2,1),#P(1,2),#P(2,3)];'
                     'items@@{_.tag==n?{->_.key;};};99}',lambda n:[2 if n in (1,3) else 1 if n==2 else 99])

    def test_receiver_scratch_is_not_loop_state(self):
        self.execute('#P(x:#u8);&(p)#P::clear(){p.x=0;} &^entry(n:#u8){#P(n).clear();'
                     'i:#u8=0;(i<3)!@{#P(n+i).clear();i+=1;};n+i}',lambda n:[(n+3)%256])

if __name__=='__main__':unittest.main(verbosity=2)
