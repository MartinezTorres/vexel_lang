#!/usr/bin/env python3
"""Nominal records, inferred fields, tuples and type-only field demand."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1) if len(sys.argv) > 1 else "build/vexel").resolve()


class RecordTests(unittest.TestCase):
    def check(self, source, expected=None, error=None):
        with tempfile.TemporaryDirectory(prefix="vexel-records-") as scratch:
            path = Path(scratch) / "main.vx"
            path.write_text(source)
            for schedule in ("fifo", "lifo", "random"):
                run = subprocess.run([str(BINARY), str(path), "--json", "--schedule", schedule],
                                     capture_output=True, text=True, timeout=20)
                if error:
                    self.assertEqual(run.returncode, 1, (schedule, run.stderr))
                    self.assertIn(error, run.stderr)
                else:
                    self.assertEqual(run.returncode, 0, (schedule, run.stderr))
                    data = json.loads(run.stdout)
                    self.assertEqual({root["name"]: root["value"] for root in data["roots"]}, expected)

    def test_constructor_copy_and_update(self):
        self.check("""#Pair(left,right);
            ^answer:#u16={p=#Pair((#u8)1,(#u16)20);q=p;p.left=3;p.left+q.left};
        """, {"answer": "4"})

    def test_nested_places_and_copy(self):
        self.check("""#Inner(x:#u8); #Outer(items:#Inner[2],tag:#u16);
            ^answer:#u16={
                p=#Outer([#Inner(1),#Inner(2)],9); q=p;
                p.items[1].x+=4; p.items[1].x+q.items[1].x
            };
        """, {"answer": "8"})

    def test_nominal_identity_and_generic_access(self):
        self.check("""#A(x:#u8);#B(x:#u8); &get(p){p.x}
            ^answer:#u8=get(#A(3))+get(#B(4));
        """, {"answer": "7"})
        self.check("#A(x:#u8);#B(x:#u8); ^answer:#A=#B(1);", error="cannot convert")
        self.check("#P(a,b); &left(p:#P){p.a} ^answer:#u8={p:#P=#P((#u8)1,(#u16)2);v:#[left(p)]=9;v};", {"answer":"9"})
        self.check("#P(a,b); value:#P=#P((#u8)1,(#u16)2); ^answer:#u8={v:#[value.a]=9;v};", {"answer":"9"})
        self.check("#P(a,b); &create()->#P{(#P((#u8)1,(#u16)2))} ^answer:#u8={v:#[create().a]=9;v};", {"answer":"9"})

    def test_type_query_does_not_require_other_field(self):
        self.check("""#Pair(left,right); &first(p){p.left}
            ^answer:#u8={x:#[first(#Pair((#u8)1, unavailable()))]=42;x};
        """, {"answer": "42"})
        self.check("""#Pair(left,right); &!read()->#u16;
            ^answer:#u8={x:#[#Pair((#u8)1,read()).left]=42;x};
        """, {"answer": "42"})

    def test_multi_assignment(self):
        self.check('^answer:#u8={a,b=(2,3);a,b=(b,a);a*10+b};',{'answer':'32'})
        self.check('^answer:#u8={i:#u8=0;&pair(){i+=1;(i,i+1)} a,b=pair();a+b+i};',{'answer':'4'})
        self.check('^answer:#u8={a:#u8=1;b:#u16=2;a,b=(3,4);a+(#u8)b};',{'answer':'7'})
        self.check('&(a,b)swap(){a,b=(b,a);} ^answer:#u8={x:#u8=2;y:#u8=3;(x,y).swap();x*10+y};',{'answer':'32'})
        self.check('^answer:#u8={a,b=(1,2,3);a};',error='arity mismatch')
        self.check('#Pair(a,b); ^answer:#u8={a,b=#Pair(1,2);a};',error='requires a tuple')
        self.check('^answer:#u8={a:#u8=1;b:#u8=2;a,b=(999,3);a};',error='not representable')

    def test_tuples_and_declared_result(self):
        self.check("""&parts(x:#u16)->(#u16,#u16){(x/10,x%10)}
            ^answer:#u16={p=parts(42);p.__0+p.__1};
        """, {"answer": "6"})
        self.check("^answer:#u8={p=((#u8)3,(#u16)4);p.__0=7;p.__0};", {"answer": "7"})

    def test_scoped_and_forward_type_declarations(self):
        self.check("^answer:#u8={p=#Local(3);#Local(x:#u8);p.x};", {"answer": "3"})

    def test_constructor_expressions_and_tuple_cast_grammar(self):
        self.check('#P(x:#u8);&make(){#P(3)}^answer:#u8=(#P(2).x+make().x);',{'answer':'5'})
        self.check('#P(x:#u8);^answer:#u8={#P(2);#P(3).x};',{'answer':'3'})
        self.check('^answer:#u16={raw:#u8[3]=[1,2,3];p=((#u8,#u16))raw;p.__1};',{'answer':'770'})
        self.check('^answer:#u8=(((#u8)1,2)).__0;',{'answer':'1'})

    def test_records_in_constraints(self):
        self.check("""#Pair(a,b);
            ^answer:#u8=?{
                p=#Pair(?(#u8,1..5),?(#u8,1..5));
                ?(p.a+p.b==5);?(p.a*p.b==4);?(p.a<p.b);p.a
            };
        """, {"answer": "1"})


if __name__ == "__main__":
    unittest.main(verbosity=2)
