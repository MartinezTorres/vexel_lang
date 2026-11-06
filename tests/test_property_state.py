#!/usr/bin/env python3
"""A property read crosses only state transitions proved not to change it."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()

class PropertyStateTests(unittest.TestCase):
    def check(self,source,value=None,error=None):
        with tempfile.TemporaryDirectory(prefix='vexel-property-state-') as directory:
            path=Path(directory)/'main.vx';path.write_text(source)
            for schedule in ('fifo','lifo','random'):
                result=subprocess.run([str(BINARY),str(path),'--json','--schedule',schedule],capture_output=True,text=True,timeout=30)
                if error:
                    self.assertEqual(result.returncode,1,(schedule,result.stderr));self.assertIn(error,result.stderr)
                else:
                    self.assertEqual(result.returncode,0,(schedule,result.stderr))
                    self.assertEqual(json.loads(result.stdout)['roots'][0]['value'],value)

    def test_unrelated_state_does_not_supply_shape_dependencies(self):
        for transition in ('flag?{}:{};', 'other:#u8=0;flag?{other=1;}:{other=2;};',
                           'flag!@{};', '(flag)@{};', '[flag]@{};'):
            self.check('&f(n:#u8,flag:#b){'+transition+'data:#u8[n];data}'
                       '^answer:#u8={flag:#b;v:#[f(3,flag)]=[7,8,9];v[2]};','9')

    def test_actual_branch_writes_are_required(self):
        prefix='&f(n:#u8,flag:#b){flag?{n=3;}:{n=4;};data:#u8[n];data}'
        self.check(prefix+'^answer:#u8={v:#[f(2,1)]=[7,8,9];v[2]};','9')
        self.check(prefix+'^answer:#u8={v:#[f(2,0)]=[7,8,9,10];v[3]};','10')
        self.check(prefix+'^answer:#u8={flag:#b;v:#[f(2,flag)]=[7,8,9];v[2]};',error='uninitialized local: flag')

    def test_transitive_and_implicit_writes_are_required(self):
        cases=[
            ('','&change(){n+=1;}&forward(){change()}','forward()'),
            ('&(self)#u8::change(){self+=1;}','','n.change()'),
            ('&run($x){$x}','','run({n+=1;})'),
            ('','&(a)#u16::+(b:#u16){n+=1;(#u8)a+(#u8)b}','(#u16)2+(#u16)3'),
            ('','&(a)#u16::+(b:#u16){n+=1;(#u8)a+(#u8)b}','[(#u16)2].+[(#u16)3]'),
            ('','&(a)#u16::@( $body ){n+=1;_:#u8=0;$body}','(#u16)2@{}'),
        ]
        for prefix,helpers,operation in cases:
            self.check(prefix+'&f(n:#u8,flag:#b){'+helpers+'flag?{'+operation+';};data:#u8[n];data}'
                       '^answer:#u8={flag:#b;v:#[f(3,flag)]=[7,8,9];v[2]};',error='uninitialized local: flag')

    def test_unavailable_nested_overloads_are_conservative(self):
        self.check('&f(n:#u8,flag:#b){flag?{&change(){n+=1;}change();};data:#u8[n];data}'
                   '^answer:#u8={flag:#b;v:#[f(3,flag)]=[7,8,9];v[2]};',error='uninitialized local: flag')
        self.check('&f(n:#u8,flag:#b){flag?{&(a)#u16::+(b:#u16){n+=1;(#u8)a+(#u8)b}'
                   '(#u16)2+(#u16)3;};data:#u8[n];data}'
                   '^answer:#u8={flag:#b;v:#[f(3,flag)]=[7,8,9];v[2]};',error='uninitialized local: flag')

    def test_ordinary_execution_still_requires_the_condition(self):
        self.check('&f(n:#u8,flag:#b){flag?{}:{};data:#u8[n]=[1,2,3];data[0]}'
                   '^answer:#u8={flag:#b;f(3,flag)};',error='uninitialized local: flag')

    def test_independent_obligations_survive_optional_rollback(self):
        self.check('&make(offset:#u8){#P(count:#u8,data:#u8[count+offset]);#P(2,[1,2,3])}'
                   '^answer:#u8={offset:#u8;x:#u8=1;#{v:#[make(offset).count]=7;x=v;absent();};x};','1')

if __name__=='__main__':unittest.main(verbosity=2)
