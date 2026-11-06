#!/usr/bin/env python3
"""Type properties are observable independently of values and effects."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1) if len(sys.argv) > 1 else "build/vexel").resolve()


class TypeTests(unittest.TestCase):
    def solve(self, source, expected):
        results = []
        with tempfile.TemporaryDirectory(prefix="vexel-types-") as scratch:
            path = Path(scratch) / "main.vx"
            path.write_text(source)
            for schedule in ("fifo", "lifo", "random"):
                run = subprocess.run([str(BINARY), str(path), "--json", "--schedule", schedule],
                                     text=True, capture_output=True, timeout=20)
                self.assertEqual(run.returncode, 0, (schedule, run.stderr))
                data = json.loads(run.stdout)
                self.assertEqual({root["name"]: root["value"] for root in data["roots"]}, expected)
                results.append(data)
        return results

    def test_uninitialized_type_is_available(self):
        self.solve("^answer:#u16={uninitialized:#u16; x:#[uninitialized]=7;x};", {"answer": "7"})

    def test_arithmetic_is_not_executed_for_type(self):
        self.solve("^answer:#u8={x:#[1/0]=42;x};", {"answer": "42"})
        self.solve("^answer:#u8={x:#[[1,2][900]]=42;x};", {"answer": "42"})

    def test_external_signature_without_value_demand(self):
        for data in self.solve("&!read()->#u16; ^answer:#u16={x:#[read()]=42;x};", {"answer": "42"}):
            calls = [n for n in data["nodes"] if n["rule"] == "Invoke" and n["operator"] == "read"]
            self.assertEqual(len(calls), 1)
            self.assertFalse(calls[0]["demanded"])
            self.assertEqual(calls[0]["type"], "#u16")

    def test_generic_body_uses_argument_types(self):
        self.solve("&twice(x){x+x} ^answer:#u8={a:#[twice(1/0)]=42;a};", {"answer": "42"})
        self.solve("&identity(x){x} &!read()->#u16; ^answer:#u16={a:#[identity(read())]=42;a};", {"answer": "42"})

    def test_forward_global_type_dependencies(self):
        self.solve("first:#[second]=7; ^answer:#u8=first; second:#u8=1/0;", {"answer": "7"})

    def test_global_type_alias_cycles_join_initializer_types(self):
        for declarations in ("a:#[b]=(#u8)255;b:#[a]=(#u16)2;", "b:#[a]=(#u16)2;a:#[b]=(#u8)255;"):
            self.solve(declarations+"^answer:#u16=a+1;", {"answer":"256"})
        self.solve("a:#[b]=1;b:#[a]=2;^answer:#u8=a+b;", {"answer":"3"})
        self.solve("a:#[a]=(#u8)7;^answer:#u8=a;", {"answer":"7"})
        self.solve("a:#[b]=(#u16)2;b:#[c]=(#u8)255;c:#[b]=1;^answer:#u16=b+1;", {"answer":"256"})
        self.solve("a:#[b]=[1,2];b:#[a]=[(#u8)3,(#u8)4];^answer:#u8=a[1]+b[1];", {"answer":"6"})
        self.solve('#P(n);a:#[b]=#P((#u8)255);b:#[a]=#P((#u16)2);^answer:#u16=a.n+1;', {'answer':'256'})
        self.solve('#P(n);b:#[a]=#P((#u16)2);a:#[b]=#P((#u8)255);^answer:#u16=a.n+1;', {'answer':'256'})

    def test_alias_type_queries_do_not_execute_initializers(self):
        self.solve("a:#[b]=1/0;b:#[a]=(#u8)2;^answer:#u8={v:#[a]=7;v};", {"answer":"7"})
        self.solve("a:#[b];b:#[a]=(#u8)2;^answer:#u8={v:#[a]=7;v};", {"answer":"7"})

    def test_alias_anchors_and_value_cycles_remain_constraints(self):
        for source,error in (("a:#[b]=(#u16)2;b:#u8=3;^answer:#u8=a;", "without narrowing"),
                             ("a:#[b]=300;b:#[a]=(#u8)2;^answer:#u8=b+a;", "not representable"),
                             ("a:#[b]=b;b:#[a]=a;^answer:#u8=a;", "unresolved dependency"),
                             ("a:#[b];b:#[a];^answer:#u8={v:#[a]=7;v};", "unresolved dependency")):
            with tempfile.TemporaryDirectory(prefix="vexel-alias-errors-") as scratch:
                path=Path(scratch)/"main.vx";path.write_text(source)
                for schedule in ("fifo","lifo","random"):
                    run=subprocess.run([str(BINARY),str(path),"--schedule",schedule],text=True,capture_output=True,timeout=20)
                    self.assertEqual(run.returncode,1,run.stderr);self.assertIn(error,run.stderr)

    def test_query_branch_selection(self):
        self.solve("^answer:#u8={x:#[1 ? 2 : unknown()]=42;x};", {"answer": "42"})
        self.solve("&choose(x){(x==1)?(#u8)2:unknown()} ^answer:#u8={x:#[choose(1)]=42;x};", {"answer": "42"})

    def test_queried_array_shape(self):
        self.solve("^answer:#u16={a:#[[(#u16)1,(#u16)2]]=[3,4];a[1]};", {"answer": "4"})

    def test_shape_reads_only_its_value_dependencies(self):
        for body, argument in (("data:#u8[n]=[1,2,3];data[0]", "3"),
                               ("data:#u8[n+1]=[1,2,3];data[0]", "2"),
                               ("n+=1;data:#u8[n]=[1,2,3];data[0]", "2")):
            self.solve("&f(n:#u8,unused:#u8){" + body + "} "
                       "^answer:#u8={unused:#u8;v:#[f(" + argument + ",unused)]=7;v};", {"answer": "7"})
        self.solve("&f(n:#u8,unused:#u8){data:#u8[n]=[1,2,3];data[0]} "
                   "^answer:#u8={unused:#u8;n:#u8=2;v:#[f(n+1,unused)]=7;v};", {"answer": "7"})
        self.solve("&f(n:#u8[3],unused:#u8){data:#u8[|n|]=[1,2,3];data[0]} "
                   "^answer:#u8={unused:#u8;v:#[f([1/0,2,3],unused)]=7;v};", {"answer": "7"})

    def test_shape_dependencies_cross_inline_helpers(self):
        for helper in ('&plus(n:#u8,unused:#u8){n+1}',
                       '&plus(n:#u8,unused:#u8)->#u8{n+=1;n}',
                       '&inner(n:#u8,unused:#u8){n+1}&plus(n:#u8,unused:#u8){inner(n,unused)}'):
            self.solve(helper+'&f(n:#u8,unused:#u8){data:#u8[plus(n,unused)]=[1,2,3];data[0]}'
                       '^answer:#u8={unused:#u8;v:#[f(2,unused)]=7;v};', {'answer':'7'})
        self.solve('&f(n:#u8,unused:#u8){data:#u8[{unused+1;n}]=[1,2,3];data[0]}'
                   '^answer:#u8={unused:#u8;v:#[f(3,unused)]=7;v};', {'answer':'7'})

    def test_projected_helpers_keep_required_values_and_execution_strict(self):
        for invocation in ('f(n,2)', 'plus(2,n)'):
            source='&plus(n:#u8,unused:#u8){n+1}&f(n:#u8,unused:#u8){data:#u8[plus(n,unused)]=[1,2,3];data[0]}'
            expression='v:#['+invocation+']=7;v' if invocation.startswith('f') else invocation
            with tempfile.TemporaryDirectory(prefix='vexel-property-call-errors-') as scratch:
                path=Path(scratch)/'main.vx';path.write_text(source+'^answer:#u8={n:#u8;'+expression+'};')
                for schedule in ('fifo','lifo','random'):
                    result=subprocess.run([str(BINARY),str(path),'--schedule',schedule],capture_output=True,text=True,timeout=20)
                    self.assertEqual(result.returncode,1,result.stderr)
                    self.assertIn('uninitialized local: n',result.stderr)

    def test_shape_helpers_with_expression_parameters(self):
        self.solve('&width($x,unused:#u8){$x+1}&f(n:#u8,unused:#u8){data:#u8[width(n,unused)]=[1,2,3];data[0]}'
                   '^answer:#u8={unused:#u8;v:#[f(2,unused)]=7;v};', {'answer':'7'})
        self.solve('&forward($x,$y,unused:#u8){$x+$y}&f(n:#u8,unused:#u8){data:#u8[forward(n,1,unused)]=[1,2,3];data[0]}'
                   '^answer:#u8={unused:#u8;v:#[f(2,unused)]=7;v};', {'answer':'7'})

    def test_property_projection_preserves_required_value_errors(self):
        prefix = "&f(n:#u8,unused:#u8){data:#u8[n]=[1,2,3];data[0]} "
        for expression, name in (("n:#u8;v:#[f(n,2)]=7;v", "n"),
                                 ("unused:#u8;f(3,unused)", "unused")):
            with tempfile.TemporaryDirectory(prefix="vexel-property-errors-") as scratch:
                path = Path(scratch) / "main.vx"
                path.write_text(prefix + "^answer:#u8={" + expression + "};")
                for schedule in ("fifo", "lifo", "random"):
                    run = subprocess.run([str(BINARY), str(path), "--schedule", schedule],
                                         text=True, capture_output=True, timeout=20)
                    self.assertNotEqual(run.returncode, 0)
                    self.assertIn("read of uninitialized local: " + name, run.stderr)

    def test_typed_calls_do_not_read_arithmetic_arguments(self):
        self.solve("&twice(x:#u8){x+x} ^answer:#u8={a:#[twice(1/0)]=42;a};", {"answer": "42"})
        self.solve("&first(x:#u8[2]){x[0]} ^answer:#u8={a:#[first([1/0,2])]=42;a};", {"answer": "42"})

    def test_recursive_signature_without_argument_values(self):
        prefix = "&~f(n:#u8){n==0?(#u8)1:n*f(n-1)} "
        self.solve(prefix + "^answer:#u8={n:#u8;x:#[f(n)]=7;x};", {"answer": "7"})
        self.solve(prefix + "^answer:#u8={x:#[f(1/0)]=7;x};", {"answer": "7"})
        self.solve('#P(x:#u8);&~walk(n:#u8)->#P{(n==0)?#P(7):walk(n-1)}'
                   '^answer:#u8={n:#u8;x:#[walk(n).x]=9;x};', {'answer':'9'})

    def test_mutually_recursive_type_equations(self):
        self.solve("&~a(n:#u8){n==0?(#b)1:b(n-1)} &~b(n:#u8){n==0?(#b)0:a(n-1)} "
                   "^answer:#b={x:#[a(1/0)]=1;x};", {"answer": "1"})

    def test_type_projection_does_not_replace_constant_execution(self):
        self.solve("&~f(n:#u8){n==0?(#u8)1:n*f(n-1)} a:#[b]=7; ^answer:#u8=a+b; b=f(5);", {"answer": "127"})
        self.solve("&~f(n:#u8){n==0?1:n*f(n-1)} ^answer:#u8={x:#[f(5)]=7;f(5)+x};", {"answer": "127"})

    def test_recursive_query_preserves_type_selecting_conditions(self):
        self.solve("&~f(n:#u8){n==0?1:(#u16)f(n-1)} ^answer:#u16={x:#[f(0)]=70000;y:#[f(2)]=42;(#u16)x+y};", {"answer": "4506"})

    def test_recursive_record_shape_revises_signature_assumption(self):
        self.solve("#Box(item); &~f(n:#u8){n==0?#Box(1):#Box(f(n-1))} "
                   "^answer:#u8={x:#[f(2)]=#Box(#Box(#Box(7)));x.item.item.item};", {"answer": "7"})
        self.solve("#Box(item); &~f(n:#u8){n==0?#Box(7):#Box(f(n-1))} "
                   "^answer:#u8={x:#[f(2)]=f(2);x.item.item.item};", {"answer": "7"})
        self.solve("&~f(n:#u8){n==0?[1]:(n==1?[1,2]:f(n-1))} "
                   "^answer:#u8={x:#[f(3)]=[4,7];x[1]};", {"answer": "7"})


if __name__ == "__main__":
    unittest.main(verbosity=2)
