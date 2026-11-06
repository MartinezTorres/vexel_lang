#!/usr/bin/env python3
"""Array shapes and element interfaces are independent graph obligations."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()

class ArrayPropertyTests(unittest.TestCase):
    def check(self,source,value=None,error=None):
        reports=[]
        with tempfile.TemporaryDirectory(prefix='vexel-array-properties-') as directory:
            path=Path(directory)/'main.vx';path.write_text(source)
            for schedule in ('fifo','lifo','random'):
                result=subprocess.run([str(BINARY),str(path),'--json','--schedule',schedule],capture_output=True,text=True,timeout=30)
                if error:
                    self.assertEqual(result.returncode,1,(schedule,result.stderr));self.assertIn(error,result.stderr)
                else:
                    self.assertEqual(result.returncode,0,(schedule,result.stderr))
                    report=json.loads(result.stdout);reports.append(report)
                    self.assertEqual(report['roots'][0]['value'],value)
        return reports

    def test_shape_does_not_require_element_interfaces(self):
        reports=self.check('^answer:#u8={v:#u8[|[unavailable(),1]|]=[4,5];v[1]};','5')
        for report in reports:
            self.assertTrue(any(n.get('element_type_obligations') for n in report['nodes']))
            self.assertFalse(any(n['rule']=='Invoke' and n['operator']=='unavailable' and n['demanded'] for n in report['nodes']))
        self.check('^answer:#u8={v:#u8[|[[unavailable()],[missing(),2]]|]=[4,5];v[1]};','5')
        self.check('^answer:#u8={v:#[|[unavailable(),1]|]=7;v};','7')

    def test_shapes_cross_helpers_and_local_copies(self):
        self.check('&extent(data){|data|}^answer:#u8={v:#u8[extent([unavailable(),1])]=[4,5];v[1]};','5')
        self.check('&width(){uninitialized:#u8;array=[uninitialized,1];data:#u8[|array|];data}'
                   '^answer:#u8={v:#[width()]=[4,5];v[1]};','5')
        self.check('&copy(data){other=data;|other|}'
                   '^answer:#u8={v:#u8[copy([unavailable(),1])]=[4,5];v[1]};','5')

    def test_whole_interfaces_and_values_still_require_elements(self):
        self.check('^answer:#u8={v:#[[unavailable(),1]];7};',error='undefined function: unavailable')
        self.check('^answer:#u8=|[unavailable(),1]|;',error='undefined function: unavailable')
        self.check('^answer:#u8={v:#[[{}]];7};',error='array element has no value')
        self.check('^answer:#u8={v:#[[(#u8)1,(#u16)2][0]]=257;(#u8)(v>>8)};','1')
        self.check('^answer:#u8={v:#[[(#u8)1,(#i8)2]];7};',error='incompatible types')

    def test_element_completion_precedes_array_shape(self):
        self.check('&f(){[{->(#u16)7;},unavailable()];(#u8)0}'
                   '^answer:#u16={v:#[f()]=513;v};','513')
        self.check('&f(){[{->(#u16)7;},unavailable()];(#u8)0}^answer:#u16=f();','7')

if __name__=='__main__':unittest.main(verbosity=2)
