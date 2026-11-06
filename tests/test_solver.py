#!/usr/bin/env python3
"""Behavioral tests of the public solver, including independent worklist orders."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1) if len(sys.argv) > 1 else "build/vexel").resolve()
REPO = Path(__file__).resolve().parent.parent


class SolverTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="vexel-test-")
        self.directory = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def write(self, name, data):
        path = self.directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(data, bytes):
            path.write_bytes(data)
        else:
            path.write_text(data)
        return path

    def run_source(self, source, *args, schedule="fifo"):
        path = self.write("main.vx", source)
        return subprocess.run(
            [str(BINARY), str(path), "--json", "--schedule", schedule, *args],
            capture_output=True, text=True, timeout=20,
        )

    def solve(self, source, *args, schedules=True):
        result = None
        for schedule in ("fifo", "lifo", "random") if schedules else ("fifo",):
            run = self.run_source(source, *args, schedule=schedule)
            self.assertEqual(run.returncode, 0, f"{schedule}: {run.stderr}")
            data = json.loads(run.stdout)
            # Allocation IDs and cache statistics are intentionally not semantic.
            roots = [{k: v for k, v in root.items() if k != "node"} for root in data["roots"]]
            if result:
                self.assertEqual(roots, result[0], f"worklist order changed result: {schedule}")
            result = roots, data
        return result[1]

    def values(self, source, expected, *args):
        data = self.solve(source, *args)
        self.assertEqual({r["name"]: r["value"] for r in data["roots"]}, expected)
        return data

    def rejects(self, source, message, *args):
        for schedule in ("fifo", "lifo", "random"):
            run = self.run_source(source, *args, schedule=schedule)
            self.assertEqual(run.returncode, 1, f"{schedule}: {run.stdout}\n{run.stderr}")
            self.assertIn(message, run.stderr)
            self.assertNotIn("internal error", run.stderr)

    def test_forward_constants_and_functions(self):
        self.values("^answer:#u32 = twice(base); &twice(x) {x*2} base=21;", {"answer": "42"})

    def test_repeat_uses_iteration_facts(self):
        self.values("""
            &f(n) { i=0; total=0; (i<n)@{total+=i;i+=1;}; total }
            ^x:#u16=f(10);
            &g(){ local=0; (0)@{unknown();}; local }
            ^zero:#u8=g();
        """, {"x": "45", "zero": "0"})
        self.rejects("&^f(n:#u16){i:#u16=0;(i<n)@{i+=1;};}", "repeat condition must be known")
        self.rejects("&f(){(1)@{};} ^x:#u8={f();1};", "iteration budget", "--max-elements", "4")

    def test_runtime_loops_have_closed_regions(self):
        source = """&^f(n:#u32)->#u32{
            i:#u32=0; total:#u32=0; step:#u32=3;
            (i<n)!@{total+=step;i+=1;}; total
        }"""
        data = self.solve(source)
        self.assertEqual(data["roots"][0]["effect"], "control")
        loops = [n for n in data["nodes"] if n["rule"] == "Loop"]
        self.assertEqual(len(loops), 1)
        self.assertEqual(len(loops[0]["carried"]), 2)
        self.assertTrue({"LoopHeader", "LoopExit", "LoopValue"} <= {n["rule"] for n in data["nodes"]})
        header = next(n for n in data["nodes"] if n["rule"] == "LoopHeader")
        self.assertIn(header["runtime_backedge"], {n["id"] for n in data["nodes"]})
        self.assertNotIn(header["runtime_backedge"], header["requires"])
        self.assertIn("type", header)
        small = self.solve(source, "--call", "f", "--arg", "5")
        large = self.solve(source, "--call", "f", "--arg", "1000000")
        self.assertEqual(small["stats"]["nodes"], large["stats"]["nodes"])
        self.assertFalse(large["roots"][0]["known"])
        self.values("&^f()->#u8{(0)!@{missing();};42}", {"f": "42"})
        self.rejects("&^f(n:#u8){i=0;(i<n)!@{i+=1;};}", "loop-carried local needs a concrete type")

    def test_runtime_control_and_traps_survive_constant_results(self):
        for source in [
            "&^f()->#u8{(1)!@{};42}",
            "&^f(x:#u8)->#u8{1/x;42}",
            "&^f(x:#u8)->#u8{(#u8)1<<x;42}",
            "&^f(x:#u8)->#u8{a:#u8[2]=[4,4];a[x]=4;42}",
        ]:
            data = self.solve(source)
            self.assertTrue(data["roots"][0]["known"])
            self.assertEqual(data["roots"][0]["value"], "42")
            self.assertEqual(data["roots"][0]["effect"], "control")
        self.rejects("&f(){(1)!@{};42} ^x:#u8=f();", "constant, effect-free")

    def test_array_assignment_copies_and_sequences(self):
        self.values("""
            &copy(a){a[0]+=10;a}
            &f(){
                a:#u8[3]=[1,2,3]; b=a; c=copy(a);
                v=a[0]+={a[1]=7;5};
                v+a[0]+a[1]+b[0]+c[0]
            }
            ^result:#u8=f();
        """, {"result": "31"})
        self.values("""
            &f(){
                a:#u16[3][2]=[[1,2,3],[4,5,6]];
                i:#u8=0;
                a[{old=i;i+=1;old}][i] += {a[1][2]=20;7};
                a[0][1]+a[1][2]+i
            }
            ^result:#u16=f();
        """, {"result": "30"})
        self.rejects("&f(){a:#u8[2]=[1,2];a[2]=4;} ^x:#u8={f();1};", "out of bounds")
        self.rejects('&f(){s="ab";s[0]=65;} ^x:#u8={f();1};', "mutable array")

    def test_array_partial_information_survives_updates(self):
        data = self.solve("&^f(i:#u8)->#u8{a:#u8[3]=[4,4,4];a[i]=4;a[0]}")
        self.assertEqual(data["roots"][0]["value"], "4")
        self.assertEqual(data["roots"][0]["effect"], "control")
        data = self.solve("&^f(i:#u8)->#u8{a:#u8[3]=[1,2,3];a[1]=i;a[0]}")
        self.assertEqual(data["roots"][0]["value"], "1")

    def test_logical_compound_assignment_short_circuits(self):
        self.values("""
            &f(){x:#b=0;x&&=missing;y:#b=1;y||=missing;x||=y;x}
            ^result:#b=f();
        """, {"result": "1"})
        self.values("""
            &f(){a:#b[2]=[0,1];a[0]&&=missing;a[1]||=missing;a[0]||=a[1];a[0]}
            ^x:#b=f();
        """, {"x": "1"})

    def test_structurally_valid_dead_regions_are_not_solved(self):
        self.values("""
            ^answer:#u8 = 1 ? 42 : broken();
            unneeded = undefined;
            &broken() { undefined_function(unknown) }
        """, {"answer": "42"})

    def test_dead_syntax_still_invalid(self):
        self.rejects("^x:#u8=1; &dead(){(2]}", "mismatched delimiter")
        self.rejects('^x:#u8=1; dead="unterminated;', "unterminated literal")
        self.rejects("^x:#u8=1; &dead(){1 + ;}", "expected expression")

    def test_no_default_machine_integer(self):
        self.rejects("^x=42;", "ABI type is not concrete")
        self.values("x=999999999999999999999999999999999999;", {"x": "999999999999999999999999999999999999"}, "--root", "x")

    def test_exact_arithmetic_and_explicit_width(self):
        self.values("""
            ^wide:#u128=18446744073709551616*3;
            ^wrap:#u8=(#u8)255+1;
            ^signed_wrap:#i8=(#i8)127+1;
            ^narrow:#u8=(#u8)258;
            ^negative:#i8=-128;
        """, {"negative": "-128", "narrow": "2", "signed_wrap": "-128", "wide": "55340232221128654848", "wrap": "0"})
        self.rejects("^bad:#u8=255+1;", "not representable")
        self.rejects("^bad:#u8=-1;", "not representable")

    def test_integer_families_and_widths(self):
        self.values("^x:#u16=(#u8)200+(#u16)1000;", {"x": "1200"})
        self.rejects("^x:#u8=(#u16)1;", "without narrowing")
        self.rejects("^x:#i16=(#u8)1+(#i8)1;", "incompatible types")
        self.rejects("^x:#u0=1;", "unsupported type")

    def test_arithmetic_traps_and_shifts(self):
        self.rejects("^x:#u8=1/0;", "division by zero")
        self.rejects("^x:#u8=(#u8)1<<8;", "shift count")
        self.rejects("^x:#u8=1<<-1;", "shift count")
        self.values("^x:#u16=((#u8)1<<7)|3;", {"x": "131"})
        self.values("^x:#i8=(#i8)-128/(#i8)-1;", {"x": "-128"})

    def test_boolean_context(self):
        self.values("^a:#b=!0; ^b:#b=1&&0; ^c:#b=0||1;", {"a": "1", "b": "0", "c": "1"})
        self.rejects("^x:#u8=2?1:0;", "not representable")
        self.rejects("^x:#u8=(#u8)1?1:0;", "matching families")

    def test_short_circuit_avoids_missing_rhs(self):
        self.values("^a:#b=0&&missing; ^b:#b=1||missing;", {"a": "0", "b": "1"})

    def test_recursion_and_specialization(self):
        self.values("""
            &~fact(n){n==0 ? 1 : n*fact(n-1)}
            ^x:#u64=fact(20);
        """, {"x": "2432902008176640000"})
        self.values("&~fact(n){n==0?1:n*fact(n-1)}", {"fact": "120"}, "--call", "fact", "--arg", "5")
        self.rejects("&fact(n){n==0?1:n*fact(n-1)} ^x:#u16=fact(5);", "recursive calls require &~")

    def test_cycles_are_not_mistaken_for_runtime(self):
        self.rejects("^x:#u8=y; y=x;", "dependency cycle")
        self.rejects("&~forever(n){forever(n)} ^x:#u8=forever(1);", "dependency cycle")

    def test_nontermination_is_bounded(self):
        self.rejects("&~grow(n){grow(n+1)} ^x:#u8=grow(0);", "budget exhausted", "--max-nodes", "500")
        self.rejects("^x:#u8=1+2;", "step budget", "--max-steps", "1")

    def test_generic_invocations_have_independent_types(self):
        self.values("""
            &identity(x){x}
            ^a:#u8=identity((#u8)42);
            ^b:#s=identity("hello");
            ^c:#i16=identity((#i16)-7);
        """, {"a": "42", "b": '"hello"', "c": "-7"})

    def test_overload_resolution_uses_arguments_only(self):
        self.values("""
            &f(arg:#u8){11}
            &f(arg:#u16){22}
            &f(arg){33}
            ^x:#u8=f((#u8)1);
            ^y:#u8=f("string");
        """, {"x": "11", "y": "33"})
        self.rejects("&f(x:#u8){1} &f(x:#u16){2} ^x:#u8=f(1);", "ambiguous call")
        self.rejects("&f(x:#u8){1} ^x:#u8=f(256);", "no matching overload")
        self.rejects("&f(x){x} ^x:#u8=f();", "no matching overload")

    def test_local_stores_and_value_copy(self):
        self.values("""
            &f(){
                a:#u8=1;
                b=a;
                a=7;
                b+a
            }
            ^x:#u8=f();
        """, {"x": "8"})
        self.values("&f(){x:#u8; x=9; x} ^x:#u8=f();".replace("^x:", "^answer:"), {"answer": "9"})
        self.rejects("&f(){x:#u8; x} ^answer:#u8=f();", "uninitialized local")
        self.rejects("&f(){x=1; {x:#u8=2;}; x} ^answer:#u8=f();", "shadowing")

    def test_function_does_not_capture_caller_locals(self):
        self.rejects("&inner(){secret} &outer(){secret=42; inner()} ^answer:#u8=outer();", "undefined identifier: secret")

    def test_known_branch_stores(self):
        self.values("""
            &f(flag){x:#u8=1; flag?{x=42;}:{x=7;}; x}
            ^yes:#u8=f(1); ^no:#u8=f(0);
        """, {"no": "7", "yes": "42"})

    def test_runtime_branch_and_store_merge(self):
        data = self.solve("&^f(flag:#b)->#u8{x:#u8=1; flag?{x=42;}; x}")
        self.assertFalse(data["roots"][0]["known"])
        self.assertTrue(any(n["rule"] == "Merge" for n in data["nodes"]))
        self.values("&^f(flag:#b)->#u8{x:#u8=1; flag?{x=42;}:{x=42;}; x}", {"f": "42"})
        self.rejects("&^f(flag:#b)->#u8{x:#u8; flag?{x=42;}; x}", "uninitialized local")

    def test_runtime_branch_types(self):
        self.rejects('&^f(flag:#b){flag?"text":(#u8)42}', "branches must have the same type")
        self.values('^x:#s=1?"text":(#u8)42;', {"x": '"text"'})
        self.rejects("&^f(flag:#b)->#u8{flag?42:missing}", "undefined identifier")

    def test_runtime_exact_values_do_not_evade_representability(self):
        self.rejects("&^f(flag:#b)->#u8{flag?300:1}", "runtime exact integer")
        self.rejects("&^f(flag:#b)->#b{flag?2:0}", "runtime exact integer")
        self.rejects("&^f(flag:#b)->#u8{(#u8)(flag?300:1)}", "runtime exact integer")
        self.values("&^f(flag:#b)->#u8{flag?(#u8)42:1}", {"f": "runtime"})

    def test_runtime_string_boundaries(self):
        self.rejects('&^f(flag:#b)->#s{flag?"a":"b"}', "compile-time constants")
        self.rejects('&!send(s:#s); &^f(flag:#b){send(flag?"a":"b");}', "compile-time constants")

    def test_deferred_branches_preserve_lexical_visibility(self):
        self.values("&f(){1?{late=4;}; late=7; late} ^answer:#u8=f();", {"answer": "7"})
        self.rejects("&f(){1?{late;}; late=7; late} ^answer:#u8=f();", "undefined identifier: late")
        self.rejects("&f(){a:#u8[later]=[1]; later=1; a} ^answer:#u8[1]=f();", "undefined identifier: later")

    def test_json_semantic_references_are_closed(self):
        data = self.solve("&id(arg){arg} &^f(x:#u8)->#u8{id(x)}")
        ids = {n["id"] for n in data["nodes"]}
        for node in data["nodes"]:
            for field in ("inputs", "requires", "branches", "branch_states"):
                for target in node.get(field, []):
                    self.assertIn(target, ids, (node["id"], field, target))
            for field in ("expansion", "before", "exit_state", "slot"):
                if field in node:
                    self.assertIn(node[field], ids, (node["id"], field, node[field]))

    def test_runtime_inputs_and_known_calls_share_rules(self):
        source = "&scale(x,factor){x*factor} &^run(x:#u16)->#u16{scale(x,3)}"
        data = self.solve(source)
        self.assertEqual(data["roots"][0]["value"], "runtime")
        self.values(source, {"run": "21"}, "--call", "run", "--arg", "7")
        self.rejects("&^bad(x){x}", "parameters need concrete types")

    def test_external_effect_is_preserved_even_with_constant_result(self):
        source = "&!send(x:#u8); &^f()->#u8{send(1); 42}"
        data = self.values(source, {"f": "42"})
        self.assertEqual(data["roots"][0]["effect"], "external")
        calls = [n for n in data["nodes"] if n["rule"] == "Invoke" and n["operator"] == "send"]
        self.assertEqual(len(calls), 1)
        self.rejects("&!read()->#u8; ^x:#u8=read();", "effect-free")
        self.rejects("&!send(x:#u8); &f(){send(1); 42} ^x:#u8=f();", "effect-free")

    def test_effectful_calls_are_not_memoized(self):
        source = "&!tick(); &effect(){tick(); 1} &^f()->#u8{effect()+effect()}"
        data = self.values(source, {"f": "2"})
        calls = [n for n in data["nodes"] if n["rule"] == "Invoke" and n["operator"] == "tick"]
        self.assertEqual(len(calls), 2)

    def test_runtime_short_circuit_retains_conditional_effect(self):
        data = self.solve("&!read()->#b; &^f(x:#b)->#b{x&&read()}")
        self.assertEqual(data["roots"][0]["effect"], "external")
        self.assertTrue(any(n["rule"] == "ShortCircuit" for n in data["nodes"]))
        self.values("&^f(x:#b)->#b{x&&0}", {"f": "0"})
        self.values("&^f(x:#b)->#b{x||1}", {"f": "1"})

    def test_arrays_and_shapes(self):
        self.values("""
            size=3;
            ^a:#u8[size]=[4,5,6];
            ^b:#u8=a[1];
            ^c:#u8=|a|;
            ^empty:#u8[0]=[];
            ^matrix:#u8[2][2]=[[1,2],[3,4]];
        """, {"a": "[4, 5, 6]", "b": "5", "c": "3", "empty": "[]", "matrix": "[[1, 2], [3, 4]]"})
        self.rejects("^x:#u8[2]=[1];", "array shape mismatch")
        self.rejects("^x:#u8=[1][1];", "index out of bounds")
        self.rejects("^x:#u8=[1][-1];", "index out of bounds")
        self.rejects("^x:#u8[-1]=[];", "array size is negative")
        self.rejects("&!read()->#u8; ^x:#u8[read()]=[];", "array size requires")

    def test_unrolled_and_sorted_iteration(self):
        self.values("""
            &sum(){total:#u32=0; 1..5@{total+=_;}; total}
            &digits(){n:#u32=0; [9,1,5]@@{n=n*10+_;}; n}
            &down(){n:#u32=0; 3..0@{n=n*10+_;}; n}
            &empty(){n:#u8=7; []@{missing();}; n}
            ^a:#u32=sum(); ^b:#u32=digits(); ^c:#u32=down(); ^d:#u8=empty();
        """, {"a": "10", "b": "159", "c": "321", "d": "7"})
        self.rejects("&f(){[1]@{_=2;}; 1} ^x:#u8=f();", "iteration binding")
        self.rejects("^x:#u8[0]=1..1;", "empty range")

    def test_nested_iteration_bindings(self):
        self.values("""
            &f(){n:#u32=0; [1,2]@{outer=_; [3,4]@{n+=outer*_;};}; n}
            ^x:#u32=f();
        """, {"x": "21"})

    def test_runtime_array_iteration(self):
        source = "&sum(a){s:#u16=0; a@{s+=_;}; s} &^f(x:#u16)->#u16{sum([x,2])}"
        self.values(source, {"f": "runtime"})
        self.values(source, {"f": "9"}, "--call", "f", "--arg", "7")
        self.rejects("&^f(x:#u8){[x,2]@@{};}", "sorted iteration requires")

    def test_string_and_array_comparison(self):
        self.values("""
            ^a:#b="abc"<"abd";
            ^b:#b=[1,2]<[1,2,3];
            ^c:#b=[1,3]>[1,2];
            ^d:#u8="A\x00B"[1];
            ^e:#u8=|"A\x00B"|;
        """, {"a": "1", "b": "1", "c": "1", "d": "0", "e": "3"})

    def test_resource_snapshot_and_depfile(self):
        resource = self.write("assets/data.bin", b"A\x00\xffZ")
        depfile = self.directory / "plan.d"
        data = self.values("""
            ^n:#u32=|::assets::data.bin|;
            ^b:#u8=(::assets::data.bin)[2];
            ignored=::assets::missing.bin;
        """, {"b": "255", "n": "4"}, "--depfile", str(depfile), "--target", "build/my plan.json")
        self.assertIn(str(resource), data["dependencies"])
        self.assertEqual(len(data["dependencies"]), 2)
        self.assertIn(r"build/my\ plan.json:", depfile.read_text())
        self.assertNotIn("missing", depfile.read_text())

    def test_make_rebuilds_after_resource_change(self):
        self.write("main.vx", "^count:#u32=|::asset.bin|;")
        resource = self.write("asset.bin", b"abc")
        self.write("Makefile", (
            "all: plan.json\n"
            "plan.json: main.vx\n"
            f"\t{BINARY} $< --json --depfile plan.d --target $@ > $@\n"
            "-include plan.d\n"
        ))
        def make(*args):
            return subprocess.run(["make", *args], cwd=self.directory,
                                  capture_output=True, text=True, timeout=20)
        first = make()
        self.assertEqual(first.returncode, 0, first.stderr)
        plan = self.directory / "plan.json"
        self.assertEqual(json.loads(plan.read_text())["roots"][0]["value"], "3")
        self.assertEqual(make("-q").returncode, 0)
        resource.write_bytes(b"abcde")
        newer = max(resource.stat().st_mtime_ns, plan.stat().st_mtime_ns + 1)
        os.utime(resource, ns=(newer, newer))
        self.assertEqual(make("-q").returncode, 1)
        changed = make()
        self.assertEqual(changed.returncode, 0, changed.stderr)
        self.assertEqual(json.loads(plan.read_text())["roots"][0]["value"], "5")
        self.assertEqual(make("-q").returncode, 0)

    def test_module_dependencies(self):
        module = self.write("lib/math.vx", "&twice(arg){arg*2}")
        data = self.values("::lib::math; ^x:#u8=twice(21);", {"x": "42"})
        self.assertIn(str(module), data["dependencies"])
        self.values("::lib::math; ::lib::math; ^x:#u8=twice(21);", {"x": "42"})

    def test_grammar_errors_are_explicit(self):
        self.rejects("^x:#b=1<2<3;", "chained comparisons")
        self.rejects("^x:#u8=1?2:3?4:5;", "nested conditionals")
        self.values('::"exit 99" -> x; ^y:#u8=1;', {"y":"1"})
        self.rejects('::"echo bad" -> x; ^y:#s=x;', "--allow-processes")
        self.rejects("^x:#u8=0x;", "hex literal")
        self.rejects("^x:#f64=1.5e;", "floating exponent")

    def test_tail_returns_and_empty_functions(self):
        self.values("&f(){->42; missing;} ^x:#u8=f();", {"x": "42"})
        self.values("&^f(){}", {"f": "(no value)"})
        self.rejects("&f(){42;} ^x:#u8=f();", "cannot convert")
        self.values("&f(){1?{->42;}:0} ^x:#u8=f();", {"x": "42"})

    def test_diagnostics_include_cause_and_demand(self):
        run = self.run_source("^x:#u8=f(); &f(){missing+1}")
        self.assertEqual(run.returncode, 1)
        self.assertIn("main.vx:1:", run.stderr)
        self.assertIn("undefined identifier: missing", run.stderr)
        self.assertIn("required by", run.stderr)

    def test_graph_output(self):
        dot = self.directory / "plan.dot"
        self.values("^x:#u8=1+2;", {"x": "3"}, "--graph", str(dot))
        self.assertIn("digraph resolution", dot.read_text())
        self.assertIn("Binary +", dot.read_text())

    def test_examples(self):
        for name in ("puzzle", "runtime", "build"):
            run = subprocess.run([str(BINARY), str(REPO / "examples" / (name + ".vx")), "--json"],
                                 capture_output=True, text=True, timeout=20)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertTrue(json.loads(run.stdout)["roots"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
