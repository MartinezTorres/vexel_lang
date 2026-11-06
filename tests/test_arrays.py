#!/usr/bin/env python3
"""Broadcasting uses trailing shape alignment and evaluates operands once."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv.pop(1) if len(sys.argv)>1 else 'build/vexel').resolve()
class ArrayTests(unittest.TestCase):
    def check(self,source,value=None,error=None):
        with tempfile.TemporaryDirectory(prefix='vexel-arrays-') as scratch:
            path=Path(scratch)/'main.vx';path.write_text(source)
            for schedule in ('fifo','lifo','random'):
                run=subprocess.run([str(BINARY),str(path),'--json','--schedule',schedule],capture_output=True,text=True,timeout=30)
                if error:
                    self.assertEqual(run.returncode,1,run.stderr);self.assertIn(error,run.stderr)
                else:
                    self.assertEqual(run.returncode,0,run.stderr)
                    self.assertEqual(json.loads(run.stdout)['roots'][0]['value'],value)

    def test_trailing_alignment_scalar_and_singleton(self):
        self.check('^answer:#u8[3][2]=[[1,2,3],[4,5,6]] .+ [10,20,30];','[[11, 22, 33], [14, 25, 36]]')
        self.check('^answer:#u8[3][2]=[[1],[2]] .* [3,4,5];','[[3, 4, 5], [6, 8, 10]]')
        self.check('^answer:#u8[2]=10 .- [1,2];','[9, 8]')
        self.check('^answer:#b[2]=[1,2] .< [2,1];','[1, 0]')

    def test_compound_and_single_evaluation(self):
        self.check('^answer:#u8={i:#u8=0;a:#u8[2]=[1,2];a .+= (i+=1);a[0]+a[1]+i};','6')
        self.check('^answer:#u8={i:#u8=0;a=[(i+=1),i] .+ [3,4];a[0]+a[1]+i};','10')

    def test_logic_is_not_short_circuit(self):
        self.check('^answer:#u8={i:#u8=0;a=[0,1] .&& [(i=1)==1,1];i+a[0]+a[1]};',error='incompatible types')
        self.check('^answer:#b[2]=[0,1] .&& [1,1];','[0, 1]')
        self.check('^answer:#b[2]=[0,1] .|| [1/0,1];',error='division by zero')

    def test_shape_errors_and_type_queries(self):
        self.check('^answer:#u8[3]=[1,2] .+ [1,2,3];',error='incompatible broadcast shapes')
        self.check('^answer:#u8={a:#u8[2];b:#[a .+ 1]=[3,4];b[0]};','3')

    def test_symbolic_broadcast(self):
        self.check('^answer:#u8[2]=?{a=?(#u8[2],1..4);b=a .+ [2,3];?(b[0]==4);?(b[1]==6);a};','[2, 3]')

    def test_empty_shapes_keep_type_without_evaluating_samples(self):
        self.check('^answer={a:#u8[0]=[];a .+ (#u8)2};','[]')
        self.check('^answer={a:#u8[3][0]=[];a .+ [(#u8)1,2,3]};','[]')
        self.check('^answer={a:#u8[0]=[];a ./ (#u8)0};','[]')
        self.check('^answer:#u8={i:#u8=0;a:#u8[0]=[];a .+ (i+=1);i};','1')

    def test_scalar_operator_methods_lift_over_record_arrays(self):
        prefix='#P(x:#u8);&(p)#P::+(q:#P){p.x+=q.x;(#P(p.x))}'
        self.check(prefix+'^answer:#P[2]=[#P(1),#P(2)] .+ #P(3);','[[4], [5]]')
        self.check(prefix+'^answer:#u8={a=[#P(1),#P(2)];b=a .+ #P(3);a[0].x+b[1].x};','6')
        self.check(prefix+'^answer:#u8={a=[#P(1),#P(2)];a .+= #P(3);a[0].x+a[1].x};','9')
        self.check(prefix+'^answer:#u8[2]=[1,2] .+ 3;','[4, 5]')

    def test_lifted_methods_thread_captures_in_element_order(self):
        self.check('#P(x:#u8);^answer:#u8={count:#u8=0;&(p)#P::+(q:#P){count+=1;(#P(p.x+q.x+count))} '
                   'a=[#P(1),#P(2)] .+ #P(3);a[0].x*10+a[1].x+count};','59')
        self.check('#P(x:#u8);^answer:#u8={count:#u8=0;&(p)#P::+(q:#P){count+=1;(#P(p.x+q.x))} '
                   'a=[#P(1),#P(2)];a .+= #P(3);count+a[0].x+a[1].x};','11')

    def test_lifted_method_types_and_empty_shapes_skip_calls(self):
        prefix='#P(x:#u8);&(p)#P::+(q:#P){p.x+q.x}'
        self.check(prefix+'^answer:#u8={a:#P[2];b:#[a .+ #P(3)]=[5,7];b[1]};','7')
        self.check('#P(x:#u8);&(p)#P::/(q:#P){p.x/q.x} ^answer={a:#P[0]=[];a ./ #P(0)};','[]')

    def test_explicit_dotted_overload_precedes_scalar_lifting(self):
        self.check('#P(x:#u8);&(p)#P::+(q:#P){(#P(99))}&(a:#P[2]).+(b:#P){[#P(4),#P(5)]} '
                   '^answer:#P[2]=[#P(1),#P(2)] .+ #P(3);','[[4], [5]]')

if __name__=='__main__':unittest.main(verbosity=2)
