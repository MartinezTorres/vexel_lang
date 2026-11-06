#!/usr/bin/env python3
"""Loop completion proofs must not execute an invocation's unknown arguments."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()

class LoopTypeTests(unittest.TestCase):
    def check(self,source,expected=None,error=None):
        with tempfile.TemporaryDirectory(prefix='vexel-loop-types-') as directory:
            path=Path(directory)/'main.vx';path.write_text(source)
            for schedule in ('fifo','lifo','random'):
                result=subprocess.run([str(BINARY),str(path),'--json','--schedule',schedule,'--max-nodes','20000'],capture_output=True,text=True,timeout=30)
                if error:
                    self.assertEqual(result.returncode,1,result.stderr);self.assertIn(error,result.stderr)
                else:
                    self.assertEqual(result.returncode,0,result.stderr)
                    self.assertEqual({r['name']:r['value'] for r in json.loads(result.stdout)['roots']},expected)

    def test_return_types_of_each_loop_form(self):
        for body in ('[x]@{->x;};(#u8)0', '(x>0)!@{->x;};x', '(x>0)@{->x;};x', '[x]!@{->x;};(#u8)0', '[x]@@{->x;};(#u8)0'):
            self.check('&f(x:#u8){'+body+'} ^answer:#u8={u:#u8;y:#[f(u)]=7;y};',{'answer':'7'})

    def test_break_and_continue_do_not_read_values(self):
        for token in ('->|;','->>;'):
            for loop in ('[x]@','[x]!@','(x>0)@','(x>0)!@'):
                self.check('&f(x:#u8){'+loop+'{'+token+'};x} ^answer:#u8={u:#u8;y:#[f(u)]=7;y};',{'answer':'7'})

    def test_empty_regions_and_dead_continuations(self):
        self.check('&f(items:#u8[0]){items@{->missing;};(#u8)3} ^answer:#u8={a:#u8[0];x:#[f(a)]=7;x};',{'answer':'7'})
        self.check('&f(x:#u8){[x]@{->x;};missing} ^answer:#u8={u:#u8;y:#[f(u)]=7;y};',{'answer':'7'})
        self.check('&f(x:#u8){(1)!@{->x;};missing} ^answer:#u8={u:#u8;y:#[f(u)]=7;y};',{'answer':'7'})

    def test_type_proof_then_value_resolution(self):
        self.check('&f(x:#u8){[x]@@{-> _+1;};(#u8)0} ^answer:#u8={y:#[f(1/0)]=f(7);y};',{'answer':'8'})
        self.check('&f(x:#u8){(x>0)@{->x+1;};(#u8)0} ^answer:#u8={u:#u8;y:#[f(u)]=f(7);y};',{'answer':'8'})

    def test_external_values_are_not_executed_for_types(self):
        self.check('&!read()->#u8; &f(){[read()]@{->_;};(#u8)0} ^answer:#u8={x:#[f()]=7;x};',{'answer':'7'})
        self.check('&!read()->#u8; &f(){(read()>0)!@{->read();};(#u8)0} ^answer:#u8={x:#[f()]=7;x};',{'answer':'7'})

    def test_indirect_record_cycles_have_a_specific_error(self):
        self.check('#A(b:#B);#B(a:#A);^answer:#u8={x:#A;1};',error='recursive record types')
        self.check('#A(b:(#u8,#B));#B(a:#A[0]);^answer:#u8={x:#A;1};',error='recursive record types')

if __name__=='__main__':unittest.main(verbosity=2)
