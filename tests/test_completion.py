#!/usr/bin/env python3
"""Completion and state are independent of normal expression values."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()
class CompletionTests(unittest.TestCase):
    def check(self, source, expected=None, error=None):
        with tempfile.TemporaryDirectory(prefix='vexel-completion-') as scratch:
            path=Path(scratch)/'main.vx';path.write_text(source)
            for schedule in ('fifo','lifo','random'):
                run=subprocess.run([str(BINARY),str(path),'--json','--schedule',schedule],capture_output=True,text=True,timeout=30)
                if error:
                    self.assertEqual(run.returncode,1,run.stderr);self.assertIn(error,run.stderr)
                else:
                    self.assertEqual(run.returncode,0,run.stderr)
                    self.assertEqual({r['name']:r['value'] for r in json.loads(run.stdout)['roots']},expected)

    def test_return_stops_remaining_demand(self):
        self.check('&f(){->7;undefined();} ^answer:#u8=f();',{'answer':'7'})
        self.check('&f(x){x?{->4;}: {->8;}; 1/0;} ^answer:#u8=f(0)+f(1);',{'answer':'12'})
        self.check('&f(){x:#u8; {->3;};x} ^answer:#u8=f();',{'answer':'3'})
        self.check('&f(){({->7;})+(missing+=1)} ^answer:#u8=f();',{'answer':'7'})
        self.check('&take(a,b){a+b} &f(){take({->7;},missing+=1)} ^answer:#u8=f();',{'answer':'7'})
        self.check('&f(){[ {->7;}, missing+=1 ]} ^answer:#u8=f();',{'answer':'7'})

    def test_returns_close_only_their_function(self):
        self.check('&f(){&inner(){->2;} x=inner();->x+5;} ^answer:#u8=f();',{'answer':'7'})
        self.check('&f(){x:#u8=1;&inner(){x+=3;->2;} y=inner();x+y} ^answer:#u8=f();',{'answer':'6'})

    def test_conditional_completion_keeps_normal_state(self):
        self.check('&^f(c:#b)->#u8{x:#u8;c?{->3;}: {x=7;};x}',{'f':'runtime'})
        self.check('&^f(c:#b)->#u8{c?{->3;}: {->7;};undefined();}',{'f':'runtime'})

    def test_return_annotation_checks_each_exit(self):
        self.check('&^f(c:#b)->#u8{c?{->999;};7}',error='not representable')
        self.check('&^f(c:#b){c?{->;};(#u8)7}',error='type')

    def test_finite_choices_with_early_returns(self):
        self.check('&f(x){x==0?{->7;};->9;} ^answer:#u8=?{x=?(#u8,[0,1]);?(x==0);f(x)};',{'answer':'7'})

    def test_unrolled_break_continue_and_return(self):
        self.check('&f(){s:#u8=0;0..9@{_==2?{->>;};_==5?{->|;};s+=_;};s} ^answer:#u8=f();',{'answer':'8'})
        self.check('&f(){0..9@{_==3?{->7;};};1} ^answer:#u8=f();',{'answer':'7'})
        self.check('&f(){s:#u8=0;0..3@{0..9@{_==2?{->|;};s+=1;};};s} ^answer:#u8=f();',{'answer':'6'})

    def test_repeat_completion(self):
        self.check('&f(){i:#u8=0;s:#u8=0;(i<9)@{i+=1;i==2?{->>;};i==5?{->|;};s+=i;};s} ^answer:#u8=f();',{'answer':'8'})
        self.check('&f(){i:#u8=0;(i<9)@{i+=1;i==3?{->7;};};1} ^answer:#u8=f();',{'answer':'7'})

    def test_loop_control_is_lexical(self):
        self.check('&f(){->|;} ^answer:#u8=f();',error='no enclosing loop')
        self.check('&f(){0..2@{&g(){->>;}g();};7} ^answer:#u8=f();',error='no enclosing loop')

    def test_type_only_return_does_not_read_payload(self):
        self.check('&f(x){->x;} ^answer:#u8={x:#u8;y:#[f(x)]=3;y};',{'answer':'3'})
        self.check('&f(x){x/0;->x;} ^answer:#u8={x:#u8;y:#[f(x)]=3;y};',{'answer':'3'})
        self.check('&f(c,x){c?{->x;};x} ^answer:#u8={x:#u8;y:#[f((#b)0,x)]=3;y};',{'answer':'3'})
        self.check('&f(c,x){c?{->x;};x} ^answer:#u8={x:#u8;c:#b;y:#[f(c,x)]=3;y};',{'answer':'3'})
        self.check('&f(c){c?{->(#u8)3;};(#u16)7} ^answer:#u8={y:#[f(1)]=3;y};',{'answer':'3'})

    def test_expression_completion_retains_caller_target(self):
        self.check('&use($a){$a;undefined();} &f(){use({->7;});9} ^answer:#u8=f();',{'answer':'7'})
        self.check('&use($a){$a;} &f(){s:#u8=0;0..4@{use({_==2?{->>;};});s+=_;};s} ^answer:#u8=f();',{'answer':'4'})
        self.check('&choose(c,$a){c?$a:0} &f(c){choose(c,{->7;});9} ^answer:#u8=?{c=?(#b,[0,1]);?(c);f(c)};',{'answer':'7'})
        self.check('&each($a){0..4@{$a;};} &forward($b){each($b);} &f(){forward({->7;});undefined();} ^answer:#u8=f();',{'answer':'7'})

if __name__=='__main__':unittest.main(verbosity=2)
