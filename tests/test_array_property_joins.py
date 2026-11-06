#!/usr/bin/env python3
"""Common array interfaces expose independent nested shape/field properties."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()


class ArrayPropertyJoinTests(unittest.TestCase):
    def check(self,source,value=None,error=None,finite=False):
        with tempfile.TemporaryDirectory(prefix='vexel-array-property-joins-') as directory:
            path=Path(directory)/'main.vx';path.write_text(source)
            for schedule in ('fifo','lifo','random'):
                for branch in (('forward','reverse') if finite else ('forward',)):
                    result=subprocess.run([str(BINARY),str(path),'--json','--schedule',schedule,
                        '--branch-order',branch],capture_output=True,text=True,timeout=30)
                    if error:
                        self.assertEqual(result.returncode,1,(schedule,branch,result.stderr));self.assertIn(error,result.stderr)
                    else:
                        self.assertEqual(result.returncode,0,(schedule,branch,result.stderr))
                        self.assertEqual(json.loads(result.stdout)['roots'][0]['value'],value)

    def test_nested_shapes_do_not_demand_leaf_interfaces(self):
        self.check('^answer:#u8={v:#u8[|[[missing(),other()],[unavailable(),0]][0]|]=[4,5];v[1]};','5')
        self.check('^answer:#u8={v:#u8[|[[[missing()]],[ [other()] ]][0][0]|]=[7];v[0]};','7')
        self.check('^answer:#u8={v:#u8[|[[],[]][0]|]=[];|v|};','0')
        self.check('^answer:#u8={v:#u8[|[[missing()],[other(),0]][0]|];0};',error='incompatible types')

    def test_fields_join_all_supplied_element_types(self):
        self.check('#P(x,y);^answer:#u32={v:#[[#P((#u8)1,missing()),'
            '#P((#u16)2,other()),#P((#u32)3,unavailable())][0].x]=65536;v};','65536')
        self.check('^answer:#u16={v:#[[((#u8)1,missing()),((#u16)2,other())][0].__0]=513;v};','513')
        self.check('#P(x);^answer:#u8={v:#[[#P((#u8)1),#P((#i8)1)][0].x];7};',error='incompatible types')

    def test_whole_interfaces_and_values_close_remaining_obligations(self):
        prefix='^answer:#u8={k:#u8=1;#P(x:#u8,data:#u8[x+k]);a=#P(1,[1,2]);k=2;b=#P(1,[1,2,3]);'
        self.check(prefix+'v:#[[a,b][0].x]=7;v};','7')
        self.check(prefix+'v:#[[a,b][0]];7};',error='different captured schema')
        self.check(prefix+'[a,b][0].x};',error='different captured schema')
        self.check('#P(x,y);^answer:#u8={v:#[[#P(1,missing()),#P(2,other())][0]];7};',error='undefined function')
        self.check('^answer:#u8={v:#[[[1,2],[3,4]][0]]=[5,6];v[1]};','6')

    def test_properties_survive_optional_and_structural_revisions(self):
        self.check('#P(x,y);^answer:#u8={r:#u8=7;#{v:#[[#P(1,missing()),#P(2,other())][0].x]=3;r=v;missing();};r};','7')
        self.check('#P(x,y);^answer:#u16=?{n=?(#b,[0,1]);'
            'v:#[[n?#P((#u8)1,missing()):#P((#u16)2,other()),#P((#u8)3,unavailable())][0].x]=7;(#u16)v};','7',finite=True)


if __name__=='__main__':unittest.main(verbosity=2)
