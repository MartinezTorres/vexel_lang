#!/usr/bin/env python3
"""Custom iteration completion targets and forwarded caller continuations."""
import unittest
import test_tuples as support

SCHEMA='#Bag(items:#u8[3]);'
METHOD='&(bag)#Bag::@($body){i:#u8=0;(i<3)@{_=bag.items[i];$body;i+=1;};}'

class IterationMethodTests(unittest.TestCase):
    check=support.TupleTests.check
    execute=support.TupleTests.execute

    def test_control_targets_and_caller_returns(self):
        self.check(SCHEMA+METHOD+'a={s:#u8=0;v=#Bag([1,2,3]);v@{_==2?{->|;};s+=_;};s};','1')
        self.check(SCHEMA+METHOD+'a={s:#u8=0;v=#Bag([1,2,3]);v@{_==2?{->>;};s+=_;};s};','4')
        method='&(bag)#Bag::@($body){i:#u8=0;(i<3)@{_=bag.items[i];j:#u8=0;(j<2)@{$body;j+=1;};i+=1;};}'
        self.check(SCHEMA+method+'a={s:#u8=0;visits:#u8=0;v=#Bag([1,2,3]);v@{visits+=1;_==2?{->|;};s+=_;};visits*10+s};','32')
        self.check(SCHEMA+METHOD+'a={s:#u8=0;&f(){v=#Bag([1,2,3]);v@{s+=_;_==2?{->7;};};9}x=f();x+s};','10')

    def test_forwarding_nested_iterations_and_receivers(self):
        method='&forward($x){$x;}&(bag)#Bag::@($body){i:#u8=0;(i<3)@{_=bag.items[i];forward($body);i+=1;};}'
        self.check(SCHEMA+method+'a={s:#u8=0;v=#Bag([1,2,3]);v@{_==2?{->>;};s+=_;};s};','4')
        self.check(SCHEMA+METHOD+'a={s:#u8=0;first=#Bag([1,2,3]);second=#Bag([1,2,3]);first@{_==2?{->>;};x=_;second@{_==2?{->|;};s+=x*10+_;};};s};','42')
        self.check('#P(x:#u8);&(p)#P::@($body){_:#u8=p.x;$body;p.x+=1;}a={p=#P(3);p@{};p.x};','4')
        self.check('#P(x:#u8);&(p)#P::@($body){_:#u8=p.x;$body;}a={total:#u8=0;#P(3)@{total+=_;};total};','3')
        self.check(SCHEMA+METHOD+'a={s:#u8=0;[1,2,3]@{_==2?{->|;};s+=(#u8)_;};s};','1')
        self.check('#P(x:#u8);&(p)#P::@($body){$body;}a={p=#P(3);p@{};0};',status='unresolved')
        self.check(SCHEMA+METHOD+'a={v=#Bag([1,2,3]);v@{_=7;};0};',status='contradictory')

    def test_runtime_completion_in_implementation_loops(self):
        for loop in ('@','!@'):
            method=METHOD.replace('(i<3)@','(i<3)'+loop)
            self.execute(SCHEMA+method+'&^entry(n:#u8){s:#u8=0;v=#Bag([1,2,3]);v@{_>=n?{->|;};_==2?{->>;};s+=_;};s}',
                         lambda n:[0 if n<=1 else 1 if n<=3 else 4])
            self.execute(SCHEMA+method+'&^entry(n:#u8)->#u8{v=#Bag([1,2,3]);v@{_==n?{->(_+10);};};99}',
                         lambda n:[n+10 if n in (1,2,3) else 99])
        self.execute('&^entry(n:#u8)->#u8{i:#u8=0;(i<3)@{i==n?{->(#u8)9;};i+=1;};i}',lambda n:[9 if n<3 else 3])

    def test_unrolled_runtime_transfers_and_restored_outer_targets(self):
        self.execute('&^entry(n:#u8){i:#u8=0;s:#u8=0;(i<4)@{i+=1;i==n?{->|;};s+=i;};s}',
                     lambda n:[sum(range(1,n)) if 1<=n<=4 else 10])
        self.execute('&^entry(n:#u8){i:#u8=0;s:#u8=0;(i<4)@{i+=1;i==n?{->>;};s+=i;};s}',
                     lambda n:[10-n if 1<=n<=4 else 10])
        self.execute('&^entry(n:#u8){i:#u8=0;s:#u8=0;({i+=1;(i<4 && i==n)?{->>;};i<4})@{s+=i;};s}',
                     lambda n:[6-n if 1<=n<=3 else 6])
        self.execute('&^entry(n:#u8)->#u8{s:#u8=0;[1,2]@{i:#u8=0;(i<3)@{i==n?{->(#u8)99;};i+=1;};s+=1;->|;};s}',
                     lambda n:[99 if n<3 else 1])

    def test_sorted_methods_and_iteration_capture_closure(self):
        method='&(bag)#Bag::@@($body){i:#u8=0;(i<3)@{_=bag.items[i];$body;i+=1;};}'
        self.check(SCHEMA+method+'a={s:#u8=0;v=#Bag([1,2,3]);v@@{s=s*10+_;};s};','123')
        self.check(SCHEMA+method+'a={s:#u8=0;[3,1,2]@@{s=s*10+_;};s};','123')
        self.check('#P(x:#u8);&(p)#P::@@($body){_=p.x;$body;}&!input()->#u8;a={p=#P(input());p@@{};0};',status='unresolved')
        self.execute('#P(x:#u8);&^entry(n:#u8){count:#u8=0;&(p)#P::@($body){count+=1;_=p.x;$body;}'
                     '&~helper(k:#u8){p=#P(k);p@{};}helper(n);count}',lambda n:[1])

if __name__=='__main__':unittest.main(verbosity=2)
