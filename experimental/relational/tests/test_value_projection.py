#!/usr/bin/env python3
"""Independent fixed-width oracles for the optional pure C value realization.

--msx also executes the same vectors with SDCC and the Z80 backend simulator.
"""
import importlib.util
import argparse
import contextlib
import json
from pathlib import Path
import sys
import tempfile
from value_bindings import check_bindings
from value_ordering import check_ordering
from value_tables import check_tables
from value_choices import check_choices
from value_predicates import check_predicates

ROOT = Path(__file__).resolve().parents[3]
support_spec = importlib.util.spec_from_file_location('support', ROOT/'examples/relational_source_msx.py')
support = importlib.util.module_from_spec(support_spec)
support_spec.loader.exec_module(support)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('solver',type=Path)
parser.add_argument('--msx',action='store_true')
parser.add_argument('--output',type=Path)
args=parser.parse_args()
SOLVER = args.solver.resolve()
MSX = args.msx
SOURCE = """
&^byte(a:#u8,b:#u8,s:#i8){(a+b,a-b,a*b,a<<7,a>>3,s/8,s%8,s>>3)}
&^word(a:#u16,b:#u16,s:#i16){(a+b,a-b,a*b,a<<15,s/512,s%512,s>>9)}
&^wide(a:#u32,b:#u32,s:#i32){(a+b,a-b,a*b,a<<31,s/2,s%2,s>>31)}
&^shared(x:#u8){(x,x+1,x+2)}
&^advance(x:#u8,dt:#u8){
    room:#u8=x<186 ? 186-x : (#u8)0;
    step:#u8=dt<room ? dt : room;
    next:#u8=x+step;
    (step,next,next-1,x)
}
&^guarded(x:#u8){x+1!=0 ? (x,x+2) : ((#u8)0,(#u8)7)}
&^partial(x:#u8,y:#u16,flag:#u8){
    n:#u8=x+1;
    ((#u8)42,n!=0 ? n+1 : (#u8)7,y,
     n!=0 ? (flag!=0 ? n+2 : n-1) : (#u8)9,x,n)
}
#Pair(a:#i16,b:#i16);
&^choose(flag:#u8,s:#i16){flag!=0 ? #Pair(s/512,s%512) : #Pair(7,-9)}
&^select_math(flag:#u8,x:#u16,y:#u16,s:#i16){
    delta:#u16=(flag&1)!=0 ? 65504 : 0;
    (x+delta+((flag&2)!=0 ? (#u16)32 : (#u16)0),
     (flag&4)!=0 ? y : x,
     (#i16)((flag&8)!=0 ? (#u16)s : y),
     flag!=0 ? ((#u32)x<<16)|(#u32)y : (#u32)65535,y)
}
&^predicates(flag:#u8,x:#u16,y:#u16,a:#u32,b:#u32,s:#i16,t:#i16){
    (((flag!=0 && x!=y) || a==b) ? (#u8)7 : (#u8)0,
     s==t ? (#u8)1 : (#u8)0, a!=b ? a : b, b)
}
&^bounded([[range(2,7)]] x:#u8){x}
&^dynamic_shift(x:#u8,n:#u8){x<<n}
&^dynamic_division(x:#i16,n:#i16){x/n}
&^unused_trap(x:#u8,n:#u8){x<<n;(#u8)7}
&!sink(x:#u8);
&^effects(x:#u8){sink(x);x}
"""


def wrap(n, bits):
    return (n + (1 << (bits - 1))) % (1 << bits) - (1 << (bits - 1))


def quotient(a, b):
    return (abs(a) // b) * (-1 if a < 0 else 1)


def cases():
    result = {}
    for name, bits, shift, divisor in (("byte", 8, 3, 8), ("word", 16, 9, 512), ("wide", 32, 31, 2)):
        mask = (1 << bits) - 1
        samples = range(256) if bits == 8 else sorted({0, 1, 127, 128, 255, 256, 511, 512, 513,
            (1 << (bits - 1)) - 1, 1 << (bits - 1), (1 << (bits - 1)) + 1, mask - 1, mask})
        if MSX and bits == 8:
            samples = (0, 1, 2, 63, 64, 127, 128, 129, 254, 255)
        rows = []
        for a in samples:
            b = (a * 17 + 3) & mask
            s = wrap(a, bits)
            expected = [(a + b) & mask, (a - b) & mask, (a * b) & mask, (a << (bits - 1)) & mask]
            if bits == 8:
                expected += [a >> shift]
            q = quotient(s, divisor)
            expected += [q, s - q * divisor, s >> shift]
            rows.append(([a, b, s], expected))
        result[name] = rows
    result["shared"] = [([x], [x, (x + 1) & 255, (x + 2) & 255]) for x in range(256)]
    # A freshly stored selected operand must not replace the other addend in A.
    result["advance"] = []
    for x in (0, 7, 119, 185, 186, 254):
        for dt in (0, 1, 3, 255):
            step = min(dt, max(0, 186-x))
            next_value = (x+step)&255
            result["advance"].append(([x, dt], [step, next_value, (next_value-1)&255, x]))
    result["guarded"] = [([x], [x, (x + 2) & 255] if x != 255 else [0, 7])
                         for x in ((0, 1, 127, 128, 254, 255) if MSX else range(256))]
    # Interleaved invariant/conditional outputs keep their slots. n is shared
    # by the guard and its continuation; x remains unchanged at byte wrap.
    result["partial"] = []
    for x in (0, 1, 127, 254, 255):
        n = (x + 1) & 255
        y = (x << 8) ^ 0xAA55
        for flag in (0, 1):
            result["partial"].append(([x, y, flag], [42, (n+1)&255 if n else 7, y,
                (n+(2 if flag else -1))&255 if n else 9, x, n]))
    result["choose"] = [([flag, x], [quotient(x, 512), x - quotient(x, 512) * 512] if flag else [7, -9])
                        for flag in (0, 1) for x in (-32768, -513, -512, -511, -1, 0, 511, 512, 32767)]
    result["select_math"] = []
    for flag, x, y, s in ((0,0,65535,-32768),(1,65530,32768,-1),(2,65535,1,32767),
                          (3,65530,65535,-32768),(7,32768,256,-513),(15,1,32767,-1)):
        result["select_math"].append(([flag,x,y,s],
            [(x+(-32 if flag&1 else 0)+(32 if flag&2 else 0))&65535,
             y if flag&4 else x, s if flag&8 else wrap(y,16),
             (x<<16)|y if flag else 65535,y]))
    result["bounded"] = [([x], [x]) for x in range(2, 8)]
    result["predicates"] = []
    for flag,x,y,a,b,s,t in ((0,0,0,0,0,-1,-1),(1,0x1200,0x1201,0,1,-1,255),
                            (1,0x1200,0x1300,0x12345678,0x12345778,-32768,32767),
                            (0,65535,0,0x12345678,0x12355678,0,0),
                            (1,256,256,0x12345678,0x13345678,256,257),
                            (255,65535,0,0xffffffff,0xffffffff,-32768,-32768)):
        result["predicates"].append(([flag,x,y,a,b,s,t],
            [7 if (flag and x!=y) or a==b else 0,int(s==t),a if a!=b else b,b]))
    return result


def c_type(t):
    return ("int" if t.startswith("#i") else "uint") + t[2:] + "_t"


run = support.run


evidence = args.output.resolve() if args.output else ROOT / "build/value-msx"
if MSX or args.output:
    evidence.mkdir(parents=True, exist_ok=True)
workspace = (contextlib.nullcontext(tempfile.mkdtemp(prefix="run-", dir=evidence)) if MSX or args.output
             else tempfile.TemporaryDirectory(prefix="vexel-values-"))
with workspace as temporary:
    directory = Path(temporary)
    source = directory / "values.vx"
    source.write_text(SOURCE)
    generated = directory / "generated"
    vectors = cases()
    rejected = ("dynamic_shift", "dynamic_division", "unused_trap", "effects")
    names = [*vectors, *rejected]
    result = run([SOLVER, source, *[arg for name in names for arg in ("--source", name)], "--emit-c", generated])
    report = json.loads(result.stdout)
    plans = {s["name"]: s for s in report["sources"]}
    for name in names:
        assert plans[name]["value_projection"]["available"] == (name in vectors), plans[name]
        if name in vectors:
            assert plans[name]["value_projection"]["fixed_frame"]["available"] == (name != "bounded"), plans[name]
    # A pure function alone is not a proof that its activations cannot overlap.
    overlapping = directory / "overlapping"
    concurrent = json.loads(run([SOLVER, source, "--source", "shared", "--mode", "overlap",
                                 "--emit-c", overlapping]).stdout)
    assert not concurrent["sources"][0]["value_projection"]["fixed_frame"]["available"]
    assert not list(overlapping.glob("vx_fixed_*.h"))
    headers = "".join(f'#include "vx_value_{name}.h"\n' for name in vectors)
    case_headers = "".join(f'#include "{plans[name]["value_projection"]["cases_header"]}"\n'
                       for name in vectors if "cases_header" in plans[name]["value_projection"])
    tables, declarations, definitions, body, fixed_body, fixed_headers, case_body = [], [], [], [], [], [], []
    for number, (name, rows) in enumerate(vectors.items(), 1):
        plan = plans[name]
        inputs = [l for p in plan["parameters"] for l in p["leaves"]]
        fields = ";".join(c_type(l["logical_type"]) + " a" + str(i) for i, l in enumerate(inputs)) + ";"
        fields += ";".join(c_type(l["logical_type"]) + " e" + str(i) for i, l in enumerate(plan["leaves"])) + ";"
        literal = lambda n: str(n) + ("L" if n < 0 else "UL") if n != -2147483648 else "(-2147483647L-1L)"
        data = ",\n".join("{" + ",".join(literal(v) for v in [*a, *b]) + "}" for a, b in rows)
        tables.append("static const struct {" + fields + "} t_" + name + "[]={\n" + data + "};\n")
        declarations.append(f"typedef struct {{{fields}}} vector_{name};\nextern const vector_{name} t_{name}[{len(rows)}];\n")
        definitions.append(f"const vector_{name} t_{name}[{len(rows)}]={{\n{data}\n}};\n")
        args = ",".join("t_" + name + "[i].a" + str(i) for i in range(len(inputs)))
        check = " || ".join(f"r.v{i}!=t_{name}[i].e{i}" for i in range(len(plan["leaves"])))
        body.append(f"{{vx_{name}_result r;for(i=0;i<{len(rows)};++i){{vx_{name}_value(&r,{args});"
                    f"if({check})return {number * 1000}UL+i;}}}}\n")
        value = plan["value_projection"]
        if value["fixed_frame"]["available"]:
            fixed = value["fixed_frame"]
            fixed_headers.append(f'#include "{fixed["header"]}"\n')
            frame = fixed["frame"]
            assignments = "".join(f"{frame}.a{j}=t_{name}[i].a{j};" for j in range(len(inputs)))
            fixed_check = check.replace("r.v", frame + ".result.v")
            fixed_body.append(f"for(i=0;i<{len(rows)};++i){{{assignments}{fixed['function']}();"
                              f"if({fixed_check})return {number * 1000}UL+i;}}\n")
        if "cases_header" in value:
            selected_args = lambda indices: ",".join(f"t_{name}[i].a{j}" for j in indices)
            arms = []
            for arm, indices in enumerate(value["case_inputs"]):
                actual = selected_args(indices)
                arms.append(f"vx_{name}_case{arm}_value(&r" + ("," + actual if actual else "") + ");")
            case_body.append(f"{{vx_{name}_result r;for(i=0;i<{len(rows)};++i){{"
                        f"if(vx_{name}_branch({selected_args(value['branch_inputs'])})){{{arms[1]}}}else{{{arms[0]}}}"
                        f"if({check})return {10000 + number * 1000}UL+i;}}}}\n")
    # Exhaust signed 16-bit quotient/remainder independently on the host;
    # the target runs the boundary vectors above to keep its ROM small.
    exhaustive = """
#ifndef __SDCC
    { int32_t n; vx_word_result r;
      for(n=-32768;n<=32767;++n) { vx_word_value(&r,0,0,(int16_t)n);
        if(r.v4!=n/512 || r.v5!=n%512) return 7000; }
    }
#endif
    { uint8_t x=254;vx_shared_result a,b;
      vx_shared_value(&a,x++);vx_shared_value(&b,7);
      if(x!=255 || a.v0!=254 || a.v1!=255 || a.v2!=0 || b.v0!=7) return 8000;
    }
"""
    functions = "uint32_t check(void){uint16_t i;\n" + "".join(body) + exhaustive + "return 0;}\n"
    case_functions = "uint32_t check_cases(void){uint16_t i;\n" + "".join(case_body) + "return 0;}\n"
    fixed_functions = "uint32_t check_fixed(void){uint16_t i;\n" + "".join(fixed_body) + """
    { vx_shared_result saved;
      vx_shared_fixed_frame.a0=254;vx_shared_fixed();saved=vx_shared_fixed_frame.result;
      vx_shared_fixed_frame.a0=7;vx_shared_fixed();
      if(saved.v0!=254 || saved.v1!=255 || saved.v2!=0 || vx_shared_fixed_frame.result.v0!=7)return 8000;
    }
    return 0;}
"""
    driver = headers + case_headers + "".join(tables) + functions + case_functions
    host = directory / "host.c"
    host.write_text(driver + "".join(fixed_headers) + fixed_functions + """
#include <setjmp.h>
#include <stdio.h>
static jmp_buf trapped;
void vx_trap(uint8_t reason){longjmp(trapped,reason);}
int main(void){
    uint32_t failed=check();int code;vx_bounded_result result;
    if(failed){printf("value failure %lu\\n",(unsigned long)failed);return 1;}
    failed=check_cases();
    if(failed){printf("case value failure %lu\\n",(unsigned long)failed);return 5;}
    failed=check_fixed();
    if(failed){printf("fixed value failure %lu\\n",(unsigned long)failed);return 4;}
    result.v0=99;code=setjmp(trapped);
    if(!code){vx_bounded_value(&result,8);return 2;}
    if(code!=16 || result.v0!=99)return 3;
    return 0;
}
""")
    runtime = ROOT / "backends/msx-neo8/runtime"
    binary = directory / "host"
    run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-fsanitize=undefined", "-fno-sanitize-recover=all",
         "-I" + str(generated), "-I" + str(runtime), host, "-o", binary])
    run([binary])
    print("Value projection: wrapping, signed division/shifts, shared operands, owned results and rejection checks passed (UBSan)")
    print("Fixed value frames: exclusive ownership, fresh inputs/temporaries, copied results and overlap rejection passed (UBSan)")
    if MSX:
        spec = importlib.util.spec_from_file_location("project", ROOT / "experimental/relational/project_msx.py")
        backend = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(backend)
        project = directory / "project"
        # Unsupported alternatives still have their normal execution code,
        # including this external sink, though the value harness never calls it.
        backend.package(generated, project, source, SOLVER)
        # Keep each resident object below the linker's 8 KiB section limit.
        (project / "user/value_vectors.h").write_text('#include <stdint.h>\n' + "".join(declarations))
        (project / "user/value_vectors.c").write_text('#include "value_vectors.h"\n' + "".join(definitions))
        (project / "user/main.c").write_text(headers + '#include "value_vectors.h"\n' + functions + """
volatile uint32_t vx_last_result;
void vx_external_sink(uint32_t *out,uint8_t x) VX_ABI VX_RESIDENT {(void)x;out[0]=0;}
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check();__asm halt __endasm;}
""")
        built = run(["make", "-C", project, "-j2", "simulate", "SIMFLAGS=--max-ticks=400000000"])
        execution = json.loads((project / "build/execution.json").read_text())
        assert execution["observation"]["last_result"] == execution["observation"]["fault"] == 0, execution
        (evidence / "execution.json").write_text(json.dumps(execution, indent=2) + "\n")
        (evidence / "build.log").write_text(built.stdout + built.stderr)
        print("Value projection: SDCC/Z80 execution agrees with the independent vectors")
        # Case evidence grows as more interfaces become provable. Keep its
        # resident code and vectors in a separate image, within both the 8 KiB
        # object window and the linker's RAM budget. Do not enlarge target RAM
        # or remove vectors merely to accommodate a larger test driver.
        case_project = directory / "case-project"
        backend.package(generated, case_project, source, SOLVER)
        (case_project / "user/value_vectors.h").write_text('#include <stdint.h>\n' + "".join(declarations))
        (case_project / "user/value_vectors.c").write_text('#include "value_vectors.h"\n' + "".join(definitions))
        (case_project / "user/main.c").write_text(case_headers + '#include "value_vectors.h"\n' + case_functions + """
volatile uint32_t vx_last_result;
void vx_external_sink(uint32_t *out,uint8_t x) VX_ABI VX_RESIDENT {(void)x;out[0]=0;}
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check_cases();__asm halt __endasm;}
""")
        built = run(["make", "-C", case_project, "-j2", "simulate", "SIMFLAGS=--max-ticks=400000000"])
        execution = json.loads((case_project / "build/execution.json").read_text())
        assert execution["observation"]["last_result"] == execution["observation"]["fault"] == 0, execution
        (evidence / "case-execution.json").write_text(json.dumps(execution, indent=2) + "\n")
        (evidence / "case-build.log").write_text(built.stdout + built.stderr)
        print("Case value interfaces: SDCC/Z80 execution agrees with the independent vectors")
        # Use a separate harness so adding a second realization does not
        # overflow the existing resident harness's 8 KiB code window.
        fixed_project = directory / "fixed-project"
        backend.package(generated, fixed_project, source, SOLVER)
        (fixed_project / "user/value_vectors.h").write_text('#include <stdint.h>\n' + "".join(declarations))
        (fixed_project / "user/value_vectors.c").write_text('#include "value_vectors.h"\n' + "".join(definitions))
        (fixed_project / "user/main.c").write_text("".join(fixed_headers) + '#include "value_vectors.h"\n' + fixed_functions + """
volatile uint32_t vx_last_result;
void vx_external_sink(uint32_t *out,uint8_t x) VX_ABI VX_RESIDENT {(void)x;out[0]=0;}
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check_fixed();__asm halt __endasm;}
""")
        built = run(["make", "-C", fixed_project, "-j2", "simulate", "SIMFLAGS=--max-ticks=400000000"])
        execution = json.loads((fixed_project / "build/execution.json").read_text())
        assert execution["observation"]["last_result"] == execution["observation"]["fault"] == 0, execution
        (evidence / "fixed-execution.json").write_text(json.dumps(execution, indent=2) + "\n")
        (evidence / "fixed-build.log").write_text(built.stdout + built.stderr)
        print("Fixed value frames: SDCC/Z80 execution and static frame sizes agree with the independent vectors")
    check_bindings(ROOT, SOLVER, directory, SOURCE, vectors, run, MSX, backend if MSX else None)
    check_ordering(ROOT, SOLVER, directory, run, MSX, backend if MSX else None)
    check_tables(ROOT, SOLVER, directory, run, MSX, backend if MSX else None)
    check_choices(ROOT, SOLVER, directory, run, MSX, backend if MSX else None)
    check_predicates(ROOT, SOLVER, directory, run, MSX, backend if MSX else None)
