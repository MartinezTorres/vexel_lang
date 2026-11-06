#!/usr/bin/env python3
"""Receiver designations are evaluated once and preserve overlapping aliases."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()
PRELUDE='#P(x:#u8,y:#u8); &(p)#P::add(v:#u8){p.x+=v;p.x} '
class MethodTests(unittest.TestCase):
    def check(self, source, expected=None, error=None):
        with tempfile.TemporaryDirectory(prefix='vexel-methods-') as scratch:
            path=Path(scratch)/'main.vx';path.write_text(source)
            for schedule in ('fifo','lifo','random'):
                run=subprocess.run([str(BINARY),str(path),'--json','--schedule',schedule],capture_output=True,text=True,timeout=30)
                if error:
                    self.assertEqual(run.returncode,1,run.stderr);self.assertIn(error,run.stderr)
                else:
                    self.assertEqual(run.returncode,0,run.stderr)
                    self.assertEqual(json.loads(run.stdout)['roots'][0]['value'],expected)

    def test_mutation_and_temporary_receiver(self):
        self.check(PRELUDE+'^answer:#u8={p=#P(1,2);p.add(3)+p.x};','8')
        self.check(PRELUDE+'^answer:#u8=#P(1,2).add(3);','4')
        self.check(PRELUDE+'^answer:#u8={p=#P(1,2);q=p;q.add(3);p.x};','1')

    def test_nested_receiver_and_designation_once(self):
        self.check(PRELUDE+'#Q(p:#P);^answer:#u8={q=#Q(#P(1,2));q.p.add(3);q.p.x};','4')
        self.check(PRELUDE+'^answer:#u8={a=[#P(1,2),#P(3,4)];i:#u8=0;a[i+=1].add(5);a[1].x+i};','9')

    def test_multiple_receivers_and_overlap(self):
        self.check('&(a,b)merge(){a+=1;b+=a;a+b} ^answer:#u8={x:#u8=2;(x,x).merge()+x};','18')
        self.check('#P(x:#u8);&(a:#P,b:#u8)change(){a.x+=1;b+=a.x;b} ^answer:#u8={p=#P(2);(p,p.x).change()+p.x};','12')

    def test_non_evaluating_method_probes(self):
        self.check(PRELUDE+'^answer:#b={p:#P;p.?add(1/0)};','1')
        self.check(PRELUDE+'^answer:#b=#P(1,2).?absent(1/0);','0')
        self.check(PRELUDE+'^answer:#b=#P(1,2).?add("wrong");','0')
        self.check(PRELUDE+'&(p)#P::add(v:#u16){p.x} ^answer:#b=#P(1,2).?add(1);',error='ambiguous')

    def test_namespaces_and_method_type_query(self):
        self.check(PRELUDE+'&add(x:#u8){x+1} ^answer:#u8={p=#P(1,2);x:#[p.add(1)]=9;add(x)};','10')
        self.check('#P(x:#u8);&(p)#P::get(ignored){p.x}^answer:#u8={p=#P(1);p.get(p.x=7)};','7')

    def test_operator_methods_and_builtin_fallback(self):
        self.check('#P(x:#u8);&(p)#P::+(q:#P){(#P(p.x+q.x))}^answer:#u8={p=#P(2)+#P(3);p.x+1};','6')
        self.check('#P(x:#u8);&(p)#P::==(q:#P){p.x==q.x}^answer:#b=#P(2)==#P(2);','1')
        self.check('#P(x:#u8);&(p)#P::-( ){(#P(0-p.x))}^answer:#u8={q=-#P(2);q.x};','254')
        self.check('#P(x:#u8);&(p)#P::+(q:#P){p.x+=q.x;(#P(p.x+1))}^answer:#u8={p=#P(2);p+=#P(3);p.x};','6')
        self.check('#P(x:#u8);&(p)#P::+(q:#P){(#P(p.x+q.x))}^answer:#u8={a=[#P(2),#P(3)];i:#u8=0;a[i+=1]+=#P(4);a[1].x+i};','8')

    def test_iteration_methods_and_body_hygiene(self):
        source='#Bag(items:#u8[3]);&(b)#Bag::@($body){i:#u8=0;(i<3)@{_=b.items[i];$body;i+=1;};}'
        self.check(source+'^answer:#u8={sum:#u8=0;bag=#Bag([3,1,2]);bag@{sum+=_;};sum};','6')
        self.check(source+'^answer:#u8={sum:#u8=0;[1,2,3]@{sum+=_;};sum};','6')
        self.check(source+'^answer:#u8={bag=#Bag([3,1,2]);bag@{_=9;};1};',error='iteration binding')

    def test_operator_declaration_interfaces(self):
        self.check('#P(x:#u8);&(p)#P::+($q){p}^answer:#u8=1;',error='cannot declare expression parameters')
        self.check('&(a,b)+(x){x}^answer:#u8=1;',error='one receiver')
        self.check('#P(x:#u8);&(p)#P::*( ){p}^answer:#u8=1;',error='value-argument arity')

    def test_temporary_receiver_conversions_preserve_declared_arithmetic(self):
        self.check('&(x:#u8)invert(){~x} ^answer:#u8=(1).invert();','254')
        self.check('&(x:#u16)next(){x+=1;x} ^answer:#u16=((#u8)255).next();','256')
        self.check('&!source()->#u8;&(x:#u16)next(){x+=1;x} ^answer:#u16={y:#[source().next()]=7;y};','7')
        self.check('^answer:#u8={&(x:#u8)invert(){~x}(1).invert()};','254')
        self.check('^answer:#u16={&(x:#u16)next(){x+=1;x}((#u8)255).next()};','256')

    def test_bound_receiver_types_cannot_change(self):
        self.check('&(x:#u16)next(){x+=1;x} ^answer:#u16={y:#u8=255;y.next()};',error='no matching overload')
        self.check('&(x:#u16)next(){x+=1;x} ^answer:#b={y:#u8;y.?next()};','0')

    def test_logical_overloads_and_builtin_laziness(self):
        operator='#P(x:#u8);&(p)#P::&&(q:#P){(#P(p.x+q.x))}'
        self.check(operator+'^answer:#u8=(#P(2)&&#P(3)).x;','5')
        self.check(operator+'^answer:#b=0 && undefined;','0')
        self.check(operator+'^answer:#b=1 || (1/0);','1')
        self.check('&(p:#b)&&(q:#s){p} ^answer:#b=(#b)0 && (1/0);','0')
        self.check('&(p:#b)&&(q:#u8){p} ^answer:#b=(#b)0 && 300;','0')
        self.check('&(p:#b)&&(q:#u8){p} ^answer:#b=(#b)0 && (1/0);',error='division by zero')

    def test_logical_overload_receiver_compounds_and_eager_order(self):
        operator='#P(x:#u8);&(p)#P::&&(q:#P){(#P(p.x+q.x))}'
        self.check(operator+'^answer:#u8={p=#P(2);p&&=#P(3);p.x};','5')
        self.check(operator+'#Q(p:#P);^answer:#u8={q=#Q(#P(2));q.p&&=#P(4);q.p.x};','6')
        self.check(operator+'^answer:#u8={a=[#P(1),#P(2)];i:#u8=0;a[i+=1]&&=#P(4);a[1].x+i};','7')
        self.check(operator+'^answer:#u8={p=#P(2);q=p && {p.x=7;(#P(3))};q.x};','10')

    def test_logical_overload_independent_type_query(self):
        self.check('#P(x:#u8);&(p)#P::&&(q:#P){p.x+q.x} ^answer:#u8={p:#P;q:#P;v:#[p&&q]=7;v};','7')

if __name__=='__main__':unittest.main(verbosity=2)
