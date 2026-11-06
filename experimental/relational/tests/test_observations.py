#!/usr/bin/env python3
"""Observable uniqueness follows demanded native outputs and boundary effects."""
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support

class ObservationTests(unittest.TestCase):
    execute=support.TupleTests.execute

    def check(self,source,status,flags=('--source','entry','--budget','8')):
        with tempfile.TemporaryDirectory(prefix='vexel-observation-') as temp:
            path=Path(temp)/'source.vx';path.write_text(source)
            for schedule,branch in support.ORDERS:
                with self.subTest(source=source,schedule=schedule,branch=branch):
                    result=support.run([support.EXE,path,*flags,'--schedule',schedule,'--branch-order',branch])
                    self.assertIn(result.returncode,(0,1),result.stdout+result.stderr)
                    report=json.loads(result.stdout);self.assertEqual(report['status'],status,report)

    def test_native_and_recursive_result_ambiguity(self):
        for marker in ('&','&~'):
            self.check(marker+'f(n:#u8){?{p=?(#u8,[0,1]);n+p}}&^entry(n:#u8){f(n)}','ambiguous')
        self.check('&~g(n:#u8){?{p=?(#u8,[0,1]);n+p}}&~h(n:#u8){g(n)}&^entry(n:#u8){h(n)}','ambiguous')
        self.check('p=?{x=?(#u16,[0,1]);x};&~f(n:#u8)->#u16{n==0?p:f(n-1)+1}&^entry(n:#u8){f(n%8)}','ambiguous')
        self.check('&^entry(n:#u8){x=n;&~f(k:#u8){x=?{p=?(#u8,[0,1]);k+p};}f(n);x}','ambiguous')

    def test_partial_and_unused_return_slots(self):
        self.check('&~f(n:#u8){?{p=?(#u8,[0,1]);n+p}}&^entry(n:#u8){f(n);n}','resolved')
        self.check('&~f(n:#u8){?{p=?(#u8,[0,1]);n}}&^entry(n:#u8){f(n)}','resolved')
        self.check('&^entry(n:#u8){x=n;&~f(k:#u8){x=?{p=?(#u8,[0,1]);k+p};}f(n);n}','resolved')
        self.check('p=?{x=?(#b,[0,1]);x};&~f(n:#u8){p?(#u8)n:(#u16)n}&^entry(n:#u8){f(n);n}','resolved')
        self.check('p=?{x=?(#b,[0,1]);x};&~f(n:#u8){p?n:(#u8[2])[n,n]}&^entry(n:#u8){f(n);n}','resolved')
        self.check('p=?{x=?(#b,[0,1]);x};&^entry(n:#u8){x=n;&~f(k:#u8){x=k+1;'
                   'p?(#u8)0:(#u8[2])[0,0]}f(n);x}','resolved')
        self.execute('p=?{x=?(#b,[0,1]);x};&^entry(n:#u8){x=p?(#u8)n:(#u16)n;'
                     '&~f(k:#u8){out:#[x]=k;out}f(n);n}',lambda n:[n])
        prefix='&~f(n:#u8){?{p=?(#u8,[0,1]);(n,n+p)}}'
        self.check(prefix+'&^entry(n:#u8){f(n).__0}','resolved')
        self.check(prefix+'&^entry(n:#u8){f(n).__1}','ambiguous')
        self.execute(prefix+'&^entry(n:#u8){f(n).__0}',lambda n:[n])

    def test_effects_empty_results_and_persistent_initializers(self):
        prefix='p=?{x=?(#u8,[0,1]);x};&!emit(n:#u8);'
        for tail in ('','(#u8[0])[]','(#u8)7'):
            self.check(prefix+'&^entry(){emit(p);'+tail+'}','ambiguous')
        self.check(prefix+'&^entry(){emit(p);}','ambiguous',('--query','value:entry'))
        self.check(prefix+'a={emit(p);(#u8[0])[]};','ambiguous',('--query','value:a'))
        self.check('p=?{x=?(#u8,[0,1]);x};&!input()->#u8;value=input()+p;&^entry(){value}','ambiguous')
        self.check('p=?{x=?(#u8,[0,1]);x};state:#u8;&^entry(){state=p;}','ambiguous')

    def test_internal_identities_are_not_observations(self):
        prefix='p=?{x=?(#b,[0,1]);x};&~g(n:#u8){n+1}&~h(n:#u8){n+1}'
        self.check(prefix+'&^entry(n:#u8){p?g(n):g(n)}','resolved')
        self.execute(prefix+'&^entry(n:#u8){p?g(n):h(n)}',lambda n:[(n+1)&255])

    def test_type_queries_and_unreached_definitions(self):
        prefix='&~f(n:#u8){?{p=?(#u8,[0,1]);n+p}}'
        self.check(prefix+'a={n:#u8;f(n)};','resolved',('--query','type:a'))
        self.check(prefix+'&^entry(n:#u8){n}','resolved')
        self.check('&!emit(n:#u8);&~f(n:#u8)->#u8{?{p=?(#u8,[0,1]);p};emit(n);n}'
                   'a={n:#u8;f(n)};','resolved',('--query','type:a'))

if __name__=='__main__':unittest.main(verbosity=2)
