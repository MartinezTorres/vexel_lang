#!/usr/bin/env python3
"""Trailing shapes, private receivers, ordered effects and empty interfaces."""
import unittest
import test_tuples as support

class BroadcastTests(unittest.TestCase):
    check=support.TupleTests.check
    execute=support.TupleTests.execute

    def test_shapes_operators_and_independent_types(self):
        cases=[
            ('a=[[1,2,3],[4,5,6]] .+ [10,20,30];','Values(Values(11,22,33),Values(14,25,36))'),
            ('a=[[1],[2]] .* [3,4,5];','Values(Values(3,4,5),Values(6,8,10))'),
            ('a=10 .- [1,2];','Values(9,8)'),
            ('a=[1,2] .< [2,1];','Values(1,0)'),
            ('a=[0,1] .&& [1,1];','Values(0,1)'),
            ('a=[1,2] .<< [2];','Values(4,8)'),
            ('a={arr:#u8[2];v:#[arr .+ 1]=[3,4];v[0]};','3'),
            ('a=?{arr=?(#u8[2],1..4);v=arr .+ [2,3];?(v[0]==4);?(v[1]==6);arr};','Values(2,3)'),
        ]
        for source,value in cases:self.check(source,value)
        self.check('a=[1,2] .+ [1,2,3];',status='contradictory')
        self.check('a=[0,1] .|| [1/0,1];',status='contradictory')
        self.check('#P(x:#Missing);p:#P[3];a=p .+ 1;','3',query='length:a')

    def test_snapshots_compounds_and_private_receiver_copies(self):
        prefix='#P(x:#u8);&(p)#P::+(q:#P){p.x+=q.x;(#P(p.x))}'
        cases=[
            ('a={i:#u8=0;arr:#u8[2]=[1,2];arr .+= (i+=1);arr[0]+arr[1]+i};','6'),
            ('a={i:#u8=0;arr=[i+=1,i] .+ [3,4];arr[0]+arr[1]+i};','10'),
            ('a={i:#u8=0;arr:#u8[2][2]=[[1,2],[3,4]];arr[i+=1] .+= 3;arr[1][0]+i};','7'),
            ('a={arr:#u8[2]=[1,2];v=arr .+ {arr[0]=7;3};arr[0]+v[0]};','11'),
            ('a={arr:#u8[2]=[1,2];arr .+= {arr[0]=7;3};arr[0]};','4'),
            (prefix+'a={arr=[#P(1),#P(2)];v=arr .+ #P(3);arr[0].x+v[1].x};','6'),
            (prefix+'a={arr=[#P(1),#P(2)];arr .+= #P(3);arr[0].x+arr[1].x};','9'),
            ('#P(x:#u8);a={count:#u8=0;&(p)#P::+(q:#P){count+=1;(#P(p.x+q.x+count))}'
             'arr=[#P(1),#P(2)] .+ #P(3);arr[0].x*10+arr[1].x+count};','59'),
            ('#P(x:#u8);&(p)#P::+(q:#P){(#P(99))}&(arr:#P[2]).+(q:#P){[#P(4),#P(5)]}'
             'a=([#P(1),#P(2)] .+ #P(3))[1].x;','5'),
        ]
        for source,value in cases:self.check(source,value)

    def test_implicit_call_capture_closure(self):
        method='&(p)#P::+(q:#P){count+=1;(#P(p.x+q.x))}'
        for operation in ('#P(1)+#P(2);','[#P(1),#P(2)].+#P(3);'):
            self.check('#P(x:#u8);a={count:#u8=0;'+method+'&helper(){'+operation+'}helper();count};',
                       '2' if operation.startswith('[') else '1')
        self.execute('#P(x:#u8);&^entry(n:#u8){count:#u8=0;'+method+
                     '&~helper(k:#u8){[#P(k),#P(k)] .+ #P(k);}helper(n);count}',lambda n:[2])

    def test_empty_shapes_and_method_interfaces(self):
        cases=[
            ('a={arr:#u8[0]=[];arr .+ (#u8)2};','Values'),
            ('a={arr:#u8[3][0]=[];arr .+ [(#u8)1,2,3]};','Values'),
            ('a={arr:#u8[0][2]=[[],[]];arr ./ (#u8)0};','Values(Values,Values)'),
            ('a={i:#u8=0;arr:#u8[0]=[];arr .+ (i+=1);i};','1'),
            ('#P(x:#u8);&(p)#P::+(q:#P){p.x+q.x}a={arr:#P[2];v:#[arr .+ #P(3)]=[5,7];v[1]};','7'),
            ('#P(x:#u8);&(p)#P::/(q:#P){p.x/q.x}a={arr:#P[0]=[];arr ./ #P(0)};','Values'),
            ('#P(x:#u8);a={count:#u8=0;&(p)#P::+(q:#P){count+=1;p.x+q.x}arr:#P[0]=[];arr .+ #P(3);count};','0'),
        ]
        for source,value in cases:self.check(source,value)

    def test_materialized_calls_captures_and_discarded_traps(self):
        self.execute('&^entry(n:#u8){[[n],[n+1]] .+ [(#u8)2,3,4]}',
                     lambda n:[(n+2)%256,(n+3)%256,(n+4)%256,(n+3)%256,(n+4)%256,(n+5)%256],6)
        self.execute('#P(x:#u8);&^entry(n:#u8){count:#u8=0;&~(p)#P::+(q:#P){count+=1;p.x+=q.x;(#P(p.x+count))}'
                     'arr=[#P(n),#P(n+1)];v=arr .+ #P(3);(arr[0].x,v[0].x,v[1].x,count)}',
                     lambda n:[n,(n+4)%256,(n+6)%256,2],4)
        self.execute('&^entry(n:#u8){arr:#u8[2][2]=[[1,2],[3,4]];i:#u8=n%2;arr[i] .+= n;arr}',
                     lambda n:[(1+(n if n%2==0 else 0))%256,(2+(n if n%2==0 else 0))%256,(3+(n if n%2 else 0))%256,(4+(n if n%2 else 0))%256],4)
        self.execute('&^entry(n:#u8){[(#u8)1,2] ./ n;(#u8)7}',lambda n:[7] if n else (1,))

if __name__=='__main__':unittest.main(verbosity=2)
