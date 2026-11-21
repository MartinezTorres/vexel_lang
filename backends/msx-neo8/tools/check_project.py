#!/usr/bin/env python3
"""Check measured placement against the target contract, after SDCC + linking.

No stack proof is inferred from C locals or object size. Facts in resolution.json
apply to this ROM only; they are not speculative source-language constants.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys


def fingerprint(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f"{value:016x}"


def check_bindings(plan, areas):
    """Absolute external locations cannot occupy activation-owned RAM.

    Memory and port addresses are separate spaces. Explicit external aliases
    remain aliases; only collisions with generated ownership are rejected.
    Addresses above stack_top are outside this allocator (e.g. BIOS variables).
    """
    ram = [area for area in areas if area["address"] >= plan["ram_start"] and area["bytes"]]
    for binding in plan.get("storage_bindings", []):
        space, begin, size = binding["space"], binding["address"], binding["bytes"]
        if type(begin) is not int or type(size) is not int or begin < 0 or size < 0:
            raise ValueError("invalid storage binding interval: " + binding["name"])
        if space == "port":
            if begin > 255 or size != 1:
                raise ValueError("invalid MSX port binding: " + binding["name"])
            continue
        if space != "address" or begin > 65535 or begin + size > 65536:
            raise ValueError("invalid MSX memory binding: " + binding["name"])
        if not size:
            continue
        end = begin + size
        if begin < plan["stack_top"] and plan["resident_limit"] < end:
            raise ValueError("storage binding overlaps reserved stack: " + binding["name"])
        for area in ram:
            if begin < area["address"] + area["bytes"] and area["address"] < end:
                raise ValueError("storage binding overlaps linked RAM area " + area["module"] +
                                 ":" + area["area"] + ": " + binding["name"])


def check(plan_path, rom_path, output_path):
    # Also usable through importlib by external validation drivers.
    spec = importlib.util.spec_from_file_location('vexel_stack_usage', Path(__file__).with_name('stack_usage.py'))
    stack_usage = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(stack_usage)
    analyze_stack = stack_usage.analyze
    output_path.unlink(missing_ok=True)  # A failed candidate has no current report.
    plan = json.loads(plan_path.read_text())
    rom = rom_path.read_bytes()
    if rom[:2] != b"AB" or rom[16:24] != b"ROM_NEO8":
        raise ValueError("missing MSX AB header or NEO-8 signature")
    if len(rom) % 8192 or len(rom) > plan["rom_segments"] * 8192:
        raise ValueError("ROM image exceeds the selected physical mapper capacity")
    areas = []
    for line in Path(str(rom_path) + ".areas.map").read_text().splitlines():
        cells = [v.strip() for v in line.split("#")]
        if len(cells) < 13:
            continue
        try:
            segment, address, size = (int(cells[i], 16) for i in (1, 2, 4))
        except ValueError:
            continue
        occupants = [(i - 6, v) for i, v in enumerate(cells[6:13], 6) if v]
        if len(occupants) != 1:
            raise ValueError("ambiguous megalinker area map row")
        column, module = occupants[0]
        location = ["header", "Y", "Z", "A", "B", "C", "D"][column]
        if location in ("Y", "Z"):
            raise ValueError("BIOS-compatible profile cannot place modules in page 0")
        if segment >= plan["rom_segments"]:
            raise ValueError("placed segment exceeds mapper capacity")
        if size and address < 0xc000 and location != "header":
            expected = {"A": 2, "B": 3, "C": 4, "D": 5}[location]
            if address // 8192 != expected:
                raise ValueError("banked area address disagrees with its selected NEO window")
            if address // 8192 != (address + size - 1) // 8192:
                raise ValueError("area crosses a NEO-8 window")
        rom_offset = None if cells[3] == "-----" else int(cells[3], 16) - 0x4000
        if rom_offset is not None and (rom_offset < 0 or rom_offset + size > len(rom)):
            raise ValueError("section contents lie outside the emitted ROM")
        areas.append({"module": module, "area": cells[5], "segment": segment,
                      "address": address, "bytes": size, "window": location, "rom_offset": rom_offset})
    if not areas:
        raise ValueError("no placements found in megalinker area map")
    ram = [a for a in areas if a["address"] >= plan["ram_start"]]
    ram_end = max((a["address"] + a["bytes"] for a in ram), default=plan["ram_start"])
    if ram_end > plan["resident_limit"]:
        raise ValueError(f"resident RAM ends at 0x{ram_end:04X}, beyond stack boundary "
                         f"0x{plan['resident_limit']:04X}")
    intervals = sorted((a["address"], a["address"] + a["bytes"]) for a in ram if a["bytes"])
    if any(b[0] < a[1] for a, b in zip(intervals, intervals[1:])):
        raise ValueError("overlapping resident RAM areas")
    check_bindings(plan, areas)
    mapper=plan.get('mapper_storage', {})
    capacity=plan.get('ram_mapper_guaranteed_segments', 0)*16384
    allocations=sorted((item['offset'],item['offset']+item['bytes']) for item in mapper.get('allocations', []))
    if any(start<0 or end<start or end>capacity for start,end in allocations) or any(b[0]<a[1] for a,b in zip(allocations,allocations[1:])):
        raise ValueError('mapper storage exceeds supplied segment ownership')
    rom_intervals = sorted((a["rom_offset"], a["rom_offset"] + a["bytes"])
                           for a in areas if a["rom_offset"] is not None and a["bytes"])
    if any(b[0] < a[1] for a, b in zip(rom_intervals, rom_intervals[1:])):
        raise ValueError("overlapping ROM section contents")
    project = plan_path.resolve().parent.parent
    overrides = [str(p.relative_to(project)) for p in (project / "user").glob("vxp_*")
                 if p.suffix in (".c", ".s")]
    static_names = {f.get("module", "vxp_" + f["name"]) for f in plan["functions"]
                    if f["frame_policy"] == "static-exclusive"}
    closed_frames = bool(plan.get("frame_planning", {}).get("static_local_bytes", 0))
    if closed_frames and overrides:
        raise ValueError("handwritten override invalidates closed-call-graph frame reasoning; "
                         "resolve with --frame-ram-bytes 0 before replacing modules")
    if any(Path(p).stem in static_names for p in overrides):
        raise ValueError("handwritten override invalidates static-frame reasoning; "
                         "export that entry with automatic frames before replacing it")
    originals = json.loads((project / "generated/source-fingerprints.json").read_text())
    modified = [name for name, digest in originals.items()
                if not (project / name).is_file() or fingerprint((project / name).read_bytes()) != digest]
    if closed_frames and any((name.startswith(("runtime/", "generated/")) and Path(name).suffix in (".c", ".h", ".s"))
                             or name in ("generated/source-plan.json", "generated/plan.json")
                             for name in modified):
        raise ValueError("edited code or contracts invalidate closed-call-graph frame reasoning; "
                         "resolve with --frame-ram-bytes 0 before editing generated code")
    if static_names and any(name.startswith("runtime/") or name == "generated/vexel.h"
                            or name == "generated/vx_bridges.c"
                            or (name.startswith("generated/") and Path(name).stem in static_names)
                            for name in modified):
        raise ValueError("edited generated code invalidates static-frame reasoning; "
                         "use automatic frames before changing the runtime or exclusive modules")
    inputs = {str(p.relative_to(project)): hashlib.sha256(p.read_bytes()).hexdigest()
              for directory in ("generated", "runtime", "user")
              for p in sorted((project / directory).iterdir())
              if p.is_file() and p.suffix in (".c", ".h", ".s", ".json", ".mk")}
    for name in ("Makefile", "local.mk", "tools/check_project.py", "tools/stack_usage.py"):
        inputs[name] = hashlib.sha256((project / name).read_bytes()).hexdigest()
    stack=analyze_stack(project,plan)
    if stack['status']=='bounded' and stack['bytes']>plan['stack_bytes']:
        raise ValueError(f"source stack bound {stack['bytes']} exceeds reservation {plan['stack_bytes']}")
    if plan.get('configuration',{}).get('stack_proof') and stack['status']!='bounded':
        raise ValueError('required stack proof is open: '+json.dumps(stack['roots'],sort_keys=True))
    result = {
        "format": "vexel-msx-measurements-1", "rom_sha256": hashlib.sha256(rom).hexdigest(),
        "rom_bytes": len(rom), "resident_bytes": ram_end - plan["ram_start"],
        "resident_limit": plan["resident_limit"], "resident_end": ram_end,
        "resident_free_bytes": plan["resident_limit"] - ram_end,
        "stack_reserved_bytes": plan["stack_bytes"], "stack_proof": stack['status'], "stack_analysis":stack,
        "checks": {"header": True, "mapper_capacity": True, "resident_ram": True,
                   "page_zero_preserved": True, "storage_bindings": True, "mapper_ownership":True},
        "handwritten_overrides": overrides,
        "modified_export_files": modified, "input_sha256": inputs,
        "toolchain": (rom_path.parent / "toolchain.txt").read_text(),
        "object_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in sorted(rom_path.parent.glob("*.rel"))},
        "manual_code_semantics": "outside Vexel proof; preserve documented ABI and mapping contract",
        "areas": areas,
    }
    output_path.write_text(json.dumps(result, indent=2) + "\n")
    print(f"NEO-8: {len(rom)} ROM bytes; {result['resident_bytes']} resident RAM bytes; "
          f"{plan['stack_bytes']} stack bytes reserved ({stack['status']} for source entries)")


if __name__ == "__main__":
    try:
        check(*(Path(p) for p in sys.argv[1:]))
    except (ValueError, OSError, KeyError) as error:
        sys.exit(f"target constraint failed: {error}")
