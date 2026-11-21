"""Exercise selected storage with one unchanged C client and existing oracles."""
import json
import subprocess


def check_bindings(root, solver, directory, source_text, vectors, run, msx, backend=None):
    source = directory / "bindings.vx"
    source_text += "\n&^cadence(cycle:#u8,slot:#u8){((cycle+slot)&3)==0 ? (#u8)1 : (#u8)0}\n"
    source_text += "&^aligned(tag:#u8,x:#u32){(#u32)tag+x}\n"
    vectors = dict(vectors, cadence=[([cycle, slot], [int((cycle+slot)&3 == 0)])
        for cycle in range(8) for slot in range(4,8)],
        aligned=[([tag, x], [tag+x]) for tag in (0, 255) for x in (0, 65535, 2147483392)])
    text = "[[prefer(value_frame_bytes, 64+64)]] allowance=128;\npriority=20;\n" + source_text
    names = ("byte", "shared", "partial", "choose", "bounded", "cadence", "aligned")
    for name in names:
        marker = ("&^" + name + "(")
        tags = f"[[c_binding({name},full,{'priority*10' if name == 'shared' else 'priority'},exclusive)]]\n"
        if name == "shared":
            tags += "[[c_binding(open_shared,full,1,open)]]\n"
        if name == "choose":
            tags += "[[c_binding(choose0,case0,2,exclusive)]]\n[[c_binding(choose1,case1,2,exclusive)]]\n"
        text = text.replace(marker, tags + marker)
    source.write_text(text)
    runtime = root / "backends/msx-neo8/runtime"
    client = None
    generated_sets = {}
    exclusion = "[[c_disjoint(" + ",".join(names + ("open_shared", "choose0", "choose1")) + ")]]\n"
    for label, budget, facts in (("0", 0, ""), ("12", 12, ""), ("128", 128, ""),
                                  ("unused", 128, exclusion+'[[prefer("binding_frequency.shared",0)]] inactive=0;\n'),
                                  ("overlay", 12, exclusion)):
        source.write_text(facts + text)
        generated = directory / f"bindings-{label}"
        args = [solver, source, "--prefer", f"value_frame_bytes={budget}", "--emit-c", generated]
        if label == "unused":
            # A later CLI hint cannot raise a source's frequency cap. The
            # same client still calls this automatic binding successfully.
            args += ["--prefer", "binding_frequency.shared=10"]
        result = json.loads(run(args).stdout)
        plan = result["value_bindings"]
        entries = {b["name"]: b for b in plan["bindings"]}
        assert plan["ram_budget"] == budget
        assert plan["static_bytes"] == sum(s["bytes"] for s in plan["slots"]) <= budget
        assert plan["unshared_static_bytes"] == sum(b["bytes"] for b in entries.values() if b["storage"] == "fixed")
        if not facts:
            assert plan["static_bytes"] == plan["unshared_static_bytes"]
        else:
            assert len(plan["slots"]) == 1 and plan["static_bytes"] < plan["unshared_static_bytes"]
        assert entries["open_shared"]["storage"] == entries["bounded"]["storage"] == "automatic"
        assert not entries["open_shared"]["eligible"] and not entries["bounded"]["eligible"]
        if label == "unused":
            assert entries["shared"]["storage"] == "automatic" and entries["shared"]["eligible"]
            assert entries["shared"]["frequency"] == 0
        elif budget:
            assert entries["shared"]["storage"] == "fixed"
        if label == "12":
            assert entries["partial"]["reason"] == "RAM budget"
        if budget == 128 or facts:
            assert all(b["storage"] == "fixed" for b in entries.values() if b["eligible"] and b["frequency"])
        generated_sets[label] = generated
        if client is None:
            headers = "".join(f'#include "{b["header"]}"\n' for b in entries.values())
            tables, declarations, checks = [], [], []
            for number, (name, binding) in enumerate(entries.items(), 1):
                if name == "open_shared":
                    continue
                original = binding["source"]
                rows = vectors[original]
                if name in ("choose0", "choose1"):
                    rows = [r for r in rows if r[0][0] == int(name[-1])]
                # The original integer oracle supplies these expected values.
                declarations.append(f"extern const int32_t t_{name}[{len(rows)}][{len(rows[0][0])+len(rows[0][1])}];\n")
                tables.append(f"const int32_t t_{name}[][ {len(rows[0][0])+len(rows[0][1])} ]={{" +
                    ",".join("{" + ",".join(str(v) for v in a + b) + "}" for a, b in rows) + "};\n")
                assignments = "".join(f"vx_bind_{name}_frame.a{i}=t_{name}[i][{i}];" for i in binding["inputs"])
                assertions = " || ".join(f"vx_bind_{name}_frame.result.v{j}!=t_{name}[i][{len(rows[0][0])+j}]"
                                         for j in range(len(rows[0][1])))
                checks.append(f"{{VX_BIND_{name}_LOCAL;for(i=0;i<{len(rows)};++i){{{assignments}"
                              f"vx_bind_{name}_run();if({assertions})return {number};}}}}\n")
            (directory / "binding_vectors.h").write_text('#include <stdint.h>\n' + "".join(declarations))
            (directory / "binding_vectors.c").write_text('#include "binding_vectors.h"\n' + "".join(tables))
            client = headers + '#include "binding_vectors.h"\n' + "uint32_t check_bindings(void){uint16_t i;\n" + "".join(checks) + """
    {VX_BIND_open_shared_LOCAL;
     vx_bind_open_shared_frame.a0=254;vx_bind_open_shared_run();
     {VX_BIND_open_shared_LOCAL;vx_bind_open_shared_frame.a0=7;vx_bind_open_shared_run();}
     if(vx_bind_open_shared_frame.result.v0!=254 || vx_bind_open_shared_frame.result.v2!=0)return 90;}
    {VX_BIND_shared_LOCAL;vx_shared_result saved;
     vx_bind_shared_frame.a0=254;vx_bind_shared_run();saved=vx_bind_shared_frame.result;
     {VX_BIND_aligned_LOCAL;vx_bind_aligned_frame.a0=255;vx_bind_aligned_frame.a1=65535;
      vx_bind_aligned_run();if(vx_bind_aligned_frame.result.v0!=65790UL)return 93;}
     vx_bind_shared_frame.a0=7;vx_bind_shared_run();
     if(saved.v0!=254 || saved.v2!=0 || vx_bind_shared_frame.result.v0!=7)return 91;}
    {uint8_t cycle,slot;VX_BIND_cadence_LOCAL;
     for(cycle=0;cycle<8;++cycle)for(slot=4;slot<8;++slot){
       vx_bind_cadence_frame.a0=cycle;vx_bind_cadence_frame.a1=slot;
       vx_bind_cadence_run();
       if(vx_bind_cadence_frame.result.v0!=(((cycle+slot)&3)==0))return 92;}}
    return 0;}
"""
        host = directory / "binding-host.c"
        host.write_text(client + """
#include <setjmp.h>
static jmp_buf trapped;
void vx_trap(uint8_t code){longjmp(trapped,code);}
int main(void){int code;VX_BIND_bounded_LOCAL;
    if(check_bindings())return 1;
    code=setjmp(trapped);
    if(!code){vx_bind_bounded_frame.a0=8;vx_bind_bounded_run();return 2;}
    return code!=16;
}
""")
        binary = directory / "binding-host"
        run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-fsanitize=undefined", "-fno-sanitize-recover=all",
             "-I" + str(generated), "-I" + str(runtime), host, directory / "binding_vectors.c",
             generated / "vx_binding_frames.c", "-o", binary])
        run([binary])
        if msx and (budget in (0, 128) or facts):
            project = directory / f"binding-project-{label}"
            backend.package(generated, project, source, solver)
            # Keep each resident object within its 8 KiB ROM source window.
            for file in ("binding_vectors.h", "binding_vectors.c"):
                (project / "user" / file).write_bytes((directory / file).read_bytes())
            # Explicit embedding storage is compiled once, alongside the client.
            (project / "user/frames.c").write_text('#include "vx_binding_frames.c"\n')
            (project / "user/main.c").write_text(client + """
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {vx_last_result=check_bindings();__asm halt __endasm;}
""")
            run(["make", "-C", project, "-j2", "simulate", "SIMFLAGS=--max-ticks=400000000"])
            execution = json.loads((project / "build/execution.json").read_text())
            assert execution["observation"]["last_result"] == execution["observation"]["fault"] == 0, execution
    # Scheduling/branch traversal cannot alter a storage plan's deterministic ties.
    reverse = directory / "binding-reverse"
    run([solver, source, "--prefer", "value_frame_bytes=12", "--schedule", "lifo", "--branch-order", "reverse", "--emit-c", reverse])
    for header in generated_sets["overlay"].glob("vx_bind*.h"):
        assert header.read_bytes() == (reverse / header.name).read_bytes()
    # Fact location and member order do not constrain binding discovery.
    source.write_text(text + "\n[[c_disjoint(" + ",".join(reversed(names +
        ("open_shared", "choose0", "choose1"))) + ")]] late_fact=0;\n")
    reordered = directory / "binding-reordered"
    run([solver, source, "--prefer", "value_frame_bytes=12", "--emit-c", reordered])
    for header in generated_sets["overlay"].glob("vx_bind*.h"):
        assert header.read_bytes() == (reordered / header.name).read_bytes()
    # Pairwise exclusion is not transitive. Keep shared's result live across
    # byte: neither may reuse the other's slot just because both exclude partial.
    source.write_text("[[c_disjoint(shared,partial)]] [[c_disjoint(partial,byte)]]\n" + text)
    separate = directory / "binding-nontransitive"
    plan = json.loads(run([solver, source, "--emit-c", separate]).stdout)["value_bindings"]
    entries = {b["name"]: b for b in plan["bindings"]}
    assert entries["shared"]["slot"] != entries["byte"]["slot"]
    host.write_text('''#include "vx_bind_shared.h"
#include "vx_bind_byte.h"
int main(void) {
    VX_BIND_shared_LOCAL; VX_BIND_byte_LOCAL;
    vx_bind_shared_frame.a0=254;vx_bind_shared_run();
    vx_bind_byte_frame.a0=19;vx_bind_byte_frame.a1=23;vx_bind_byte_frame.a2=-17;vx_bind_byte_run();
    return vx_bind_shared_frame.result.v0!=254 || vx_bind_shared_frame.result.v2!=0;
}
''')
    run(["cc", "-std=c11", "-O2", "-fsanitize=undefined", "-fno-sanitize-recover=all",
         "-I" + str(separate), "-I" + str(runtime), host, separate / "vx_binding_frames.c", "-o", binary])
    run([binary])
    source.write_text(exclusion + text)
    overlap = json.loads(run([solver, source, "--mode", "overlap"]).stdout)["value_bindings"]
    assert overlap["static_bytes"] == 0 and all(not b["eligible"] for b in overlap["bindings"])
    # Identities are labels, not bare C member names: digits and C keywords
    # still need safe names in a shared union.
    source.write_text('''[[prefer(value_frame_bytes,2)]] [[c_disjoint("9",int)]] budget=2;
[[c_binding("9",full,1,exclusive)]] &^f(x:#u8){x+1}
[[c_binding(int,full,1,exclusive)]] &^g(x:#u8){x+2}
''')
    separate = directory / "binding-labels"
    run([solver, source, "--emit-c", separate])
    host.write_text('''#include "vx_bind_9.h"
#include "vx_bind_int.h"
int main(void){
    VX_BIND_9_LOCAL;VX_BIND_int_LOCAL;
    vx_bind_9_frame.a0=5;vx_bind_9_run();if(vx_bind_9_frame.result.v0!=6)return 1;
    vx_bind_int_frame.a0=7;vx_bind_int_run();return vx_bind_int_frame.result.v0!=9;
}
''')
    run(["cc", "-std=c11", "-O2", "-I" + str(separate), "-I" + str(runtime),
         host, separate / "vx_binding_frames.c", "-o", binary])
    run([binary])
    invalid = directory / "binding-invalid.vx"
    for bad in ("[[c_binding(a,wrong,1,exclusive)]] &^f(x:#u8){x}",
                "[[c_binding(a,full,1,exclusive)]] &^f(x:#u8,n:#u8){x/n}",
                "[[c_binding(a,case0,1,exclusive)]] &^f(x:#u8){x}",
                "[[c_binding(a,full,1,exclusive)]] [[c_binding(a,full,2,open)]] &^f(x:#u8){x}",
                "[[c_disjoint(a,missing)]] [[c_binding(a,full,1,exclusive)]] &^f(x:#u8){x}",
                '[[prefer("binding_frequency.missing",0)]] [[c_binding(a,full,1,exclusive)]] &^f(x:#u8){x}',
                '[[require("binding_frequency.a",0)]] [[c_binding(a,full,1,exclusive)]] &^f(x:#u8){x}',
                "[[c_disjoint(a,a)]] [[c_binding(a,full,1,exclusive)]] &^f(x:#u8){x}",
                "[[c_disjoint(a)]] [[c_binding(a,full,1,exclusive)]] &^f(x:#u8){x}"):
        invalid.write_text(bad)
        rejected = subprocess.run([str(solver), str(invalid)], capture_output=True, text=True)
        assert rejected.returncode != 0, rejected.stdout
    print("Selected bindings: unchanged client under zero/partial/full/overlaid budgets, nontransitive lifetime contracts, callbacks, mixed alignment, cases and deterministic selection passed" +
          (" (host UBSan and Z80)" if msx else " (UBSan)"))
