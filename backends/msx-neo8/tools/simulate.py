#!/usr/bin/env python3
"""Record conditional machine execution evidence; never infer a stack proof."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def symbols(path):
    # Megalinker's human-readable names are truncated to 20 columns. Only
    # exact, short, unique runtime names are accepted here; no prefix matching.
    wanted = {"_vx_segment_b", "_vx_fault", "_vx_last_result"}
    result = {}
    for line in path.read_text().splitlines():
        cells = [s.strip() for s in line.split("#")]
        if len(cells) < 12:
            continue
        for name in cells[5:12]:
            if name not in wanted:
                continue
            if name in result:
                raise ValueError("ambiguous runtime symbol: " + name)
            result[name] = int(cells[2], 16)
    if not {"_vx_segment_b", "_vx_fault"} <= result.keys():
        raise ValueError("runtime symbols missing from linked map")
    return result


def simulate(args):
    args.output.unlink(missing_ok=True)
    plan = json.loads(args.plan.read_text())
    placement = json.loads(args.resolution.read_text())
    if (plan["target"] != "msx-neo8-bios-16k" or plan["ram_start"] != 0xc000 or
            plan["physical_ram_bytes"] != 16384 or plan["stack_top"] != 0xf380 or
            plan["resident_limit"] + plan["stack_bytes"] != 0xf380):
        raise ValueError("execution model does not implement this edited machine profile")
    if digest(args.rom) != placement["rom_sha256"]:
        raise ValueError("placement report belongs to a different ROM; rebuild before simulating")
    if (plan["resident_limit"] != placement["resident_limit"] or
            plan["stack_bytes"] != placement["stack_reserved_bytes"]):
        raise ValueError("placement report belongs to a different RAM/stack plan")
    symbol_path = Path(str(args.rom) + ".symbols.map")
    syms = symbols(symbol_path)
    with tempfile.TemporaryDirectory(prefix="vexel-neo8-run-") as temp:
        snapshot = Path(temp) / "ram.bin"
        command = [str(args.runner.resolve()), "--rom", str(args.rom.resolve()),
                   "--dump-ram", str(snapshot), "--max-ticks", str(args.max_ticks),
                   "--stack-floor", str(plan["resident_limit"]), "--himem", str(args.himem),
                   "--irq-period", str(args.irq_period), "--shadow", str(syms["_vx_segment_b"]),
                   "--fault", str(syms["_vx_fault"])]
        if args.expanded_slot:
            command.append("--expanded-slot")
        if args.ram_segments:
            command += ['--ram-segments', str(args.ram_segments)]
        result = subprocess.run(command, text=True, capture_output=True)
        if result.returncode:
            raise ValueError(result.stderr.strip())
        observation = json.loads(result.stdout)
        ram = snapshot.read_bytes()
    if len(ram) != 16384:
        raise ValueError("invalid machine RAM snapshot")
    if observation["fault"]:
        raise ValueError(f"Vexel runtime trapped with reason {observation['fault']}")
    if observation["page2_slot_calls"] < 1 or observation.get('mapper_page2', False):
        raise ValueError("startup did not complete cartridge slot selection")
    if observation["bank_b"] != 1:
        raise ValueError("application halted without restoring initial B mapping")
    if "_vx_last_result" in syms:
        offset = syms["_vx_last_result"] - 0xc000
        if not 0 <= offset <= len(ram) - 4:
            raise ValueError("invalid result symbol address")
        observation["last_result"] = int.from_bytes(ram[offset:offset + 4], "little")
    project = args.plan.resolve().parent.parent
    harness_inputs = ("tools/neo8_runner.cpp", "tools/simulate.py", "vendor/chips/z80.h")
    report = {
        "format": "vexel-msx-execution-1", "evidence": "workload-observation",
        "rom_sha256": digest(args.rom), "plan_sha256": digest(args.plan),
        "resolution_sha256": digest(args.resolution), "symbols_sha256": digest(symbol_path),
        "runner_sha256": digest(args.runner),
        "harness_input_sha256": {p: digest(project / p) for p in harness_inputs},
        "workload": {"entry": "cartridge-init-to-first-halt", "initial_ram_fill": 165,
                     "himem": args.himem, "expanded_slot": args.expanded_slot,
                     "irq_period_ticks": args.irq_period, "max_ticks": args.max_ticks},
        "observation": observation, "stack_proof": "unproven",
        "limitations": ["BIOS stubs, not an MSX BIOS ROM", "synthetic interrupt workload",
                        "no VDP, audio or wait-state model; mapper slot 3 is a supplied test device",
                        "first HALT is a stopping convention, not proof of application success",
                        "observed stack use is not a bound for other inputs or interrupt schedules"],
    }
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Z80 workload: {observation['ticks']} model ticks; "
          f"{observation['observed_stack_bytes']} observed stack bytes; "
          f"{observation['interrupts_accepted']} interrupts; stack bound remains unproven")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("plan", "resolution", "rom", "runner", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--max-ticks", type=int, default=10000000)
    parser.add_argument("--irq-period", type=int, default=0)
    parser.add_argument("--himem", type=lambda s: int(s, 0), default=0xf380)
    parser.add_argument("--expanded-slot", action="store_true")
    parser.add_argument("--ram-segments", type=int, default=0)
    try:
        simulate(parser.parse_args())
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f"execution constraint failed: {error}\n")
