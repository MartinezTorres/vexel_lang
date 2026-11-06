#!/usr/bin/env python3
"""A proven normal function result has independent type and execution demands."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_msx import EXE

class InterfaceCompletionTests(unittest.TestCase):
    def check(self,source,value=None,error=None):
        with tempfile.TemporaryDirectory(prefix='vexel-interface-completion-') as directory:
            path=Path(directory)/'main.vx';path.write_text(source)
            reports=[]
            for schedule in ('fifo','lifo','random'):
                result=subprocess.run([str(EXE),str(path),'--json','--schedule',schedule],capture_output=True,text=True,timeout=30)
                if error:
                    self.assertEqual(result.returncode,1,(schedule,result.stderr));self.assertIn(error,result.stderr)
                else:
                    self.assertEqual(result.returncode,0,(schedule,result.stderr))
                    report=json.loads(result.stdout);reports.append(report)
                    self.assertEqual(report['roots'][0]['value'],value)
            return reports

    def test_local_array_shape_is_independent_of_element_interfaces(self):
        function='&shape(){array=[unavailable(),1];data:#u8[|array|];data}'
        reports=self.check(function+'^answer:#u8={v:#[shape()]=[4,5];v[1]};','5')
        for report in reports:
            missing=[n for n in report['nodes'] if n.get('operator')=='unavailable' and n['rule']=='Invoke']
            self.assertTrue(missing)
            self.assertTrue(all(not n['demanded'] and not n['type_demanded'] for n in missing))
        self.check('&shape(){array=[[missing()],[other()]];copy=array;data:#u8[|copy|];data}^answer:#u8={v:#[shape()]=[4,5];v[1]};','5')
        self.check('&shape(){array=[missing(),1];array}^answer:#u8={v:#[shape()[0]]=7;v};',error='undefined function')

    def test_unrelated_statement_values_remain_undemanded(self):
        self.check('&make(){unavailable();(#u8)7}^answer:#u8={v:#[make()]=9;v};','9')
        self.check('&!external()->#u8;&make(){external();(#u8)7}^answer:#u8={v:#[make()]=9;v};','9')
        self.check('&make(){1/0;(#u8)7}^answer:#u8={v:#[make()]=9;v};','9')
        self.check('&make(){unavailable();(#u8)7}^answer:#u8=make();',error='undefined function')
        self.check('&make(){1/0;(#u8)7}^answer:#u8=make();',error='division by zero')

    def test_required_state_and_escaping_returns_still_contribute(self):
        self.check('&make(){count:#u8=2;count+=1;data:#u8[count];data}^answer:#u8={v:#[make()]=[4,5,6];v[2]};','6')
        self.check('&make(flag:#b){flag?{->(#u16)7;};(#u8)3}^answer:#u8={flag:#b;v:#[make(flag)]=9;(#u8)v};',error='uninitialized local: flag')
        self.check('&run($body){$body}&make(){run({->(#u16)7;});(#u8)3}^answer:#u16={v:#[make()]=300;v};','300')
        self.check('&make(){[0]@{->(#u16)7;};(#u8)3}^answer:#u16={v:#[make()]=300;v};','300')

    def test_value_execution_retains_effects_and_runtime_control(self):
        reports=self.check('&!external()->#u8;&^answer()->#u8{external();7}','7')
        self.assertTrue(all(r['roots'][0]['effect']=='external' for r in reports))
        reports=self.check('&^answer(flag:#b)->#u8{(flag)!@{};7}','7')
        self.assertTrue(all(r['roots'][0]['effect']=='control' for r in reports))
        self.check('&make(){count:#u8=0;count+=1;count}^answer:#u8=make();','1')
        self.check('&make(){count:#u8=0;[0,1]@{count+=1;};count}^answer:#u8=make();','2')

if __name__=='__main__':unittest.main(verbosity=2)
