#!/usr/bin/env python3
"""Lexical callable bindings and unevaluated argument substitution."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()
class FunctionTests(unittest.TestCase):
    def check(self, source, expected=None, error=None):
        with tempfile.TemporaryDirectory(prefix='vexel-functions-') as scratch:
            path=Path(scratch)/'main.vx';path.write_text(source)
            for schedule in ('fifo','lifo','random'):
                run=subprocess.run([str(BINARY),str(path),'--json','--schedule',schedule],capture_output=True,text=True,timeout=30)
                if error:
                    self.assertEqual(run.returncode,1,run.stderr);self.assertIn(error,run.stderr)
                else:
                    self.assertEqual(run.returncode,0,run.stderr)
                    self.assertEqual({r['name']:r['value'] for r in json.loads(run.stdout)['roots']},expected)

    def test_nested_capture_by_reference(self):
        self.check('^answer:#u8={x:#u8=1;&add(v:#u8){x+=v;x} a=add(2);b=add(3);a+b+x};',{'answer':'15'})
        self.check('^answer:#u8={&read(){x} x:#u8=7;read()};',{'answer':'7'})
        self.check('^answer:#u8={&read(){x} a=read();x:#u8=7;a};',error='uninitialized')

    def test_nested_overloads_and_nonleaking_names(self):
        self.check('^answer:#u8={&f(x:#u8){x+1}&f(x:#u16){(#u8)(x+2)} f((#u8)1)+f((#u16)2)};',{'answer':'6'})
        self.check('^answer:#u8={ {&private(){1}};private()};',error='undefined function')

    def test_parameters_cannot_shadow_enclosing_bindings(self):
        self.check('^answer:#u8={x:#u8=1;&f(x:#u8){x} f(2)};',error='parameter shadows')
        self.check('^answer:#u8={&outer(x:#u8){&inner(x:#u8){x}inner(2)}outer(1)};',error='parameter shadows')
        self.check('&outer($x){&f($x){$x} f(2)} ^answer:#u8=outer(1);',error='parameter shadows')
        self.check('^answer:#u8={x:#u8=1;&(x:#u8)f(){x}(2).f()};',error='parameter shadows')

    def test_deferred_arguments_are_fresh_and_hygienic(self):
        self.check('&twice($a){$a;$a} ^answer:#u8={x:#u8=0;twice(x+=1);x};',{'answer':'2'})
        self.check('&use($a){x:#u8=9;$a} ^answer:#u8={x:#u8=3;use(x)};',{'answer':'3'})
        self.check('&ignore($a){42} ^answer:#u8=ignore(1/0);',{'answer':'42'})
        self.check('&choose(c:#b,$a,$b){c?$a:$b} ^answer:#u8=choose(1,7,1/0);',{'answer':'7'})

    def test_expression_forwarding_and_value_order(self):
        self.check('&twice($a){$a;$a}&forward($b){twice($b)} ^answer:#u8={x:#u8=0;forward(x+=1);x};',{'answer':'2'})
        self.check('&mix($a,b:#u8){$a+b} ^answer:#u8={x:#u8=0;mix(x+=1,x+=5)+x};',{'answer':'17'})

    def test_typed_expression_checks_and_query(self):
        self.check('&use($a:#u8){$a} ^answer:#u8=use(8);',{'answer':'8'})
        self.check('&use($a:#u8){$a} ^answer:#u8=use(999);',error='not representable')
        self.check('&use($a:#u8){$a;42} ^answer:#u8=use(999);',error='not representable')
        self.check('&use($a){$a} ^answer:#u8={x:#u8;y:#[use(x)]=9;y};',{'answer':'9'})

    def test_pure_captures_in_constraints(self):
        self.check('^answer:#u8=?{x=?(#u8,1..6);&condition(){x*x==9} ?(condition());x};',{'answer':'3'})
        self.check('&choose(c:#b,$a,$b){c?$a:$b} ^answer:#u8=?{x=?(#u8,[0,1]);value=choose(x==0,7,1/0);value};',{'answer':'7'})

    def test_runtime_recursive_interfaces(self):
        self.check('#P(x:#u8);&~walk(n:#u8)->#P{(n==0)?#P(7):walk(n-1)}&^answer(n:#u8)->#u8{walk(n).x}',{'answer':'runtime'})
        self.check('&~f(n:#u8){n==0?n:n+f(n-1)} &^run(n:#u8)->#u8{f(n)}',{'run':'runtime'})
        self.check('&~f(n:#u8,a:#u16)->#u16{n==0?a:f(n-1,a+1)} &^run(n:#u8)->#u16{f(n,0)}',{'run':'runtime'})
        self.check('&~a(n:#u8)->#b{n==0?1:b(n-1)} &~b(n:#u8)->#b{n==0?0:a(n-1)} &^run(n:#u8)->#b{a(n)}',{'run':'runtime'})

    def test_recursive_component_requires_materialization(self):
        self.check('&~a(n:#u8)->#u8{n==0?0:b(n-1)} &b(n:#u8)->#u8{a(n)} &^run(n:#u8)->#u8{a(n)}',error='recursive calls require &~')

    def test_recursive_capture_constant_and_mutual_runtime(self):
        self.check('^answer:#u8={x:#u8=5;&~f()->#u8{x==0?0:{x-=1;f()+1}} f()};',{'answer':'5'})
        self.check('&^run(n:#u8)->#u8{x:#u8=0;&~a(k:#u8)->#u8{k==0?x:{x+=1;b(k-1)}} &~b(k:#u8)->#u8{a(k)} a(n)}',{'run':'runtime'})

    def test_recursive_finite_region_revises_choices(self):
        self.check('&~f(n:#u8)->#u8{n==0?0:n+f(n-1)} ^answer:#u8=?{n=?(#u8,0..5);?(f(n)==6);n};',{'answer':'3'})

    def test_recursive_optional_revision_ownership(self):
        self.check('&~f(n:#u8,a:#u8)->#u8{n==0?a:{#{a=f(n-1,a+1);};a}} &^run(n:#u8)->#u8{f(n,0)}',{'run':'runtime'})

if __name__=='__main__':unittest.main(verbosity=2)
