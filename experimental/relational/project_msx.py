#!/usr/bin/env python3
"""Package a resolved relational C projection using the existing MSX assets.

This module has no Vexel evaluator and never invokes the established frontend.
The source executable supplies both the checked plan and its C projection.
"""
import hashlib
import json
from pathlib import Path
import shutil

REPO = Path(__file__).resolve().parents[2]
ASSETS = REPO / "backends/msx-neo8"


def fingerprint(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f"{value:016x}"


def package(generated, project, source, machine):
    generated, project = Path(generated), Path(project)
    report = json.loads((generated / "source-plan.json").read_text())
    if report["status"] != "resolved" or not report.get("sources"):
        raise ValueError("a closed source realization is required")
    def files(inputs, kind):
        digests = {}
        for item in inputs:
            data = Path(item["path"]).read_bytes()
            if fingerprint(data) != item["fnv1a"]:
                raise ValueError(kind + " input changed after resolution: " + item["path"])
            digests[item["path"]] = hashlib.sha256(data).hexdigest()
        return digests
    source_inputs = files(report["source_inputs"], "source")
    resource_inputs = files(report.get("resource_inputs", []), "resource")
    for item in report.get("input_selections", []):
        key, expected = item["key"], item["value"]
        if key.startswith("path-exists:"):
            actual = "true" if Path(key.removeprefix("path-exists:")).exists() else "false"
        elif key.startswith("path-target:"):
            actual = str(Path(key.removeprefix("path-target:")).resolve())
        elif key.startswith("resource-kind:"):
            path = Path(key.removeprefix("resource-kind:"))
            actual = "directory" if path.is_dir() or (not path.exists() and not path.suffix) else "file"
        elif key.startswith("source-kind:"):
            actual = "file" if Path(key.removeprefix("source-kind:")).is_file() else "other"
        else:
            continue
        if actual != expected:
            raise ValueError("input selection changed after resolution: " + key)
    for item in report.get("directory_inputs", []):
        path = Path(item["path"])
        names = sorted(p.name for p in path.iterdir() if p.is_file()) if path.exists() else []
        if names != item["files"]:
            raise ValueError("resource directory changed after resolution: " + str(path))
    if project.exists() and (not project.is_dir() or any(project.iterdir())):
        raise ValueError("project directory must be new or empty")
    for directory in ("generated", "runtime", "user", "tools", "vendor", "docs"):
        (project / directory).mkdir(parents=True, exist_ok=True)
    for directory in ("runtime", "tools", "vendor"):
        shutil.copytree(ASSETS / directory, project / directory, dirs_exist_ok=True, copy_function=shutil.copyfile)
    shutil.copyfile(ASSETS / "runtime/crt0.s.in", project / "runtime/crt0.s")
    shutil.copyfile(ASSETS / "Project.mk", project / "Makefile")
    shutil.copyfile(ASSETS / "Project.md", project / "README.md")
    shutil.copyfile(REPO / "docs/sdcc-operands.md", project / "docs/sdcc-operands.md")
    shutil.copyfile(REPO / "docs/backend-msx-neo8.md", project / "docs/backend-msx-neo8.md")
    for path in generated.iterdir():
        if path.is_file():
            shutil.copyfile(path, project / "generated" / path.name)
    configuration = {key: item['value'] for key, item in report.get('configuration', {}).items()}
    stack_bytes = configuration.get('stack_bytes', 1024)
    mapper_segments = configuration.get('mapper_segments', 0)
    plan = {
        "format": "vexel-msx-plan-1", "target": "msx-neo8-bios-16k",
        "ram_start": 0xc000, "stack_top": 0xf380, "stack_bytes": stack_bytes,
        "resident_limit": 0xf380 - stack_bytes, "physical_ram_bytes": 16384,
        "rom_segment_bytes": 8192, "rom_segments": 256,
        "ram_mapper_guaranteed_segments": mapper_segments, "calling_convention": "sdcccall(0)",
        "mapper_storage": report.get('mapper_storage', {}),
        "configuration": configuration,
        "stack_proof": "unproven: reserve is a budget, not a bound",
        "functions": report.get("bank_modules", []),
        "storage_bindings": [{"name": item["name"], "type": item["type"], **item["binding"]}
                             for item in report.get("boundary_storage", []) if "binding" in item],
        "target_facts": {},
        "source_dependencies": report.get("input_dependencies", list(source_inputs)),
        "relational_output_contract": report["activation"],
        "relational_payload_bytes": report["extent"],
        "frame_planning": report.get("frame_planning", {}),
        "boundary_contracts": report.get("boundary_contracts", []),
        "access_order": report.get("access_order", []),
        "activation_graph": report.get("activation_graph", {}),
        "source_occurrences": [{"name": source["name"], "source": source.get("source", source["name"]), "stream":source.get('stream')}
                               for source in report["sources"]],
    }
    (project / "generated/plan.json").write_text(json.dumps(plan, indent=2) + "\n")
    features = report.get("runtime_features", {})
    bank_modules = report.get("bank_modules", [])
    (project / "generated/modules.mk").write_text("BANK_MODULES := " + " ".join(item["module"] for item in bank_modules) + "\nROM_SEGMENTS := 256\n"
        + ("BRIDGE_SOURCE := generated/vx_banked.c\n" if bank_modules else "")
        + "FIXED_RUNTIME := " + str(int(features.get("fixed", False))) + "\n"
        + "FLOAT_RUNTIME := " + str(int(features.get("float", False))) + "\n"
        + "MATH_RUNTIME := " + str(int(features.get("math", False))) + "\n"
        + "MAPPER_SEGMENTS := " + str(mapper_segments) + "\n"
        + "CPPFLAGS += -DVX_FIXED_RUNTIME=$(FIXED_RUNTIME) -DVX_FLOAT_RUNTIME=$(FLOAT_RUNTIME) -DVX_MATH_RUNTIME=$(MATH_RUNTIME) -DVX_MAPPER_SEGMENTS=$(MAPPER_SEGMENTS)\n")
    (project / "local.mk").write_text("# Editable SDCC and megalinker settings.\n")
    (project / "user/stack-contracts.json").write_text(json.dumps({'environment_bytes':0, 'functions':{}},indent=2)+'\n')
    anchors='\n'.join('    vx_link_root = (uint16_t)&vx_'+item['name']+('_submit' if item.get('stream') else '')+';'
                      for item in report['sources'])
    (project / "user/main.c").write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
volatile uint16_t vx_link_root;
void main(void) VX_ABI VX_RESIDENT {
    /* Finite occurrences: vx_NAME(...) and vx_NAME_read(index).
       Streams: vx_NAME_submit(...), vx_NAME_read_lease(token,index),
       vx_NAME_release(token). Follow generated/source-plan.json.
       Grant any required mapper segments before accessing mapped storage. */
    /* Keep requested interfaces linked while this adapter is still empty. */
'''+anchors+'''
    for (;;) { __asm halt __endasm; }
}
''')
    (project / "SOURCE-CONTRACT.txt").write_text(
        "This project was resolved by vexel-source without the established Vexel solver.\n"
        "The source type and stored encoding are separate in generated/source-plan.json.\n"
        "vx_NAME produces a result; vx_NAME_read reads a scalar leaf by its reported index.\n"
        "Signed result leaves use sign-extended two's-complement uint32_t values.\n"
        "Outputs stay live until their sink releases them. Follow the selected activation contract.\n"
        "Stream mode uses submit/read_lease/release; raw entry/read functions are internal scratch operations.\n"
        "A stream token is never reused; zero submission means backpressure without executing the source.\n"
        "In graph mode, each named occurrence owns at most one activation and outstanding result.\n"
        "Its begin/produce/release events and ordering are in activation_graph; source gives the definition.\n"
        "Finish and release an epoch before reusing its occurrence names.\n"
        "Source entries and materialized functions are separate window B modules.\n"
        "External adapters must honor the reported resource footprints and no-feedback contracts.\n"
        "An access-order edge ends the earlier execution before the next begins; output release is separate.\n"
        "Resident wrappers preserve the suspended caller's bank across every call.\n"
        "Replace generated modules with matching user/*.c or user/*.s files.\n"
        "A nonzero --frame-ram-bytes budget can select exclusive static locals.\n"
        "Those choices depend on the generated call graph; use a zero budget before overriding modules.\n"
        "Linked RAM/mapper checks are enforced; resolution.json records conditional assembly stack bounds.\n"
        "Supply external and environment stack contracts in user/stack-contracts.json; an unknown bound stays open.\n"
        "Edits are inspectable but their semantic correctness requires fresh verification.\n")
    sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
    provenance = {"source_sha256": sha(source), "machine_sha256": sha(machine),
                  "source_inputs_sha256": source_inputs,
                  "resource_inputs_sha256": resource_inputs,
                  "directory_inputs": report.get("directory_inputs", []),
                  "process_inputs": report.get("process_inputs", []),
                  "projection_sha256": sha(generated / "vx_bridges.c"), "plan_sha256": sha(generated / "source-plan.json")}
    (project / "generated/provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    originals = {str(path.relative_to(project)): fingerprint(path.read_bytes())
                 for path in project.rglob("*") if path.is_file()}
    (project / "generated/source-fingerprints.json").write_text(json.dumps(originals, indent=2) + "\n")
    return report
