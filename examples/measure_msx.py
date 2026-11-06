#!/usr/bin/env python3
"""A bounded, reproducible frame-policy experiment, not a general optimizer."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def command(args, cwd=None):
    env = os.environ.copy()
    for key in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES", "MAKELEVEL", "BUILD"):
        env.pop(key, None)
    if "MSX_TEST_RUNNER_CXXFLAGS" in env:
        env["RUNNER_CXXFLAGS"] = env["MSX_TEST_RUNNER_CXXFLAGS"]
    result = subprocess.run([str(a) for a in args], cwd=cwd, env=env, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"failed: {args}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def solve_plan(executable, directory, rows, observed_stack_limit):
    """Feed verified measurements back as facts; the policy itself is Vexel."""
    for row in rows:
        project = Path(row['project'])
        if hashlib.sha256((project / 'build/game.rom').read_bytes()).hexdigest() != row['rom_sha256']:
            raise ValueError('stale candidate ROM: ' + row['candidate'])
        for filename, expected in (('execution.json', row['execution_sha256']),
                                   ('resolution.json', row['placement_sha256'])):
            if hashlib.sha256((project / 'build' / filename).read_bytes()).hexdigest() != expected:
                raise ValueError('stale candidate evidence: ' + row['candidate'])
    candidate_values = ',\n'.join(
        f"    #Candidate({r['resident_bytes']},{r['rom_section_bytes']},{r['observed_stack_bytes']})"
        for r in rows)
    resident_budget = min(r['resident_budget'] for r in rows)
    source = f'''// Measured costs apply only to the recorded workload and ROM hashes.
#Candidate(ram:#u32,rom:#u32,observed_stack:#u32);
candidates=[
{candidate_values}
];
resident_budget:#u32={resident_budget};
observed_stack_limit:#u32={observed_stack_limit};
selected=?{{
    i=?(#u8,0..|candidates|);
    ?(candidates[i].ram<=resident_budget);
    ?(candidates[i].observed_stack<=observed_stack_limit);
    0..|candidates|@{{
        j=(#u8)_;
        fits=(candidates[j].ram<=resident_budget)&&(candidates[j].observed_stack<=observed_stack_limit);
        cheaper=(candidates[j].rom<candidates[i].rom)||((candidates[j].rom==candidates[i].rom)&&(j<i));
        ?(!fits||!cheaper);
    }};
    i
}};
'''
    policy = directory / f'plan-stack-{observed_stack_limit}.vx'
    policy.write_text(source)
    search = policy.with_suffix('.search.json')
    resolved = json.loads(command([executable, policy, '--root', 'selected', '--json', '--search-report', search]))
    index = int(resolved['roots'][0]['value'])
    return {'observed_stack_limit': observed_stack_limit, 'resident_budget': resident_budget,
            'selected_candidate': rows[index]['candidate'], 'project': rows[index]['project'],
            'policy': str(policy), 'search': str(search),
            'policy_sha256': hashlib.sha256(policy.read_bytes()).hexdigest(),
            'evidence_sha256': [r['execution_sha256'] for r in rows],
            'contract': 'minimize measured ROM sections subject to resident RAM and observed workload stack; index breaks ties',
            'stack_proof': 'unproven'}


def experiment(executable, output, irq_period=997):
    source = Path(__file__).with_name("neo8_frames.vx")
    inputs = [0, 1, 7, 255, 65535, 0x12345678, 0x80000000, 0xffffffff]
    oracle = [int(json.loads(command([executable, source, "--call", "sample", "--arg", x, "--json"]))
                  ["roots"][0]["value"]) for x in inputs]
    # No external callbacks or ISR calls to sample: the static contract is
    # established by this owned workload, not inferred for arbitrary programs.
    main = '#include "vexel.h"\nvolatile uint32_t vx_last_result;\nvoid main(void) VX_ABI VX_RESIDENT {\n'
    for value, expected in zip(inputs, oracle):
        main += f"    if (vx_sample({value}UL) != {expected}UL) vx_trap(6);\n"
    main += "    vx_last_result=1;\n    for (;;) { __asm halt __endasm; }\n}\n"
    output.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="run-", dir=output)).resolve()
    rows = []
    for name, flags in (("automatic", []), ("static-sample", ["--msx-exclusive", "sample"])):
        project = directory / name
        command([executable, source, "--emit-msx", project, *flags])
        (project / "user/main.c").write_text(main)
        command(["make", "-j2", "simulate", f"SIMFLAGS=--irq-period {irq_period}"], cwd=project)
        placement = json.loads((project / "build/resolution.json").read_text())
        execution = json.loads((project / "build/execution.json").read_text())
        measured = execution["observation"]
        if measured.get("last_result") != 1:
            raise RuntimeError("candidate did not reach the workload completion marker")
        rows.append({"candidate": name, "project": str(project),
                     "resident_bytes": placement["resident_bytes"], "rom_bytes": placement["rom_bytes"],
                     "rom_section_bytes": sum(a["bytes"] for a in placement["areas"] if a["rom_offset"] is not None),
                     "observed_stack_bytes": measured["observed_stack_bytes"],
                     "resident_budget": placement["resident_bytes"] + placement["resident_free_bytes"],
                     "model_ticks": measured["ticks"], "mapper_word_writes": measured["mapper_word_writes"],
                     "rom_sha256": execution["rom_sha256"],
                     "placement_sha256": hashlib.sha256((project / "build/resolution.json").read_bytes()).hexdigest(),
                     "execution_sha256": hashlib.sha256((project / "build/execution.json").read_bytes()).hexdigest()})
    def dominates(a, b):
        costs = ("resident_bytes", "rom_section_bytes", "observed_stack_bytes", "model_ticks", "mapper_word_writes")
        return all(a[k] <= b[k] for k in costs) and any(a[k] < b[k] for k in costs)
    result = {"format": "vexel-msx-frame-experiment-1", "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
              "inputs": inputs, "expected_results": oracle, "irq_period_ticks": irq_period,
              "caller_contract": "single foreground caller; no ISR entry or callbacks into sample",
              "validation": "each candidate compared with resolver for these eight inputs only",
              "stack_proof": "unproven", "candidates": rows,
              "resolved_plans": [solve_plan(executable, directory, rows, limit) for limit in (80, 128)],
              "pareto_candidates_for_this_workload": [a["candidate"] for a in rows if not any(dominates(b, a) for b in rows)]}
    (directory / "comparison.json").write_text(json.dumps(result, indent=2) + "\n")
    for row in rows:
        print(f"{row['candidate']}: resident={row['resident_bytes']} B, ROM sections={row['rom_section_bytes']} B, "
              f"observed stack={row['observed_stack_bytes']} B, model ticks={row['model_ticks']}")
    print("Evidence: " + str(directory / "comparison.json"))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=lambda s: Path(s).resolve())
    parser.add_argument("--output", type=Path, default=Path("build/msx-frame-experiment"))
    parser.add_argument("--irq-period", type=int, default=997)
    args = parser.parse_args()
    try:
        experiment(args.executable, args.output, args.irq_period)
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        parser.exit(1, str(error) + "\n")
