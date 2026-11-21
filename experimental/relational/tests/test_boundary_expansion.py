#!/usr/bin/env python3
"""Native realization contracts preserve ordered effects and snapshot lifetimes."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("solver", type=Path)
parser.add_argument("--msx", action="store_true")
args = parser.parse_args()
SOLVER = args.solver.resolve()


def run(command, ok=True):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=240)
    if ok:
        assert result.returncode == 0, f"{command}\n{result.stdout}\n{result.stderr}"
    return result


SOURCE = """
#Pair(a:#u8,b:#u8);
[[c_expansion(NATIVE_PEEK)]] [[reads(engine)]] [[writes(trace)]] &!peek(n:#u8)->#Pair;
[[writes(engine)]] [[writes(trace)]] &!advance(n:#u8);
&!pulse();
[[c_expansion(NATIVE_SEED)]] &!seed()->#Pair;
[[c_expansion(NATIVE_PAIR)]] &!pair(a:#u8,b:#u8)->#Pair;
[[c_expansion(NATIVE_CONSUME)]] &!consume(a:#u8,b:#u8);
[[c_binding(scan,region,10,exclusive)]] &^scan(n:#u8){
    first=peek(n);advance(n+1);i:#u8=0;sum:#u8=0;
    (i<4)!@{pulse();i!=2 ? {part=peek(i);sum+=part.a;};advance(i);i+=1;};
    last=peek(n);(first.a,first.b,last.a,last.b,sum)
}
[[c_binding(nested,region,1,open)]] &^nested(n:#u8){peek(n)}
// The first publication is dead before the second is populated.
[[c_binding(reuse,region,1,exclusive)]] &^reuse(n:#u8){
    a=seed();consume(a.a,a.b);b=pair(n+2,n+3);consume(b.a,b.b);n
}
// Reversed replies would overlap the next call's input slots in a union.
[[c_binding(handoff,region,1,exclusive)]] &^handoff(){a=seed();pair(a.b,a.a)}
// A publication must survive both a conditional callback and another reply.
[[c_binding(retained,region,1,exclusive)]] &^retained(n:#u8){
    a=seed();(n&1)!=0 ? {advance(n);} : {pulse();};b=pair(n,n+1);(a.a,a.b,b.a,b.b)
}
"""
CLIENT = r'''
#include <stdint.h>
static uint8_t memory;
static uint16_t trace;
static uint8_t callback_value;
static void record(uint8_t op,uint8_t n) {trace=(uint16_t)(trace*17+op*3+n);}
static void callback(uint8_t n);
#define NATIVE_PEEK(q) do {record(1,(q).a0);(q).r0=(uint8_t)(memory+(q).a0);(q).r1=memory;} while(0)
#define NATIVE_ADVANCE(q) do {record(2,(q).a0);memory+=(q).a0;callback((q).a0);} while(0)
#define NATIVE_PULSE(q) do {record(3,0);} while(0)
#define NATIVE_SEED(q) do {record(4,0);(q).r0=memory;(q).r1=(uint8_t)(memory+17);} while(0)
#define NATIVE_PAIR(q) do {record(5,(q).a0);record(6,(q).a1);(q).r0=(uint8_t)((q).a0+1);(q).r1=(uint8_t)((q).a1+3);} while(0)
#define NATIVE_CONSUME(q) do {record(7,(q).a0);record(8,(q).a1);} while(0)
#include "vx_bind_reuse.h"
#include "vx_bind_handoff.h"
#include "vx_bind_retained.h"
#include "vx_bind_scan.h"
#include "vx_bind_nested.h"
#if !VX_BIND_scan_EXPANDED_peek
VX_BIND_scan_BOUNDARY_peek {NATIVE_PEEK(VX_BIND_scan_DATA_peek);}
#endif
#if !VX_BIND_scan_EXPANDED_advance
VX_BIND_scan_BOUNDARY_advance {NATIVE_ADVANCE(VX_BIND_scan_DATA_advance);}
#endif
#if !VX_BIND_scan_EXPANDED_pulse
VX_BIND_scan_BOUNDARY_pulse {NATIVE_PULSE(VX_BIND_scan_DATA_pulse);}
#endif
#if !VX_BIND_nested_EXPANDED_peek
VX_BIND_nested_BOUNDARY_peek {NATIVE_PEEK(VX_BIND_nested_DATA_peek);}
#endif
#if !VX_BIND_reuse_EXPANDED_seed
VX_BIND_reuse_BOUNDARY_seed {NATIVE_SEED(VX_BIND_reuse_DATA_seed);}
#endif
#if !VX_BIND_reuse_EXPANDED_pair
VX_BIND_reuse_BOUNDARY_pair {NATIVE_PAIR(VX_BIND_reuse_DATA_pair);}
#endif
#if !VX_BIND_reuse_EXPANDED_consume
VX_BIND_reuse_BOUNDARY_consume {NATIVE_CONSUME(VX_BIND_reuse_DATA_consume);}
#endif
#if !VX_BIND_handoff_EXPANDED_seed
VX_BIND_handoff_BOUNDARY_seed {NATIVE_SEED(VX_BIND_handoff_DATA_seed);}
#endif
#if !VX_BIND_handoff_EXPANDED_pair
VX_BIND_handoff_BOUNDARY_pair {NATIVE_PAIR(VX_BIND_handoff_DATA_pair);}
#endif
#if !VX_BIND_retained_EXPANDED_seed
VX_BIND_retained_BOUNDARY_seed {NATIVE_SEED(VX_BIND_retained_DATA_seed);}
#endif
#if !VX_BIND_retained_EXPANDED_pair
VX_BIND_retained_BOUNDARY_pair {NATIVE_PAIR(VX_BIND_retained_DATA_pair);}
#endif
#if !VX_BIND_retained_EXPANDED_advance
VX_BIND_retained_BOUNDARY_advance {NATIVE_ADVANCE(VX_BIND_retained_DATA_advance);}
#endif
#if !VX_BIND_retained_EXPANDED_pulse
VX_BIND_retained_BOUNDARY_pulse {NATIVE_PULSE(VX_BIND_retained_DATA_pulse);}
#endif
static void callback(uint8_t n) {
    VX_BIND_nested_LOCAL;
    vx_bind_nested_frame.a0=n;vx_bind_nested_run();
    callback_value=vx_bind_nested_frame.result.v0;
}
static uint8_t check(void) {
    unsigned n;uint8_t i,first,last,expected_memory,sum,expected_callback;
    uint16_t expected_trace;
    VX_BIND_scan_LOCAL;
    VX_BIND_reuse_LOCAL;
    VX_BIND_handoff_LOCAL;
    VX_BIND_retained_LOCAL;
    for(n=0;n<256;++n) {
        memory=251;trace=0;record(1,(uint8_t)n);first=(uint8_t)(memory+n);
        record(2,(uint8_t)(n+1));memory+=(uint8_t)(n+1);record(1,(uint8_t)(n+1));
        sum=0;
        for(i=0;i<4;++i) {
            record(3,0);
            if(i!=2) {record(1,i);sum+=(uint8_t)(memory+i);}
            record(2,i);memory+=i;record(1,i);
        }
        expected_callback=(uint8_t)(memory+3);
        record(1,(uint8_t)n);last=(uint8_t)(memory+n);
        expected_memory=memory;expected_trace=trace;
        memory=251;trace=0;vx_bind_scan_frame.a0=(uint8_t)n;vx_bind_scan_run();
        if(memory!=expected_memory || trace!=expected_trace || callback_value!=expected_callback)return 1;
        if(vx_bind_scan_frame.result.v0!=first || vx_bind_scan_frame.result.v1!=251 ||
           vx_bind_scan_frame.result.v2!=last || vx_bind_scan_frame.result.v3!=expected_memory ||
           vx_bind_scan_frame.result.v4!=sum)return 2;
        memory=(uint8_t)n;trace=0;
        record(4,0);record(7,(uint8_t)n);record(8,(uint8_t)(n+17));
        record(5,(uint8_t)(n+2));record(6,(uint8_t)(n+3));record(7,(uint8_t)(n+3));record(8,(uint8_t)(n+6));
        expected_trace=trace;trace=0;vx_bind_reuse_frame.a0=(uint8_t)n;vx_bind_reuse_run();
        if(trace!=expected_trace || memory!=n || vx_bind_reuse_frame.result.v0!=n)return 3;
        trace=0;record(4,0);record(5,(uint8_t)(n+17));record(6,(uint8_t)n);
        expected_trace=trace;trace=0;vx_bind_handoff_run();
        if(trace!=expected_trace || vx_bind_handoff_frame.result.v0!=(uint8_t)(n+18) ||
           vx_bind_handoff_frame.result.v1!=(uint8_t)(n+3))return 4;
        trace=0;record(4,0);
        if(n&1) {record(2,(uint8_t)n);record(1,(uint8_t)n);}else record(3,0);
        record(5,(uint8_t)n);record(6,(uint8_t)(n+1));expected_trace=trace;trace=0;
        vx_bind_retained_frame.a0=(uint8_t)n;vx_bind_retained_run();
        if(trace!=expected_trace || memory!=(uint8_t)(n*(n&1 ? 2 : 1)) ||
           vx_bind_retained_frame.result.v0!=n || vx_bind_retained_frame.result.v1!=(uint8_t)(n+17) ||
           vx_bind_retained_frame.result.v2!=(uint8_t)(n+1) || vx_bind_retained_frame.result.v3!=(uint8_t)(n+4))return 5;
    }
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="vexel-native-boundaries-") as tmp:
    directory = Path(tmp)
    source = directory / "source.vx"
    source.write_text(SOURCE)
    offer = ["--boundary-expansion", "advance:NATIVE_ADVANCE", "--boundary-expansion", "pulse:NATIVE_PULSE"]
    runtime = ROOT / "backends/msx-neo8/runtime"
    for budget, expanded, overlay in ((128, 0, 0), (128, 0, 1), (128, 1, 0), (128, 1, 1), (128, 1, 2), (0, 1, 1)):
        generated = directory / f"g{budget}-{expanded}-{overlay}"
        plan = json.loads(run([SOLVER, source, *offer, "--prefer", f"value_frame_bytes={budget}",
                              "--prefer", f"region_boundary_expansion={expanded}",
                              "--prefer", f"region_boundary_overlay={overlay}", "--emit-c", generated]).stdout)
        bindings = {b["name"]: b for b in plan["value_bindings"]["bindings"]}
        assert bindings["scan"]["storage"] == ("fixed" if budget else "automatic")
        assert bindings["nested"]["storage"] == "automatic"
        for binding in bindings.values():
            code = binding["codegen"]
            assert code["region_boundary_bytes"] <= code["region_boundary_unshared_bytes"]
            slots = {member: i for i, slot in enumerate(code["region_boundary_slots"]) for member in slot["members"]}
            assert all(slots[a] != slots[b] for a, b in code["region_boundary_conflicts"])
            if not overlay:
                assert code["region_boundary_bytes"] == code["region_boundary_unshared_bytes"]
        assert bindings["scan"]["codegen"]["region_expansions"] == (
            {"peek": "NATIVE_PEEK", "advance": "NATIVE_ADVANCE", "pulse": "NATIVE_PULSE"} if expanded else {})
        assert bindings["scan"]["codegen"]["region_expansion_barriers"] == (["peek"] if expanded else [])
        assert bindings["reuse"]["codegen"]["region_expansion_barriers"] == (["pair", "seed"] if expanded else [])
        assert bindings["scan"]["codegen"]["region_boundary_dedicated"] == (["advance", "pulse"] if expanded and overlay == 1 else [])
        for name in ("reuse", "handoff", "retained"):
            code = bindings[name]["codegen"]
            slots = {member: i for i, slot in enumerate(code["region_boundary_slots"]) for member in slot["members"]}
            assert (slots["seed"] == slots["pair"]) == bool(overlay and name == "reuse")
        assert all(not c["no_feedback"] for c in plan["boundary_contracts"])
        assert {a["resource"] for a in plan["source_accesses"]["scan"]} >= {"external:engine", "external-world"}
        client = directory / "client.c"
        client.write_text(CLIENT + "\n#include <stdlib.h>\nvoid vx_trap(uint8_t code){exit(code);}\nint main(void){return check();}\n")
        binary = directory / "host"
        command = ["cc", "-std=c11", "-O2", "-fsanitize=undefined", "-fno-sanitize-recover=all",
                   "-I" + str(generated), "-I" + str(runtime), client, generated / "vx_binding_frames.c", "-o", binary]
        run(command)
        run([binary])
        if expanded:
            client.write_text(CLIENT.replace("#define NATIVE_PEEK(q)", "#define MISSING_PEEK(q)") + "\nint main(void){return 0;}\n")
            missing = run(command, ok=False)
            assert missing.returncode != 0 and "native boundary expansion NATIVE_PEEK must be defined" in missing.stderr
        if args.msx:
            spec = importlib.util.spec_from_file_location("project", ROOT / "experimental/relational/project_msx.py")
            backend = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(backend)
            project = directory / f"p{budget}-{expanded}-{overlay}"
            backend.package(generated, project, source, SOLVER)
            (project / "user/main.c").write_text('#include "vx_binding_frames.c"\n' + CLIENT + '''
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check();__asm halt __endasm;}
''')
            run(["make", "-C", project, "-j2", "simulate", "SIMFLAGS=--max-ticks=400000000"])
            execution = json.loads((project / "build/execution.json").read_text())
            assert execution["observation"]["last_result"] == execution["observation"]["fault"] == 0, execution
    # Availability and selection are distinct, and equal facts commute.
    for extra in (["--boundary-expansion", "peek:NATIVE_PEEK"], ["--prefer", "boundary_expansion.peek=0"]):
        plan = json.loads(run([SOLVER, source, *extra]).stdout)
        scan = next(b for b in plan["value_bindings"]["bindings"] if b["name"] == "scan")
        assert scan["codegen"]["region_expansions"] == ({"peek": "NATIVE_PEEK"} if extra[0] == "--boundary-expansion" else {})
    for extra, error in [
        (["--boundary-expansion", "peek:DIFFERENT"], "conflicting boundary expansions"),
        (["--boundary-expansion", "absent:MACRO"], "external function declaration"),
        (["--boundary-expansion", "peek:bad-name"], "C macro identifier"),
        (["--boundary-expansion", "peek:3bad"], "C macro identifier"),
        (["--prefer", "boundary_expansion.absent=0"], "offered expansion"),
        (["--require", "boundary_expansion.peek=0"], "prefer relationship"),
    ]:
        failed = run([SOLVER, source, *extra], ok=False)
        assert failed.returncode != 0 and error in failed.stderr, failed.stderr
    # Native offers participate in the whole-request cache key. Returning to
    # an earlier choice may reuse only that exact choice and is verified fresh.
    commands = [f'solve "{source}"',
                f'solve "{source}" --boundary-expansion advance:NATIVE_ADVANCE',
                f'solve "{source}"', "quit"]
    session = subprocess.run([str(SOLVER), "--session", "--verify-incremental"],
                             input="\n".join(commands) + "\n", capture_output=True, text=True, timeout=240)
    replies = [json.loads(line) for line in session.stdout.splitlines()]
    assert session.returncode == 0 and len(replies) == 3, session.stderr
    assert [p["incremental"]["reused"] for p in replies] == [False, False, True]
    assert replies[-1]["incremental"]["verified"]
    # The physical alternative cannot bypass runtime result validation.
    source.write_text('[[c_expansion(NATIVE_BAD)]] &!bad()->#b;'
                      '[[c_binding(checked,region,1,exclusive)]] &^checked(){bad()}')
    checked = directory / "checked"
    run([SOLVER, source, "--emit-c", checked])
    client.write_text('#include <stdlib.h>\n#define NATIVE_BAD(q) do {(q).r0=2;} while(0)\n'
                      '#include "vx_bind_checked.h"\nvoid vx_trap(uint8_t code){exit(code);}\n'
                      'int main(void){VX_BIND_checked_LOCAL;vx_bind_checked_run();return 0;}\n')
    run(["cc", "-std=c11", "-O2", "-I" + str(checked), "-I" + str(runtime), client, "-o", binary])
    assert run([binary], ok=False).returncode == 16
    source.write_text('[[c_expansion(NATIVE)]] &^entry(){0}')
    failed = run([SOLVER, source], ok=False)
    assert failed.returncode != 0 and "external function" in failed.stderr
print("Native boundaries: lifetime reuse, reversed handoffs, retained snapshots, callbacks, fixed/automatic storage, expansion and fallback passed" + (" (SDCC/Z80 too)" if args.msx else " (UBSan)"))
