#!/usr/bin/env python3
"""Typed effect regions: loop-carried state, callbacks, copies and storage."""
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

def run(command, **kwargs):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=240, **kwargs)
    assert result.returncode == 0, f"{command}\n{result.stdout}\n{result.stderr}"
    return result

SOURCE = """
&!observe(x:#u8)->#u8;
&!pulse()->#u8;
&!publish(x:#u8);
[[c_binding(helper,full,10,exclusive)]] &^helper(x:#u8){x+17}
[[c_binding(steps,region,50,exclusive)]] &^steps(a:#u8,b:#u8,n:#u8){
    x:#u8=a;y:#u8=b;i:#u8=0;
    (i<n)!@{
        t:#u8=x;x=y;y=t;
        pulse();first=observe(x);second=observe(y);publish(first);
        x+=first+second;i+=1;
    };
    (x,y)
}
[[c_binding(nested,region,20,exclusive)]] &^nested(x:#u16,n:#u8){
    value:#u16=x;i:#u8=0;
    (i<n)!@{j:#u8=0;(j<2)!@{value+=31;j+=1;};i+=1;};value
}
[[c_binding(partial,region,10,exclusive)]] &^partial(n:#u8){
    x:#u16;i:#u8=0;(i<3)!@{n<128?{x=(#u16)n+(#u16)i+1;};i+=1;};n<128?x:(#u16)0
}
[[c_binding(reentrant,region,5,open)]] &^reentrant(n:#u8){observe(n)+1}
[[c_binding(final_only,region,10,exclusive)]] &^final_only(n:#u8){
    x:#u16=7;i:#u8=0;(i<n)!@{x=19;i+=1;};x
}
// The old outer value decides which inner result is selected. Coalescing
// their fields before preserving that decision would change the answer.
[[c_binding(guards,region,10,exclusive)]] &^guards(n:#u8){
    x:#u8=n;i:#u8=0;
    (i<3)!@{x<128?{j:#u8=0;(j<2)!@{x+=91;j+=1;};};i+=1;};
    x
}
[[c_binding(control,region,10,exclusive)]] &^control(n:#u8){
    x:#u8=n;i:#u8=0;
    ({x+=1;i<8})!@{publish(i);i+=1;i==2?{->>;};x+=7;i==5?{->|;};};
    (x,i)
}
// Only the middle snapshot must survive another publication. The branch
// makes that lifetime path dependent, and the last result remains a value.
[[c_binding(snapshots,region,10,exclusive)]] &^snapshots(n:#u8){
    a=observe(n);publish(a);
    b=observe(n+1);
    (n&1)!=0 ? {c=observe(n+2);publish(c);};
    publish(b);d=observe(n+3);publish(d);(b,d)
}
// A decision can discard Boolean materialization but must retain numeric
// results, narrowing-to-zero, negative truth and the short-circuit order.
[[c_binding(decisions,region,10,exclusive)]] &^decisions(x:#u16,y:#u8){
    v:#u16=y<128 ? x : (#u16)256;b:#u8=(#u8)v;
    b!=0 ? {publish(1);} : {publish(2);};
    v!=0 ? {publish(4);} : {publish(8);};
    ((#u8)(b+3)<30 && y<38) || (y==255 && (#i16)x<0) ? {publish(16);} : {publish(32);};
    (v,b)
}
"""
CLIENT = r'''
#include "vx_bind_helper.h"
#include "vx_bind_steps.h"
#include "vx_bind_nested.h"
#include "vx_bind_partial.h"
#include "vx_bind_reentrant.h"
#include "vx_bind_final_only.h"
#include "vx_bind_guards.h"
#include "vx_bind_control.h"
#include "vx_bind_snapshots.h"
#include "vx_bind_decisions.h"
static uint8_t observations;
static uint8_t pulses,publications;
static uint16_t effect_hash;
static void publish(uint8_t x) {effect_hash=(uint16_t)(effect_hash*3+x);}
VX_BIND_snapshots_BOUNDARY_observe {VX_BIND_snapshots_DATA_observe.r0=(uint8_t)(VX_BIND_snapshots_DATA_observe.a0+17);}
VX_BIND_snapshots_BOUNDARY_publish {publish(VX_BIND_snapshots_DATA_publish.a0);}
VX_BIND_decisions_BOUNDARY_publish {publish(VX_BIND_decisions_DATA_publish.a0);}
VX_BIND_control_BOUNDARY_publish {publish(VX_BIND_control_DATA_publish.a0);}
VX_BIND_steps_BOUNDARY_pulse {++pulses;VX_BIND_steps_DATA_pulse.r0=201;}
VX_BIND_steps_BOUNDARY_publish {++publications;}
VX_BIND_steps_BOUNDARY_observe {
    VX_BIND_helper_LOCAL;
    vx_bind_helper_frame.a0=VX_BIND_steps_DATA_observe.a0;
    vx_bind_helper_run();
    VX_BIND_steps_DATA_observe.r0=vx_bind_helper_frame.result.v0;
    ++observations;
}
VX_BIND_reentrant_BOUNDARY_observe {
    VX_BIND_reentrant_LOCAL;
    if(VX_BIND_reentrant_DATA_observe.a0) {
        vx_bind_reentrant_frame.a0=VX_BIND_reentrant_DATA_observe.a0-1;
        vx_bind_reentrant_run();
        VX_BIND_reentrant_DATA_observe.r0=vx_bind_reentrant_frame.result.v0;
    } else VX_BIND_reentrant_DATA_observe.r0=5;
}
static uint32_t check(void) {
    static const uint16_t words[]={0,1,255,256,257,32768,65535};
    static const uint8_t bytes[]={0,1,26,27,37,38,127,128,254,255};
    uint8_t n,k,x,y,tmp,a;
    uint16_t expected,v;
    VX_BIND_steps_LOCAL;
    VX_BIND_nested_LOCAL;
    VX_BIND_partial_LOCAL;
    VX_BIND_reentrant_LOCAL;
    VX_BIND_final_only_LOCAL;
    VX_BIND_guards_LOCAL;
    VX_BIND_control_LOCAL;
    VX_BIND_snapshots_LOCAL;
    VX_BIND_decisions_LOCAL;
    vx_bind_reentrant_frame.a0=3;vx_bind_reentrant_run();
    if(vx_bind_reentrant_frame.result.v0!=9)return 4;
    for(a=0;a<8;++a)for(n=0;n<5;++n) {
        x=(uint8_t)(a*37);y=(uint8_t)(255-a);observations=pulses=publications=0;
        vx_bind_steps_frame.a0=x;vx_bind_steps_frame.a1=y;vx_bind_steps_frame.a2=n;
        for(k=0;k<n;++k) {tmp=x;x=y;y=tmp;x=(uint8_t)(x+(uint8_t)(x+17)+(uint8_t)(y+17));}
        vx_bind_steps_run();
        if(vx_bind_steps_frame.result.v0!=x || vx_bind_steps_frame.result.v1!=y || observations!=2*n || pulses!=n || publications!=n)return 1;
        vx_bind_nested_frame.a0=65530;vx_bind_nested_frame.a1=n;vx_bind_nested_run();
        if(vx_bind_nested_frame.result.v0!=(uint16_t)(65530+62*n))return 2;
        vx_bind_partial_frame.a0=(uint8_t)(a*37);vx_bind_partial_run();
        if(vx_bind_partial_frame.result.v0!=((uint8_t)(a*37)<128 ? (uint8_t)(a*37)+3 : 0))return 3;
        vx_bind_final_only_frame.a0=n;vx_bind_final_only_run();
        if(vx_bind_final_only_frame.result.v0!=(n ? 19 : 7))return 5;
        x=(uint8_t)(a*37);
        for(k=0;k<3;++k)if(x<128)x=(uint8_t)(x+182);
        vx_bind_guards_frame.a0=(uint8_t)(a*37);vx_bind_guards_run();
        if(vx_bind_guards_frame.result.v0!=x)return 6;
        effect_hash=0;vx_bind_control_frame.a0=(uint8_t)(a*37);vx_bind_control_run();
        if(vx_bind_control_frame.result.v0!=(uint8_t)(a*37+33) || vx_bind_control_frame.result.v1!=5 || effect_hash!=58)return 7;
    }
    for(a=0;a<sizeof(bytes);++a) {
        n=bytes[a];effect_hash=0;publish((uint8_t)(n+17));
        if(n&1)publish((uint8_t)(n+19));
        publish((uint8_t)(n+18));publish((uint8_t)(n+20));expected=effect_hash;effect_hash=0;
        vx_bind_snapshots_frame.a0=n;vx_bind_snapshots_run();
        if(effect_hash!=expected || vx_bind_snapshots_frame.result.v0!=(uint8_t)(n+18) ||
           vx_bind_snapshots_frame.result.v1!=(uint8_t)(n+20))return 8;
        for(k=0;k<sizeof(words)/sizeof(*words);++k) {
            v=n<128 ? words[k] : 256;x=(uint8_t)v;effect_hash=0;
            publish(x ? 1 : 2);publish(v ? 4 : 8);
            publish(((uint8_t)(x+3)<30 && n<38) || (n==255 && words[k]>=32768) ? 16 : 32);
            expected=effect_hash;effect_hash=0;
            vx_bind_decisions_frame.a0=words[k];vx_bind_decisions_frame.a1=n;vx_bind_decisions_run();
            if(effect_hash!=expected || vx_bind_decisions_frame.result.v0!=v || vx_bind_decisions_frame.result.v1!=x)return 9;
        }
    }
    return 0;
}
'''
runtime = ROOT / "backends/msx-neo8/runtime"
with tempfile.TemporaryDirectory(prefix="vexel-regions-") as tmp:
    directory = Path(tmp)
    source = directory / "regions.vx"
    source.write_text(SOURCE)
    sizes = {}
    for schedule, budget in ((0, 1024), (1, 1024), (1, 0)):
        generated = directory / f"g{schedule}-{budget}"
        plan = json.loads(run([SOLVER, source, "--prefer", f"region_copy_schedule={schedule}",
                              "--prefer", f"value_frame_bytes={budget}", "--emit-c", generated]).stdout)
        bindings = {b["name"]: b for b in plan["value_bindings"]["bindings"]}
        assert all(b["storage"] == ("fixed" if budget and b["name"] != "reentrant" else "automatic") for b in bindings.values())
        assert len({b["slot"] for b in bindings.values()}) == (10 if budget else 1)
        assert bindings["steps"]["codegen"]["region_boundaries"] == 4
        assert bindings["nested"]["codegen"]["region_loops"] == 2
        assert bindings["nested"]["codegen"]["region_coalesced_bytes"] > 0
        assert bindings["snapshots"]["codegen"]["region_coalesced_bytes"] > 0
        assert bindings["decisions"]["codegen"]["region_direct_decisions"] > 0
        sizes[schedule] = bindings["steps"]["bytes"]
        client = directory / "host.c"
        client.write_text(CLIENT + '''
#include <stdlib.h>
void vx_trap(uint8_t code) {exit(code);}
int main(void) {return check();}
''')
        binary = directory / "host"
        run(["cc", "-std=c11", "-O2", "-fsanitize=undefined", "-fno-sanitize-recover=all",
             "-I" + str(generated), "-I" + str(runtime), client, generated / "vx_binding_frames.c", "-o", binary])
        run([binary])
        if args.msx:
            spec = importlib.util.spec_from_file_location("project", ROOT / "experimental/relational/project_msx.py")
            backend = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(backend)
            project = directory / f"p{schedule}-{budget}"
            backend.package(generated, project, source, SOLVER)
            (project / "user/main.c").write_text('#include "vx_binding_frames.c"\n' + CLIENT + '''
volatile uint32_t vx_last_result;
void vx_external_observe(uint32_t *out,uint8_t x) VX_ABI VX_RESIDENT {out[0]=(uint8_t)(x+17);}
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check();__asm halt __endasm;}
''')
            run(["make", "-C", project, "-j2", "simulate", "SIMFLAGS=--max-ticks=400000000"])
            execution = json.loads((project / "build/execution.json").read_text())
            assert execution["observation"]["last_result"] == execution["observation"]["fault"] == 0, execution
    assert sizes[1] < sizes[0], sizes
    source.write_text('[[c_binding(overlap,region,1,exclusive)]] &^overlap(n:#u8){i:#u8=0;(i<n)!@{i+=1;};i}')
    overlap = json.loads(run([SOLVER, source, "--mode", "overlap", "--prefer", "value_frame_bytes=1024"]).stdout)
    assert overlap["value_bindings"]["static_bytes"] == 0
    # A region annotation is a request for a certified realization. Unsupported
    # effects must fail, never disappear from an otherwise successful header.
    source.write_text('[[c_binding(bad,region,1,exclusive)]] &^bad(x:#u8,n:#u8){x<<n}')
    failed = subprocess.run([str(SOLVER), str(source), "--emit-c", str(directory / "bad")], capture_output=True, text=True)
    assert failed.returncode != 0 and "shift count" in failed.stderr + failed.stdout
print("Execution regions: independent loop/callback oracle, disjoint workspaces, scheduled copies and automatic fallback passed" + (" (SDCC/Z80 too)" if args.msx else " (UBSan)"))
