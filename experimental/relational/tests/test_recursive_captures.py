#!/usr/bin/env python3
"""Recursive interfaces close from equations or explicit state, without mode search."""
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support

READ='&^entry(n:#u8){offset:#u16=3;&~count(k:#u8)->#u16{k==0?offset:count(k-1)+1}count(n)}'
OUTPUT='&^entry(n:#u8){x:#u16;&~fill(k:#u8)->#u16{k==0?{x=17;(#u16)0}:{part:#u16=fill(k-1);x+=1;part+1}}answer=fill(n);(answer,x)}'
EXPLICIT='&~count(k:#u8,state:#u16)->#u16{k==0?state:count(k-1,state+1)}&^entry(n:#u8){count(n,11)}'
PAIR='&~fill(k:#u8)->(#u16,#u16){k==0?((#u16)0,(#u16)17):{part=fill(k-1);(part.__0+1,part.__1+1)}}&^entry(n:#u8){fill(n)}'

class RecursiveInterfaceTests(unittest.TestCase):
    execute=support.TupleTests.execute
    check=support.TupleTests.check

    def test_base_path_inference_and_explicit_results(self):
        for result in ('', '->#u16'):
            self.execute('&~count(k:#u8)'+result+'{k==0?(#u16)0:count(k-1)+1}&^entry(n:#u8){count(n)}',lambda n:[n])
        self.check('&~count(k:#u8,state:#u16)->#u16{k==0?state:count(k-1,state+1)}a=count(12,11);','23')

    def test_closed_conservative_captures(self):
        self.execute(READ,lambda n:[n+3])
        self.execute(OUTPUT,lambda n:[n,n+17],2)

    def test_explicit_accumulator_and_returned_state(self):
        self.execute(EXPLICIT,lambda n:[n+11])
        self.execute(PAIR,lambda n:[n,n+17],2)

    def test_mutual_recursion_with_explicit_state(self):
        self.execute('&~a(k:#u8,s:#u16)->#u16{k==0?s:b(k-1,s+1)}'
                     '&~b(k:#u8,s:#u16)->#u16{k==0?s:a(k-1,s+1)}'
                     '&^entry(n:#u8){a(n,0)}',lambda n:[n])

    def test_invariant_shape_survives_accumulator_generalization(self):
        self.execute('&~count(k:#u8,state:#u16,size:#u8)->#u16{'
                     'data:#u8[size]=[1,2];k==0?state:count(k-1,state+data[0],size)}'
                     '&^entry(n:#u8){count(n,11,2)}',lambda n:[n+11])
        self.execute('&~count(k:#u8,example:#u16)->#[example]{'
                     'value:#[example]=k;k==0?value:count(k-1,example)+1}'
                     '&^entry(n:#u8){count(n,0)}',lambda n:[n])

    def test_missing_capture_interface_requires_source_evidence(self):
        for source in (
            '&^entry(n:#u8){unused:#Missing;&choose(x:#s){unused}&choose(x:#u8){x}'
            '&~count(k:#u8)->#u16{k==0?(#u16)0:{choose(k);count(k-1)+1}}count(n)}',
            '&^entry(n:#u8){x:#u16;&~count(k:#u8)->#u16{k==0?x:count(k-1)+1}count(n)}',
        ):
            self.check(source,status='unresolved',query='value:entry')
        # An unrelated lexical declaration is harmless; there is no invented
        # value or implicit initialization of that declaration.
        self.execute('&^entry(n:#u8){unused:#Missing;&~count(k:#u8)->#u16{'
                     'k==0?(#u16)0:count(k-1)+1}count(n)}',lambda n:[n])

    def test_open_result_has_an_actionable_interface_diagnostic(self):
        with tempfile.TemporaryDirectory(prefix='vexel-recursive-interface-') as temp:
            path=Path(temp)/'source.vx';path.write_text('&~f(k:#u8){f(k)}&^entry(n:#u8){f(n)}')
            for schedule,branch in support.ORDERS:
                result=support.run([support.EXE,path,'--source','entry','--schedule',schedule,'--branch-order',branch])
                self.assertEqual(result.returncode,1,result.stdout+result.stderr)
                report=json.loads(result.stdout);self.assertEqual(report['status'],'unresolved')
                self.assertTrue(any('return annotation' in text for text in report['interface_inputs']),report)
        # A declared interface establishes type, not constant completion.
        self.check('&~f(k:#u8)->#u16{f(k)}a=f(0);','#u16',query='type:a')
        self.check('&~f(k:#u8)->#u16{f(k)}a=f(0);',status='unresolved')

    def test_changing_static_interface_is_rejected(self):
        self.check('&~f(k:#u8,s:#s)->#u8{k==0?(#u8)|s|:f(k-1,"x")}'
                   '&^entry(n:#u8){f(n,"")}',status='unresolved',query='value:entry')
        self.check('&~f(k:#u8,data)->#u8{k==0?(#u8)|data|:f(k-1,[(#u8)1,2,3])}'
                   '&^entry(n:#u8){f(n,[(#u8)1,2])}',status='unresolved',query='value:entry')

    def test_shared_frame_cannot_hide_an_incompatible_recursive_edge(self):
        source='&~a(k:#u8,data)->#u8{k==0?(#u8)|data|:b(k-1,[(#u8)1,2,3])}' \
               '&~b(k:#u8,data)->#u8{k==0?(#u8)|data|:a(k-1,data)}' \
               '&^first(n:#u8){a(n,[(#u8)1,2])}&^second(n:#u8){b(n,[(#u8)1,2,3])}'
        with tempfile.TemporaryDirectory(prefix='vexel-recursive-context-') as temp:
            path=Path(temp)/'source.vx';path.write_text(source)
            for first,second in (('first','second'),('second','first')):
                for schedule,branch in support.ORDERS:
                    result=support.run([support.EXE,path,'--source',first,'--source',second,
                                        '--schedule',schedule,'--branch-order',branch])
                    self.assertEqual(result.returncode,1,result.stdout+result.stderr)
                    report=json.loads(result.stdout);self.assertEqual(report['status'],'unresolved')
                    self.assertTrue(any('fixed interface' in text for text in report['pending']),report)

if __name__=='__main__':unittest.main(verbosity=2)
