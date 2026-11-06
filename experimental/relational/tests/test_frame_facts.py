#!/usr/bin/env python3
"""Static return/capture facts and dynamic native slots share one call interface."""
import unittest
import json
from pathlib import Path
import tempfile
import test_tuples as support

class FrameFactTests(unittest.TestCase):
    execute=support.TupleTests.execute

    def test_static_and_mixed_return_values(self):
        self.execute('&~name(n:#u8)->#s{n<128 ? "AZ" : "AZ"}&^entry(n:#u8){name(n)[n%2]}',
                     lambda n:[b'AZ'[n%2]])
        self.execute('&~pair(n:#u8)->(#u8,#s){(n+1,"AZ")}'
                     '&^entry(n:#u8){p=pair(n);p.__0+p.__1[n%2]}',lambda n:[(n+1+b'AZ'[n%2])%256])
        self.execute('&~wide(n:#u8)->(#u8,#u64){(n,0x1200000000)}'
                     '&^entry(n:#u8){p=wide(n);p.__0+(#u8)(p.__1>>32)}',lambda n:[(n+18)%256])

    def test_static_captures_and_output_only_interfaces(self):
        self.execute('&^entry(n:#u8){text="AZ";&~read(k:#u8){text[k%2]}read(n)}',lambda n:[b'AZ'[n%2]])
        self.execute('&^entry(n:#u8){text:#s;value:#u8;&~init(k:#u8){text="AZ";value=k;}init(n);value+text[n%2]}',
                     lambda n:[(n+b'AZ'[n%2])%256])
        self.execute('&^entry(n:#u8){text="AB";&~replace(k:#u8){text="XYZ";k+1}'
                     'value=replace(n);text[1]+value}',lambda n:[(n+1+89)%256])
        self.execute('#P(tag:#s,value:#u8);&^entry(n:#u8){p=#P("AB",n);&~change(k:#u8){p.value+=k;p.tag="XYZ";}change(n);p.tag[1]+p.value}',
                     lambda n:[(2*n+89)%256])

    def test_recursive_mixed_results_and_shared_capture_facts(self):
        self.execute('&~sum(n:#u8)->(#u16,#s){n==0 ? ((#u16)0,"ABC") : '
                     '{previous=sum(n-1);((#u16)n+previous.__0,"ABC")}}'
                     '&^entry(n:#u8){p=sum(n);p.__0+(#u16)p.__1[n%3]}',
                     lambda n:[n*(n+1)//2+b'ABC'[n%3]])
        self.execute('&^entry(n:#u8){text="ABC";total:#u16=0;'
                     '&~sum(k:#u8)->#u16{k==0 ? total : {total+=(#u16)text[k%3];sum(k-1)}}sum(n);total}',
                     lambda n:[sum(b'ABC'[i%3] for i in range(1,n+1))])

    def test_static_facts_are_not_invented(self):
        cases=[
            ('&~text(n:#u8)->#s{n<128 ? "A" : "B"}&^entry(n:#u8){text(n)[0]}','contradictory'),
            ('&~wide(n:#u8)->#u64{(#u64)n+1}&^entry(n:#u8){(#u8)wide(n)}','unresolved'),
            ('&^entry(n:#u8){text:#s;&~read(k:#u8){text[k%2]}read(n)}','unresolved'),
        ]
        for source,status in cases:
            for schedule,branch in support.ORDERS:
                with self.subTest(source=source,schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-static-rejection-') as temp:
                    path=Path(temp)/'source.vx';path.write_text(source)
                    result=support.run([support.EXE,path,'--source','entry','--schedule',schedule,'--branch-order',branch])
                    self.assertEqual(result.returncode,1,result.stdout+result.stderr)
                    self.assertEqual(json.loads(result.stdout)['status'],status,result.stdout)

if __name__=='__main__':unittest.main(verbosity=2)
