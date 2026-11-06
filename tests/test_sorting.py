#!/usr/bin/env python3
"""Sorting uses the same overloaded comparison relations as ordinary calls."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()
RECORD='#R(x:#u8);'
DESCENDING=RECORD+'&(a)#R::<(b:#R){a.x>b.x}'

class SortingTests(unittest.TestCase):
    def check(self,source,expected=None,error=None):
        with tempfile.TemporaryDirectory(prefix='vexel-sorting-') as directory:
            path=Path(directory)/'main.vx';path.write_text(source)
            for schedule in ('fifo','lifo','random'):
                result=subprocess.run([str(BINARY),str(path),'--json','--schedule',schedule],capture_output=True,text=True,timeout=30)
                if error:
                    self.assertEqual(result.returncode,1,result.stderr);self.assertIn(error,result.stderr)
                else:
                    self.assertEqual(result.returncode,0,result.stderr)
                    self.assertEqual({r['name']:r['value'] for r in json.loads(result.stdout)['roots']},expected)
                    data=json.loads(result.stdout);ids={node['id'] for node in data['nodes']}
                    for node in data['nodes']:
                        for key in ('expansion','before','exit_state','slot','region','runtime_backedge','signature','signature_origin'):
                            if key in node:self.assertIn(node[key],ids,(key,node))

    def test_comparison_override_and_copy_semantics(self):
        self.check(DESCENDING+'''^answer:#u16={items=[#R(1),#R(2),#R(3)];out:#u16=0;
            items@@{out=out*10+(#u16)_.x;};out*10+(#u16)items[0].x};''',{'answer':'3211'})
        self.check(RECORD+'''&(a)#R::<(b:#R){old:#u8=a.x;a.x=0;old<b.x}
            ^answer:#u8={out:#u8=0;[#R(2),#R(1)]@@{out=out*10+_.x;};out};''',{'answer':'12'})

    def test_stable_equal_keys(self):
        self.check('''#Item(key:#u8,id:#u8);&(a)#Item::<(b:#Item){a.key<b.key}
            ^answer:#u16={out:#u16=0;[#Item(2,1),#Item(1,2),#Item(2,3),#Item(1,4)]@@{
            out=out*10+(#u16)_.id;};out};''',{'answer':'2413'})

    def test_pure_captures_and_builtin_fallback(self):
        self.check(RECORD+'''^answer:#u16={reverse:#b=1;
            &(a)#R::<(b:#R){reverse?a.x>b.x:a.x<b.x}
            out:#u16=0;[#R(1),#R(2),#R(3)]@@{out=out*10+(#u16)_.x;};out};''',{'answer':'321'})
        self.check(DESCENDING+'''^answer:#u16={out:#u16=0;[3,2,1]@@{out=out*10+(#u16)_;};out};''',{'answer':'123'})

    def test_returns_and_breaks_use_sorted_order(self):
        self.check(DESCENDING+'''&first(){[#R(1),#R(3),#R(2)]@@{->_.x;};(#u8)0}
            ^answer:#u8=first();''',{'answer':'3'})
        self.check(DESCENDING+'''^answer:#u8={out:#u8=0;[#R(1),#R(3),#R(2)]@@{out=_.x;->|;};out};''',{'answer':'3'})

    def test_comparisons_cannot_mutate_enclosing_state(self):
        self.check(RECORD+'''^answer:#u8={hits:#u8=0;&(a)#R::<(b:#R){hits+=1;a.x<b.x}
            [#R(2),#R(1)]@@{};hits};''',error='cannot modify enclosing storage')
        self.check(RECORD+'''&!random()->#b;&(a)#R::<(b:#R){random()}
            ^answer:#u8={[#R(2),#R(1)]@@{};1};''',error='pure compile-time Boolean')

    def test_comparison_choices_join_structural_search(self):
        self.check(RECORD+'''^answer:#b=?{reverse=?(#b,[0,1]);
            &(a)#R::<(b:#R){reverse?a.x>b.x:a.x<b.x}
            out:#u16=0;[#R(1),#R(2),#R(3)]@@{out=out*10+(#u16)_.x;};?(out==321);reverse};''',{'answer':'1'})

if __name__=='__main__':unittest.main(verbosity=2)
