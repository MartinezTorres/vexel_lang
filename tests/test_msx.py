#!/usr/bin/env python3
"""Executable backend tests: graph -> C behavior, SDCC objects, megalinker ROM.

The host tests exercise C semantics, not MSX hardware emulation. Toolchain tests
are skipped only when SDCC/sdasz80 are unavailable; `make test-msx` checks them.
"""
import ctypes
import json
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

EXE = Path(sys.argv.pop(1)).resolve()
REPO = Path(__file__).resolve().parent.parent

FIXED_SOURCE = r"""
&^fixed_mul_bits(a:#u32,b:#u32)->#u32 {
    x=(#i16.16)((#u8[4])a);y=(#i16.16)((#u8[4])b);(#u32)((#u8[4])(x*y))
}
&^fixed_div_bits(a:#u32,b:#u32)->#u32 {
    x=(#i16.16)((#u8[4])a);y=(#i16.16)((#u8[4])b);(#u32)((#u8[4])(x/y))
}
&^fixed_fraction_bits(a:#u32,b:#u32)->#u32 {
    x=(#u0.32)((#u8[4])a);y=(#u0.32)((#u8[4])b);(#u32)((#u8[4])(x*y))
}
&^fixed_rescale(x:#i16)->#i16 {a=(#i8.8)x/2;(#i16)a}
&^fixed_branch(x:#i16,c:#b)->#i16 {a=(#i8.8)x/2;b=c?a:2;(#i16)(b*2)}
#FixedRecord(a:#i8.8,b:#i8.8);
&^fixed_record(x:#i16)->#i16 {p=#FixedRecord((#i8.8)x/2,2);(#i16)(p.a+p.b)}
&^fixed_array(x:#i16)->#i16 {a=[(#i8.8)x/2,2];b=(#i4.4[2])a;(#i16)(b[0]+b[1])}
&^fixed_compare(x:#i16)->#b {((#i8.8)x)/2<2}
&~fixed_step(n:#u8,a:#i8.8)->#i8.8{n==0?a:fixed_step(n-1,a/2)}
&^fixed_call(n:#u8)->#i16{(#i16)fixed_step(n,4)}
"""

BASE_SOURCE = r"""
&!again()->#b;
&^noarg_dynamic()->#u16{x:#u16=0;&~f()->#u16{again()?{x+=1;f()}:x}f()}
&~noarg_param_body(n:#u16)->#u16{again()?noarg_param_body(n+1):n}
&^noarg_generalized()->#u16{noarg_param_body(0)}

&^capture_recursion(n:#u16)->#u16 {
 x:#u16=0;
 &~walk(k:#u16)->#u16 {k==0?x:{x+=k;walk(k-1)}}
 y=walk(n);x+y
}
&^capture_after(n:#u16)->#u16 {
 x:#u16=0;
 &~walk(k:#u16)->#u16 {k==0?x:{y=walk(k-1);x+=k;x+y}}
 y=walk(n);x+y
}
#Cell(x:#u16);
&~(p)#Cell::count(k:#u16)->#u16 {k==0?p.x:{p.x+=k;p.count(k-1)}}
&^receiver_recursion(n:#u16)->#u16 {p=#Cell(0);y=p.count(n);y+p.x}
&~recursive_sum_body(n:#u16)->#u16 {n==0?0:n+recursive_sum_body(n-1)}
&~recursive_acc_body(n:#u16,a:#u16)->#u16 {n==0?a:recursive_acc_body(n-1,a+n)}
&~recursive_inferred_body(n:#u16) {n==0?n:n+recursive_inferred_body(n-1)}
&~recursive_even_body(n:#u16)->#b {n==0?1:recursive_odd_body(n-1)}
&~recursive_odd_body(n:#u16)->#b {n==0?0:recursive_even_body(n-1)}
&~recursive_shape_body(n:#u16,k:#u8)->#u16 {
    a:#u8[k]=[1,2,3]; n==0?(#u16)|a|:recursive_shape_body(n-1,k)+1
}
&^recursive_sum(n:#u16)->#u16 {recursive_sum_body(n)}
&^recursive_acc(n:#u16)->#u16 {recursive_acc_body(n,0)}
&^recursive_inferred(n:#u16)->#u16 {recursive_inferred_body(n)}
&^recursive_even(n:#u16)->#b {recursive_even_body(n)}
&^recursive_shape(n:#u16)->#u16 {recursive_shape_body(n,3)}
&^recursive_export(n:#u16)->#u16 {n==0?0:n+recursive_export(n-1)}
&early(c:#b,x:#u16)->#u16{c?{->x+4;};x+8}
&^tuple_assign(a:#u16,b:#u16)->#u16{a,b=(b,a);a*3+b}
&^tuple_loop(a:#u16,b:#u16)->#u16{i:#u8=0;(i<3)!@{a,b=(b,a);i+=1;};a*3+b}
&^early_call(c:#b,x:#u16)->#u16{early(c,x)+early(!c,x)}
&^early_value(c:#b)->#u16{v=(c?{->7;}:3)+5;v}
&^early_both(c:#b)->#u16{c?{->7;}: {->9;};undefined();}
&^early_capture(c:#b)->#u16{x:#u16=0;&change()->#u16{c?{x=7;->2;};x=9;4} y=change();x+y}
&^unroll_exit(n:#u8)->#u16{s:#u16=0;0..10@{_==3?{->>;};_>=n?{->|;};s+=(#u16)_;};s}
&^runtime_exit(n:#u8)->#u16{s:#u16=0;((#u8)0)..10!@{_==3?{->>;};_>=n?{->|;};s+=(#u16)_;};s}
&^runtime_return(n:#u8)->#u16{((#u8)0)..10!@{_==n?{->(#u16)_*2;};};42}
&^repeat_exit(n:#u8)->#u16{i:#u8=0;s:#u16=0;(i<10)@{i+=1;i>n?{->|;};s+=(#u16)i;};s}
#Point(x:#u16,y:#u16);
&~(p)#Point::materialized_bump(n:#u16){p.x+=n;p.x}
&^materialized_method(x:#u16,i:#u8)->#u16 {p=[#Point(x,0),#Point(2,0)];p[i].materialized_bump(3);p[i].x}
&^materialized_capture(x:#u16)->#u16 {
    y:#u16=3; &~inner(n:#u16)->#u16 {y+=n;x+=1;y+x}
    a=inner(2); b=inner(4); a+b+y+x
}
&~materialized_array_body(a){a[0]+=5;a}
&^materialized_array(x:#u16)->#u16 {a=materialized_array_body([x,2,3]);a[0]+a[1]}
&(p)#Point::bump(n:#u16){p.x+=n;p.x}
&(p)#Point::+(q:#Point){p.x+=q.x;(#Point(p.x,p.y))}
&(p)#Point::&&(q:#Point){p.x+=q.x;(#Point(p.x,p.y))}
&^logical_method(x:#u16)->#u16{p=#Point(x,0);p&&=#Point(3,0);p.x}
&^logical_field(x:#u16)->#u16{p=[#Point(x,0),#Point(2,0)];p[0]&&=#Point(3,0);p[0].x}
&^lifted_method(x:#u16)->#u16{a=[#Point(x,0),#Point(2,0)];b=a .+ #Point(3,0);a[0].x+b[0].x+b[1].x}
&^lifted_capture(x:#u16)->#u16{n:#u16=0;&(p)#Cell::+(q:#Cell){n+=1;(#Cell(p.x+q.x+n))}a=[#Cell(x),#Cell(2)] .+ #Cell(3);a[0].x+a[1].x+n}
#Bag(items:#u16[3]);
&(bag)#Bag::@($body){i:#u8=0;(i<3)@{_=bag.items[i];$body;i+=1;};}
&repeat(n:#u8,$body){i:#u8=0;(i<n)!@{$body;i+=1;};}
&^macro_loop(x:#u16,n:#u8)->#u16{sum:#u16=x;repeat(n,sum+=3);sum}
&^custom_iter(x:#u16)->#u16{sum:#u16=0;bag=#Bag([x,2,3]);bag@{sum+=_;};sum}
&^operator_loop(x:#u16)->#u16{p=#Point(x,0);i:#u8=0;(i<3)!@{p+#Point(2,0);i+=1;};p.x}
&^method_call(x:#u16,i:#u8)->#u16 {p=[#Point(x,0),#Point(2,0)];p[i].bump(3);p[i].x}
&^method_loop(x:#u16)->#u16 {p=#Point(x,0);i:#u8=0;(i<3)!@{p.bump(2);i+=1;};p.x}
&twice($a){$a;$a}
&^expressions(x:#u16)->#u16 {twice(x+=3);x}
&^captured(x:#u16)->#u16 {&inc(){x+=2;} inc();inc();x}
&^captured_loop(x:#u16)->#u16 {&inc(){x+=2;} i:#u8=0;(i<3)!@{inc();i+=1;};x}
&^broadcast(x:#u16,i:#u8)->#u16 {a:#u16[3]=[x,2,3];b=a .+ [4,5,6];b[i]}
#Record(data:#u16[3],count:#u16);
&!record_exchange(r:#Record)->#Record;
&!bytes_sum(s:#s)->#u16;
&^external_record(x:#u16)->#u16 {r=record_exchange(#Record([x,2,3],4));r.data[0]+r.count}
&^binary_text()->#u16 {bytes_sum("A\0B")}
#Wire(a:#u9,b:#i7);
&^bit_round(x:#u16)->#u16 {bits=(#b[16])x;bits[3]=!bits[3];(#u16)bits}
&^wire_byte(x:#u16,i:#u8)->#u8 {r=#Wire((#u9)x,-2);raw=(#u8[3])r;raw[i]}
&^wire_decode(x:#u16)->#u16 {raw:#u8[3]=[(#u8)x,(#u8)(x>>8),126];r=(#Wire)raw;(#u16)r.a+(#u16)(#u8)r.b}
&^boolean_bytes(x:#u8)->#u8 {raw:#u8[2]=[0,x];bits=(#b[2])raw;(#u8)((#u2)bits)}
&^discard_boolean_bytes(x:#u8)->#u8 {raw:#u8[1]=[x];(#b[1])raw;7}
^public_number:#u16=513;
^public_array:#u16[3]=[1,257,65535];
^public_record:#Record=#Record([10,20,30],3);
^public_bytes:#s="A\0B";
&^array_compare(x:#u16,y:#u16)->#u8 { (#u8)([x,2]<[y,3])+(#u8)([x,2]==[y,2])*2 }
&^shorter_compare(x:#u16,y:#u16)->#b {[x,2]<[y]}
&^record_compare(x:#u16,y:#u16)->#b {p=#Point(x,2);q=#Point(y,3);p<q}
&^signed_array_compare(x:#i16)->#b {[-1,2]<[x,(#i16)3]}
&^round_record(r:#Record)->#Record {r.data[1]+=r.count;r.count+=1;r}
&~materialized_record_body(r:#Record)->#Record {round_record(r)}
&^materialized_record(x:#u16)->#u16 {r=materialized_record_body(#Record([x,2,3],4));r.data[0]+r.data[1]+r.count}
&^record_copy(x:#u16,i:#u8)->#u16 {
    p=#Record([x,2,3],4); q=p;
    p.data[i]+=7; p.count+=1;
    p.data[i]+q.data[i]+p.count
}
&^record_branch(x:#u16,flag:#b)->#u16 {
    p=flag?#Point(x,3):#Point(4,x);
    p.x+p.y
}
&^record_loop(x:#u16)->#u16 {
    p=#Point(x,0); i:#u8=0;
    (i<3)!@{p.x+=2;i+=1;};
    p.x
}
&^record_iter(x:#u16)->#u16 {
    table=[#Point(x,1),#Point(2,3)]; total:#u16=0;
    table!@{total+=_.x;};
    total
}
&!read_byte()->#u8;
&!write_byte(value:#u8);
&~mix(value:#u16)->#u16 { value * 3 + 5 }
&^arithmetic(a:#u16,b:#u16)->#u16 { (a+b)*(a-b) }
&^wide(a:#u32,b:#u32)->#u32 { (a+b)*(a-b) }
&^division(a:#i32,b:#i32)->#i32 { a/b }
&^remainder(a:#i32,b:#i32)->#i32 { a%b }
&^right(a:#i16,b:#u8)->#i16 { a>>b }
&^left(a:#u8,b:#u8)->#u8 { a<<b }
&^signed_small(a:#i8)->#i8 { a*3+1 }
&^odd_width(a:#u7)->#u7 { a+1 }
&^absolute(a:#i16)->#i16 { |a| }
&^compare(a:#i16,b:#i8)->#b { a<b }
&^narrow(a:#i32)->#u8 { (#u8)a }
&^flow(a:#u16,b:#b)->#u16 {
    x:#u16=mix(a);
    b ? {x+=7;} : {x-=9;};
    b ? {x+=2;};
    x
}
&^nested(a:#u8,b:#b,c:#b)->#u8 {
    x=a;
    b ? {c ? {x+=3;} : {x+=5;};} : {x+=7;};
    x
}
&^lookup(i:#u8)->#u8 { a:#u8[4]=[2,3,5,7]; a[i] }
&^unroll(a:#u16)->#u16 { x=a; 0..4@{x+=_;}; x }
&^effects(a:#u8,b:#b)->#u8 {
    x=a;
    b ? {x+=read_byte();} : {x+=3;};
    write_byte(x);
    x
}
&^short(b:#b)->#b { b && ((#b)read_byte()) }
&^effect_constant(b:#b)->#u8 { b ? {write_byte(1); 42} : {write_byte(2); 42} }
&pair(a:#u8,b:#u8)->#u8 { a*10+b }
&^order(a:#u8)->#u8 { x=a; pair(x, x=3) }
&identity(x){x}
&constant_event(v){write_byte(v); (#u8)42}
&^reuse_one()->#u8 {identity(constant_event((#u8)1))}
&^reuse_two()->#u8 {identity(constant_event((#u8)2))}
&^wide_constant()->#u8 {
    big:#u128=340282366920938463463374607431768211455;
    write_byte((#u8)big);
    (#u8)(big>>120)
}
"""

CONTROL_SOURCE = r"""
&^loop_sum(n:#u16)->#u16 {
    i:#u16=0; total:#u16=0;
    (i<n)!@{total+=i;i+=1;}; total
}
&^nested_loops(n:#u8)->#u16 {
    i:#u8=0; total:#u16=0;
    (i<n)!@{
        j:#u8=0;
        (j<i)!@{total+=(#u16)j;j+=1;};
        i+=1;
    }; total
}
&^guard_effect(n:#u8)->#u16 {
    i:#u8=0; total:#u16=0;
    ({write_byte(i); i<n})!@{total+=(#u16)i;i+=1;};
    total
}
&^guard_mutation(n:#u8)->#u8 {
    i:#u8=0;
    ({i+=1;i<n})!@{};
    i
}
&^runtime_array(a:#u16,b:#u16)->#u16 {
    items:#u16[3]=[a,b,7]; total:#u16=0;
    items!@{total+=_;}; total
}
&^mutating_loop(a:#u16)->#u16 {
    items:#u16[3]=[a,2,3]; i:#u8=0;
    (#u8)0..3!@{items[_]+=(#u16)_;i+=1;};
    items[0]+items[1]+items[2]+(#u16)i
}
&^iteration_snapshot(a:#u8)->#u16 {
    items:#u8[3]=[a,2,3]; total:#u16=0;
    items!@{items[1]=99;total+=(#u16)_;};
    total+(#u16)items[1]
}
&^index_once(k:#u8)->#u16 {
    a:#u8[3]=[10,20,30];i=k;
    a[{j=i;i+=1;j}]+=3;
    (#u16)a[k]+(#u16)i*100
}
&change_copy(a){a[0]+=10;a}
&^array_copy(x:#u8,i:#u8)->#u8 {
    a:#u8[3]=[x,2,3]; b=a; c=change_copy(a);
    a[i]+={a[1]=7;5};
    a[0]+a[1]+b[0]+c[0]
}
&^matrix(x:#u16,i:#u8,j:#u8)->#u16 {
    a:#u16[3][2]=[[x,2,3],[4,5,6]];
    b=a;
    a[i][j]+={a[1][2]=20;7};
    a[0][1]+a[1][2]+b[0][0]
}
&^chosen_array(x:#u8,b:#b)->#u8 {
    a:#u8[2]=b ? [x,(#u8)2] : [(#u8)3,(#u8)4];
    a[1]+=x; a[0]+a[1]
}
&^nested_iteration(x:#u8)->#u16 {
    a:#u8[2][2]=[[x,2],[3,4]]; total:#u16=0;
    a!@{row=_;row!@{total+=(#u16)_;};};
    total
}
&^unrolled_repeat(x:#u8)->#u8 {
    i=0; total=x;
    (i<4)@{total+=(#u8)i;i+=1;};
    total
}
&^logical_store(b:#b)->#b {
    a:#b[2]=[b,1];a[0]&&=(#b)read_byte();a[0]
}
&^trap_then_constant(x:#u8)->#u8 {1/x;42}
&^bounds_then_constant(i:#u8)->#u8 {a:#u8[2]=[4,4];a[i]=4;42}
&~constant_loop_helper(n:#u8)->#u8 {
    i:#u8=0;(i<n)!@{i+=1;};42
}
&^constant_after_loop(n:#u8)->#u8 {constant_loop_helper(n)}
"""

BASE_SOURCE += CONTROL_SOURCE
SOURCE = BASE_SOURCE + FIXED_SOURCE


def project_environment():
    env = os.environ.copy()
    # A parent `make sanitize BUILD=...` must not override this independent
    # project's build layout through GNU make's inherited command-line flags.
    for name in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES", "MAKELEVEL", "BUILD"):
        env.pop(name, None)
    if "MSX_TEST_RUNNER_CXXFLAGS" in env:
        env["RUNNER_CXXFLAGS"] = env["MSX_TEST_RUNNER_CXXFLAGS"]
    return env


def run(args, **kwargs):
    if str(args[0]) == "make":
        kwargs.setdefault("env", project_environment())
    result = subprocess.run([str(a) for a in args], text=True, capture_output=True, **kwargs)
    if result.returncode:
        raise AssertionError(f"command failed: {args}\n{result.stdout}\n{result.stderr}")
    return result


class MsxTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="vexel-msx-test-")
        cls.base = Path(cls.tmp.name)
        cls.source = cls.base / "program.vx"
        cls.source.write_text(SOURCE)
        cls.projects = []
        cls.libs = []
        for mode in ("fifo", "lifo", "random"):
            project = cls.base / mode
            run([EXE, cls.source, "--emit-msx", project, "--schedule", mode])
            hooks = project / "hooks.c"
            hooks.write_text('''#include "vexel.h"
unsigned reads, writes, last;
unsigned again_count;
uint8_t vx_ext_again(void) { return again_count++ < 3; }
uint8_t vx_ext_read_byte(void) { ++reads; return 11; }
void vx_ext_write_byte(uint8_t v) { ++writes; last=v; }
uint16_t vx_ext_bytes_sum(const uint8_t* bytes,uint16_t size) {
    uint16_t result=0;while(size--) result+=*bytes++;return result;
}
''')
            record_type=re.search(r'void vx_round_record\(const (\w+)\* p0,',(project/'generated/vexel.h').read_text()).group(1)
            with hooks.open('a') as stream:
                stream.write(f'void vx_ext_record_exchange(const {record_type}* input, {record_type}* output) {{ *output=*input; output->f1+=2; }}\n')
            library = project / "test.so"
            run(["cc", "-std=c11", "-O2", "-shared", "-fPIC", "-I" + str(project / "generated"),
                 "-I" + str(project / "runtime"), *sorted((project / "generated").glob("*.c")),
                 project / "runtime/vx_runtime.c", hooks, "-o", library])
            cls.projects.append(project)
            cls.libs.append(ctypes.CDLL(str(library)))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def call(self, lib, name, args, types, result):
        fn = getattr(lib, "vx_" + name)
        fn.argtypes = types
        fn.restype = result
        return fn(*args)

    def oracle(self, name, args):
        cmd = [EXE, self.source, "--call", name, "--json"]
        for value in args:
            cmd.extend(["--arg", str(value)])
        return int(json.loads(run(cmd).stdout)["roots"][0]["value"])

    def test_arithmetic_matches_resolver(self):
        u8, u16, u32 = ctypes.c_uint8, ctypes.c_uint16, ctypes.c_uint32
        i8, i16, i32 = ctypes.c_int8, ctypes.c_int16, ctypes.c_int32
        cases = [
            ("arithmetic", [65535, 40000], [u16, u16], u16),
            ("wide", [4294967295, 2147483649], [u32, u32], u32),
            ("division", [-2147483648, -1], [i32, i32], i32),
            ("division", [-2147483648, 3], [i32, i32], i32),
            ("remainder", [-2147483648, 3], [i32, i32], i32),
            ("remainder", [2147483647, -99], [i32, i32], i32),
            ("right", [-32768, 15], [i16, u8], i16),
            ("right", [-1, 0], [i16, u8], i16),
            ("left", [255, 7], [u8, u8], u8),
            ("signed_small", [-128], [i8], i8),
            ("signed_small", [127], [i8], i8),
            ("odd_width", [127], [u8], u8),
            ("absolute", [-32768], [i16], i16),
            ("compare", [-300, 127], [i16, i8], u8),
            ("narrow", [-1000], [i32], u8),
        ]
        rng = random.Random(731)
        for _ in range(25):
            a, b = rng.randrange(-2**31, 2**31), rng.randrange(-2**31, 2**31) or 1
            cases.extend([("division", [a, b], [i32, i32], i32),
                          ("remainder", [a, b], [i32, i32], i32)])
        for name, args, types, result in cases:
            expected = self.oracle(name, args)
            for lib in self.libs:
                with self.subTest(name=name, args=args):
                    self.assertEqual(self.call(lib, name, args, types, result), expected)

    def test_fixed_point_matches_resolver_and_integer_oracle(self):
        u8, u32, i16 = ctypes.c_uint8, ctypes.c_uint32, ctypes.c_int16
        rng = random.Random(27019)
        signed = lambda n: n - 2**32 if n >= 2**31 else n
        trunc = lambda a,b: (abs(a)//abs(b)) * (-1 if (a < 0) != (b < 0) else 1)
        cases = []
        for _ in range(40):
            a, b = rng.randrange(2**32), rng.randrange(1,2**32)
            cases.extend([
                ('fixed_mul_bits', [a,b], [u32,u32], u32, trunc(signed(a)*signed(b),2**16) & 0xffffffff),
                ('fixed_div_bits', [a,b], [u32,u32], u32, trunc(signed(a)*2**16,signed(b)) & 0xffffffff),
                ('fixed_fraction_bits', [a,b], [u32,u32], u32, (a*b)//2**32),
            ])
        for x in (-32768,-257,-3,-1,0,1,3,127,255,32767):
            for name in ('fixed_rescale','fixed_record','fixed_array'):
                cases.append((name,[x],[i16],i16,self.oracle(name,[x])))
            cases.append(('fixed_compare',[x],[i16],u8,self.oracle('fixed_compare',[x])))
            for condition in (0,1):
                cases.append(('fixed_branch',[x,condition],[i16,u8],i16,self.oracle('fixed_branch',[x,condition])))
        for n in range(6):
            cases.append(('fixed_call',[n],[u8],i16,self.oracle('fixed_call',[n])))
        for name, args, types, result, expected in cases:
            self.assertEqual(self.oracle(name,args),expected,(name,args))
            for lib in self.libs:
                with self.subTest(name=name,args=args):
                    self.assertEqual(self.call(lib,name,args,types,result),expected)

    def test_runtime_recursion_and_specialization(self):
        for lib in self.libs:
            counter=ctypes.c_uint.in_dll(lib,'again_count')
            for name in ('noarg_dynamic','noarg_generalized'):
                counter.value=0
                self.assertEqual(self.call(lib,name,[],[],ctypes.c_uint16),3)
                self.assertEqual(counter.value,4)
            for n in (0,1,2,7,20):
                for name in ('recursive_sum','recursive_acc','recursive_inferred','recursive_export'):
                    self.assertEqual(self.call(lib,name,[n],[ctypes.c_uint16],ctypes.c_uint16),n*(n+1)//2)
                self.assertEqual(self.call(lib,'recursive_even',[n],[ctypes.c_uint16],ctypes.c_uint8),int(n%2==0))
                self.assertEqual(self.call(lib,'recursive_shape',[n],[ctypes.c_uint16],ctypes.c_uint16),n+3)
                for name in ('capture_recursion','receiver_recursion'):
                    self.assertEqual(self.call(lib,name,[n],[ctypes.c_uint16],ctypes.c_uint16),n*(n+1))
                self.assertEqual(self.call(lib,'capture_after',[n],[ctypes.c_uint16],ctypes.c_uint16),n*(n+1)//2+n*(n+1)*(n+2)//6)

    def test_materialized_environments_and_aggregate_abi(self):
        class Array(ctypes.Structure):
            _fields_=[('items',ctypes.c_uint16*3)]
        class Record(ctypes.Structure):
            _fields_=[('data',Array),('count',ctypes.c_uint16)]
        for lib in self.libs:
            self.assertEqual(ctypes.c_uint16.in_dll(lib,'vx_public_number').value,513)
            self.assertEqual(list(Array.in_dll(lib,'vx_public_array').items),[1,257,65535])
            self.assertEqual(list(Record.in_dll(lib,'vx_public_record').data.items),[10,20,30])
            self.assertEqual(bytes((ctypes.c_uint8*3).in_dll(lib,'vx_public_bytes')),b'A\0B')
            self.assertEqual(self.call(lib,'binary_text',[],[],ctypes.c_uint16),131)
            for x in (0,10,65535):
                self.assertEqual(self.call(lib,'materialized_capture',[x],[ctypes.c_uint16],ctypes.c_uint16),(3*x+28)&65535)
                self.assertEqual(self.call(lib,'materialized_array',[x],[ctypes.c_uint16],ctypes.c_uint16),(x+7)&65535)
                self.assertEqual(self.call(lib,'materialized_record',[x],[ctypes.c_uint16],ctypes.c_uint16),(x+11)&65535)
                self.assertEqual(self.call(lib,'external_record',[x],[ctypes.c_uint16],ctypes.c_uint16),(x+6)&65535)
                for i in (0,1):
                    self.assertEqual(self.call(lib,'materialized_method',[x,i],[ctypes.c_uint16,ctypes.c_uint8],ctypes.c_uint16),((x if i==0 else 2)+3)&65535)
                original=Record(Array((ctypes.c_uint16*3)(x,2,3)),4); result=Record()
                self.call(lib,'round_record',[ctypes.byref(original),ctypes.byref(result)], [ctypes.POINTER(Record),ctypes.POINTER(Record)],None)
                self.assertEqual(list(result.data.items),[x,6,3]);self.assertEqual(result.count,5)
                self.assertEqual(list(original.data.items),[x,2,3]);self.assertEqual(original.count,4)

    def test_runtime_deep_comparisons(self):
        for lib in self.libs:
            for x,y in ((0,0),(1,2),(2,1),(65535,0),(65535,65535)):
                args=[ctypes.c_uint16,ctypes.c_uint16]
                expected=int([x,2]<[y,3])+2*int([x,2]==[y,2])
                self.assertEqual(self.call(lib,'array_compare',[x,y],args,ctypes.c_uint8),expected)
                self.assertEqual(self.call(lib,'shorter_compare',[x,y],args,ctypes.c_uint8),int([x,2]<[y]))
                self.assertEqual(self.call(lib,'record_compare',[x,y],args,ctypes.c_uint8),int([x,2]<[y,3]))
            for x in (-32768,-2,-1,0,32767):
                self.assertEqual(self.call(lib,'signed_array_compare',[x],[ctypes.c_int16],ctypes.c_uint8),int([-1,2]<[x,3]))

    def test_runtime_representation_casts(self):
        for lib in self.libs:
            for x in (0,1,8,255,256,511,65535):
                self.assertEqual(self.call(lib,'bit_round',[x],[ctypes.c_uint16],ctypes.c_uint16),x^8)
                self.assertEqual(self.call(lib,'wire_decode',[x],[ctypes.c_uint16],ctypes.c_uint16),(x&511)+254)
                for i,expected in enumerate((x&255,(x>>8)&1,126)):
                    self.assertEqual(self.call(lib,'wire_byte',[x,i],[ctypes.c_uint16,ctypes.c_uint8],ctypes.c_uint8),expected)
            for x in (0,1):
                self.assertEqual(self.call(lib,'boolean_bytes',[x],[ctypes.c_uint8],ctypes.c_uint8),x*2)

    def test_completion_and_early_state(self):
        u8, u16 = ctypes.c_uint8, ctypes.c_uint16
        for lib in self.libs:
            for name in ('tuple_assign','tuple_loop'):
                self.assertEqual(self.call(lib,name,[4,7],[u16,u16],u16),25)
            for c in (0,1):
                self.assertEqual(self.call(lib,'early_call',[c,19],[u8,u16],u16),50)
                self.assertEqual(self.call(lib,'early_value',[c],[u8],u16),7 if c else 8)
                self.assertEqual(self.call(lib,'early_both',[c],[u8],u16),7 if c else 9)
                self.assertEqual(self.call(lib,'early_capture',[c],[u8],u16),9 if c else 13)
            for n in range(13):
                for name in ('unroll_exit','runtime_exit'):
                    self.assertEqual(self.call(lib,name,[n],[u8],u16),sum(i for i in range(min(n,10)) if i!=3))
                self.assertEqual(self.call(lib,'runtime_return',[n],[u8],u16),n*2 if n<10 else 42)
                self.assertEqual(self.call(lib,'repeat_exit',[n],[u8],u16),sum(range(1,min(n,10)+1)))

    def test_record_values_and_regions(self):
        u8, u16 = ctypes.c_uint8, ctypes.c_uint16
        for lib in self.libs:
            for x in (0, 123, 65535):
                for i in range(3):
                    original = [x, 2, 3][i]
                    expected = (((original + 7) & 65535) + original + 5) & 65535
                    self.assertEqual(self.call(lib, "record_copy", [x,i], [u16,u8], u16), expected)
                for flag in (0,1):
                    self.assertEqual(self.call(lib, "record_branch", [x,flag], [u16,u8], u16), (x+(3 if flag else 4))&65535)
                self.assertEqual(self.call(lib, "record_loop", [x], [u16], u16), (x+6)&65535)
                self.assertEqual(self.call(lib, "record_iter", [x], [u16], u16), (x+2)&65535)
                self.assertEqual(self.call(lib, "expressions", [x], [u16], u16), (x+6)&65535)
                self.assertEqual(self.call(lib, "captured", [x], [u16], u16), (x+4)&65535)
                self.assertEqual(self.call(lib, "captured_loop", [x], [u16], u16), (x+6)&65535)
                self.assertEqual(self.call(lib, "method_loop", [x], [u16], u16), (x+6)&65535)
                self.assertEqual(self.call(lib, "operator_loop", [x], [u16], u16), (x+6)&65535)
                self.assertEqual(self.call(lib, "logical_method", [x], [u16], u16), (x+3)&65535)
                self.assertEqual(self.call(lib, "logical_field", [x], [u16], u16), (x+3)&65535)
                self.assertEqual(self.call(lib, "lifted_method", [x], [u16], u16), (2*x+8)&65535)
                self.assertEqual(self.call(lib, "lifted_capture", [x], [u16], u16), (x+13)&65535)
                self.assertEqual(self.call(lib, "custom_iter", [x], [u16], u16), (x+5)&65535)
                self.assertEqual(self.call(lib, "macro_loop", [x,7], [u16,u8], u16), (x+21)&65535)
                self.assertEqual(self.call(lib, "method_call", [x,0], [u16,u8], u16), (x+3)&65535)
                self.assertEqual(self.call(lib, "method_call", [x,1], [u16,u8], u16), 5)
                for i in range(3):
                    self.assertEqual(self.call(lib, "broadcast", [x,i], [u16,u8], u16), [(x+4)&65535,7,9][i])

    def test_control_state_and_materialization(self):
        u8, u16 = ctypes.c_uint8, ctypes.c_uint16
        for lib in self.libs:
            for value in (0, 1, 123, 65535):
                for flag in (0, 1):
                    self.assertEqual(self.call(lib, "flow", [value, flag], [u16, u8], u16),
                                     (value * 3 + 5 + (9 if flag else -9)) & 65535)
            for a in (0, 200, 255):
                for b in (0, 1):
                    for c in (0, 1):
                        self.assertEqual(self.call(lib, "nested", [a, b, c], [u8]*3, u8),
                                         (a + (3 if c else 5) if b else a + 7) & 255)
                self.assertEqual(self.call(lib, "order", [a], [u8], u8), (a*10 + 3) & 255)
            for index, expected in enumerate((2, 3, 5, 7)):
                self.assertEqual(self.call(lib, "lookup", [index], [u8], u8), expected)
            self.assertEqual(self.call(lib, "unroll", [65534], [u16], u16), 4)
        self.assertIn("vxn_flow_", (self.projects[0] / "generated/vxp_flow.c").read_text())

    def test_runtime_regions_and_array_values(self):
        u8, u16 = ctypes.c_uint8, ctypes.c_uint16
        for lib in self.libs:
            for n in (0, 1, 5, 10, 255):
                self.assertEqual(self.call(lib, "loop_sum", [n], [u16], u16), n*(n-1)//2 & 65535)
                self.assertEqual(self.call(lib, "nested_loops", [n], [u8], u16),
                                 sum(i*(i-1)//2 for i in range(n)) & 65535)
                if n < 255:
                    self.assertEqual(self.call(lib, "guard_mutation", [n], [u8], u8), max(n, 1))
                self.assertEqual(self.call(lib, "constant_after_loop", [n], [u8], u8), 42)
            for a in (0, 1, 200, 255):
                self.assertEqual(self.call(lib, "runtime_array", [a, 65535], [u16,u16], u16), (a+6)&65535)
                self.assertEqual(self.call(lib, "mutating_loop", [a], [u16], u16), a+11)
                self.assertEqual(self.call(lib, "iteration_snapshot", [a], [u8], u16), a+104)
                self.assertEqual(self.call(lib, "nested_iteration", [a], [u8], u16), a+9)
                self.assertEqual(self.call(lib, "unrolled_repeat", [a], [u8], u8), (a+6)&255)
                for i in range(3):
                    self.assertEqual(self.call(lib, "array_copy", [a, i], [u8,u8], u8),
                                     (3*a+22 if i==0 else 3*a+17)&255)
                for i in range(2):
                    for j in range(3):
                        expected = 29+a if (i,j)==(0,1) else 15+a if (i,j)==(1,2) else 22+a
                        self.assertEqual(self.call(lib, "matrix", [a,i,j], [u16,u8,u8], u16), expected)
                for b in (0,1):
                    self.assertEqual(self.call(lib,"chosen_array",[a,b],[u8,u8],u8), ((2*a+2) if b else (a+7))&255)
            for k in range(3):
                self.assertEqual(self.call(lib,"index_once",[k],[u8],u16), 113+110*k)
            writes = ctypes.c_uint.in_dll(lib, "writes")
            last = ctypes.c_uint.in_dll(lib, "last")
            reads = ctypes.c_uint.in_dll(lib, "reads")
            for n in (0,1,10):
                writes.value=0
                self.assertEqual(self.call(lib,"guard_effect",[n],[u8],u16), n*(n-1)//2)
                self.assertEqual((writes.value,last.value),(n+1,n))
            reads.value=0
            self.assertEqual(self.call(lib,"logical_store",[0],[u8],u8),0)
            self.assertEqual(reads.value,0)
            self.assertEqual(self.call(lib,"logical_store",[1],[u8],u8),1)
            self.assertEqual(reads.value,1)

    def test_effect_order_and_short_circuit(self):
        u8 = ctypes.c_uint8
        for lib in self.libs:
            reads = ctypes.c_uint.in_dll(lib, "reads")
            writes = ctypes.c_uint.in_dll(lib, "writes")
            last = ctypes.c_uint.in_dll(lib, "last")
            reads.value = writes.value = last.value = 0
            self.assertEqual(self.call(lib, "effects", [250, 0], [u8, u8], u8), 253)
            self.assertEqual((reads.value, writes.value, last.value), (0, 1, 253))
            self.assertEqual(self.call(lib, "effects", [250, 1], [u8, u8], u8), 5)
            self.assertEqual((reads.value, writes.value, last.value), (1, 2, 5))
            self.assertEqual(self.call(lib, "short", [0], [u8], u8), 0)
            self.assertEqual(reads.value, 1)
            self.assertEqual(self.call(lib, "short", [1], [u8], u8), 1)
            self.assertEqual(reads.value, 2)
            for flag in (0, 1):
                self.assertEqual(self.call(lib, "effect_constant", [flag], [u8], u8), 42)
                self.assertEqual(last.value, 1 if flag else 2)
            for name, expected in (("reuse_one", 1), ("reuse_two", 2)):
                writes.value = 0
                self.assertEqual(self.call(lib, name, [], [], u8), 42)
                self.assertEqual((writes.value, last.value), (1, expected))
            writes.value = 0
            self.assertEqual(self.call(lib, "wide_constant", [], [], u8), 255)
            self.assertEqual((writes.value, last.value), (1, 255))

    def test_runtime_traps(self):
        for name, args, reason in [("division", [1, 0], 1), ("left", [1, 8], 2),
                                    ("lookup", [4], 3), ("flow", [1, 2], 5),
                                    ("trap_then_constant", [0], 1), ("bounds_then_constant", [2], 3)]:
            script = f'''import ctypes, resource
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
p=ctypes.CDLL({str(self.projects[0] / "test.so")!r})
p.vx_{name}({','.join(map(str, args))})
'''
            p = subprocess.run([sys.executable, "-c", script], capture_output=True)
            self.assertLess(p.returncode, 0, f"trap {reason} did not abort")

    def export(self, source, *flags):
        work = Path(tempfile.mkdtemp(dir=self.base))
        file = work / "input.vx"
        file.write_text(source)
        project = work / "project"
        p = subprocess.run([str(EXE), str(file), "--emit-msx", str(project), *flags],
                           text=True, capture_output=True)
        return p, project

    def test_target_facts_and_static_frame_contract(self):
        p, project = self.export("&^f(x:#u16)->#u16{x+(#u16)__target_rom_bank_bytes}", "--msx-exclusive", "f")
        self.assertEqual(p.returncode, 0, p.stderr)
        plan = json.loads((project / "generated/plan.json").read_text())
        self.assertEqual(plan["target_facts"]["__target_ram_bytes"], 16384)
        self.assertEqual(plan["functions"][0]["frame_policy"], "static-exclusive")
        self.assertGreater(plan["functions"][0]["static_temporaries_before_sdcc"], 0)
        self.assertIn("static uint16_t", (project / "generated/vxp_f.c").read_text())
        library = project / "static.so"
        run(["cc", "-std=c11", "-O2", "-shared", "-fPIC", "-I" + str(project / "generated"),
             "-I" + str(project / "runtime"), *sorted((project / "generated").glob("*.c")),
             project / "runtime/vx_runtime.c", "-o", library])
        lib = ctypes.CDLL(str(library))
        for arg in (0, 123, 65535):
            self.assertEqual(self.call(lib, "f", [arg], [ctypes.c_uint16], ctypes.c_uint16), (arg+8192)&65535)
        p, _ = self.export("&!callback(); &^f(){callback();}", "--msx-exclusive", "f")
        self.assertIn("without external callbacks", p.stderr)

    def test_static_array_frames_reinitialize_between_calls(self):
        source = """&^f(x:#u8)->#u16{
            a:#u16[2]=[x,3];i:#u8=0;
            (i<2)!@{a[i]+=(#u16)i;i+=1;};a[0]+a[1]
        }"""
        p, project = self.export(source, "--msx-exclusive", "f")
        self.assertEqual(p.returncode, 0, p.stderr)
        library = project / "static-array.so"
        run(["cc", "-std=c11", "-O2", "-shared", "-fPIC", "-I" + str(project / "generated"),
             "-I" + str(project / "runtime"), *sorted((project / "generated").glob("*.c")),
             project / "runtime/vx_runtime.c", "-o", library])
        lib = ctypes.CDLL(str(library))
        for arg in (0,255,1,128,0):
            self.assertEqual(self.call(lib,"f",[arg],[ctypes.c_uint8],ctypes.c_uint16), arg+4)

    def test_failures_leave_no_partial_project(self):
        cases = [
            ("&^f(x:#u64)->#u64{x+1}", (), "widths 1..32"),
            ("&^f()->#u8{1}", ("--msx-entry", "missing"), "zero-argument"),
            ("&^f()->#u8{1}", ("--msx-stack", "10"), "128..8192"),
            ("&^f()->#u8{1}", ("--msx-rom-segments", "8"), "16..16384"),
            ("__target_ram_bytes=3; ^x:#u8=1;", (), "reserved target fact"),
            ("&^f()->#u8{__target_ram_bytes=1; 1}", (), "target fact"),
            ("^mul:#u8=1;", (), "reserved MSX C runtime name"),
        ]
        for source, flags, message in cases:
            p, project = self.export(source, *flags)
            self.assertNotEqual(p.returncode, 0)
            self.assertIn(message, p.stderr)
            self.assertFalse(project.exists())

    def test_existing_project_is_preserved(self):
        project = self.projects[0]
        before = (project / "user/main.c").read_bytes()
        p = subprocess.run([str(EXE), str(self.source), "--emit-msx", str(project)], capture_output=True, text=True)
        self.assertIn("will not be overwritten", p.stderr)
        self.assertEqual((project / "user/main.c").read_bytes(), before)

    def test_export_tracks_backend_inputs(self):
        work = Path(tempfile.mkdtemp(dir=self.base))
        file = work / "input.vx"
        file.write_text("&^f()->#u8{1}")
        depfile = work / "project.d"
        run([EXE, file, "--emit-msx", work / "project", "--depfile", depfile])
        deps = depfile.read_text()
        self.assertIn("runtime/crt0.s.in", deps)
        self.assertIn("vendor/megalinker/megalinker.cc", deps)
        self.assertIn("Project.mk", deps)

    @unittest.skipUnless(shutil.which("sdcc") and shutil.which("sdasz80"), "SDCC toolchain unavailable")
    def test_real_sdcc_megalinker_and_assembly_override(self):
        p, project = self.export((REPO / "examples/neo8.vx").read_text())
        self.assertEqual(p.returncode, 0, p.stderr)
        run(["make", "-j2"], cwd=project)
        report = json.loads((project / "build/resolution.json").read_text())
        self.assertLess(report["resident_bytes"], 16384 - 1024)
        self.assertEqual(report["stack_reserved_bytes"], 1024)
        self.assertEqual((project / "build/game.rom").read_bytes()[16:24], b"ROM_NEO8")
        stamp = (project / "build/vxp_lookup.rel").stat().st_mtime_ns
        run(["make"], cwd=project)
        self.assertEqual((project / "build/vxp_lookup.rel").stat().st_mtime_ns, stamp)
        (project / "user/vxp_lookup.s").write_text('''.module vxp_lookup
.globl _vxi_lookup
.area _CODE
_vxi_lookup::
    ld l,#42
    ret
''')
        run(["make", "-j2"], cwd=project)
        report = json.loads((project / "build/resolution.json").read_text())
        self.assertIn("user/vxp_lookup.s", report["handwritten_overrides"])
        # Removing an override must not reuse its newer .rel for generated C.
        (project / "user/vxp_lookup.s").unlink()
        run(["make", "-j2"], cwd=project)
        report = json.loads((project / "build/resolution.json").read_text())
        self.assertEqual(report["handwritten_overrides"], [])
        self.assertIn("vxt_0", (project / "build/vxp_lookup.asm").read_text())
        # Budget failure is based on measured resident placement, and removes
        # the failed ROM target instead of leaving a successful-looking image.
        plan = json.loads((project / "generated/plan.json").read_text())
        plan["resident_limit"] = 49153
        (project / "generated/plan.json").write_text(json.dumps(plan))
        p = subprocess.run(["make"], cwd=project, text=True, capture_output=True, env=project_environment())
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("beyond stack boundary", p.stderr)
        self.assertFalse((project / "build/game.rom").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
