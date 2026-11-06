#!/usr/bin/env python3
"""Capture requirements follow selected semantic bodies and reaching facts."""
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support

METHODS='#P(x:#u8);#Q(x:#u8);&^entry(n:#u8){unused:#Missing;count:#u8=0;' \
        '&(p)#P::+(q:#P){count+=1;#P(p.x+q.x)}&(p)#Q::+(q:#Q){unused;#Q(0)}'

class CaptureClosureTests(unittest.TestCase):
    execute=support.TupleTests.execute

    def test_selected_overloads_and_nested_native_calls(self):
        self.execute('&~(x:#u16)init(n:#u8){x=(#u16)n+300;}&^entry(n:#u8){x:#u16;x.init(n);x}',lambda n:[n+300])
        self.execute(METHODS+'&~helper(k:#u8){#P(k)+#P(k);}helper(n);count}',lambda n:[1])
        self.execute(METHODS+'&~leaf(k:#u8){#P(k)+#P(k);}&~helper(k:#u8){leaf(k);}helper(n);count}',lambda n:[1])
        self.execute('&^entry(n:#u8){unused:#Missing;answer:#u16;'
                     '&bad(x:#u8){unused;}&bad(x:#u16){answer=x;}'
                     '&~helper(k:#u8){bad((#u16)k+300);}helper(n);answer}',lambda n:[n+300])

    def test_static_facts_and_changing_captured_values(self):
        self.execute('&^entry(n:#u8){text="x";text="abc";&~helper(k:#u8){'
                     '#P(data:#u8[|text|]);p=#P([k,k,k]);|p.data|}helper(n)}',lambda n:[3])
        self.execute('&^entry(n:#u8){flag:#b=0;a:#u8=17;b:#u8=29;'
                     '&~helper(k:#u8){flag?a+k:b+k}x=helper(n);flag=1;y=helper(n);(x,y)}',
                     lambda n:[(n+29)&255,(n+17)&255],2)
        self.execute('&^entry(n:#u8){unused:#Missing;&~helper(k:#u8,flag:#b){flag?unused:k}helper(n,0)}',lambda n:[n])
        self.execute('&^entry(n:#u8){unused:#u8;&~helper(k:#u8){x:#[unused]=k;x}helper(n)}',lambda n:[n])

    def test_required_captures_remain_obligations(self):
        for source in (
            '&^entry(n:#u8){missing:#Missing;&~helper(k:#u8){missing;k}helper(n)}',
            '&^entry(n:#u8){missing:#u8;&~helper(k:#u8){missing+k}helper(n)}',
            '&^entry(n:#u8){missing:#u8;flag:#b=0;&~helper(k:#u8){flag?missing:k}helper(n)}',
        ):
            for schedule,branch in support.ORDERS:
                with self.subTest(source=source,schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-capture-rejection-') as temp:
                    path=Path(temp)/'source.vx';path.write_text(source)
                    result=support.run([support.EXE,path,'--source','entry','--schedule',schedule,'--branch-order',branch])
                    self.assertEqual(result.returncode,1,result.stdout+result.stderr)
                    self.assertEqual(json.loads(result.stdout)['status'],'unresolved',result.stdout)

    def test_inspectable_selection(self):
        with tempfile.TemporaryDirectory(prefix='vexel-capture-proof-') as temp:
            path=Path(temp)/'source.vx';path.write_text(METHODS+'&~helper(k:#u8){#P(k)+#P(k);}helper(n);count}')
            result=support.run([support.EXE,path,'--source','entry']);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            report=json.loads(result.stdout)
            proofs=[p for p in report['capture_proofs'] if p['function']=='helper' and p['status']=='resolved']
            self.assertTrue(proofs,report)
            self.assertTrue(any(set(p['candidates'])=={'unused','count'} and p['selected']==['count'] for p in proofs),proofs)
            self.assertEqual([f['captures'] for f in report['runtime_functions']],[1])

if __name__=='__main__':unittest.main(verbosity=2)
