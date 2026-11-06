#!/usr/bin/env python3
"""Replay edits, compare clean builds, and measure an editable NEO-8 workload."""
import argparse
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('source_msx', REPO / 'examples/relational_source_msx.py')
support = importlib.util.module_from_spec(spec)
spec.loader.exec_module(support)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def files(directory):
    return {p.name: p.read_bytes() for p in directory.iterdir() if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('machine', type=lambda p: Path(p).resolve())
    parser.add_argument('--output', type=Path, default=Path('build/incremental-msx'))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    inputs = directory / 'inputs'
    shutil.copytree(REPO / 'examples/incremental_msx', inputs)
    source = inputs / 'game.vx'
    steps, candidates = [], []
    session = subprocess.Popen([args.machine, '--session'], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    def solve(name, flags, reused):
        generated, clean = directory / (name + '-incremental'), directory / (name + '-clean')
        command = ['solve', str(source), *flags, '--emit-c', str(generated)]
        started = time.perf_counter()
        session.stdin.write(' '.join(json.dumps(s) for s in command) + '\n')
        session.stdin.flush()
        line = session.stdout.readline()
        if not line:
            raise RuntimeError('incremental session exited: ' + session.stderr.read())
        report = json.loads(line)
        elapsed = time.perf_counter() - started
        assert report['status'] == 'resolved', report
        incremental = report.pop('incremental')
        assert incremental['reused'] == reused, incremental
        started = time.perf_counter()
        fresh = support.run([args.machine, source, *flags, '--emit-c', clean])
        clean_elapsed = time.perf_counter() - started
        assert report == json.loads(fresh.stdout), name
        assert files(generated) == files(clean), name
        steps.append({'name': name, 'request': flags, **incremental,
                      'incremental_seconds': elapsed, 'clean_seconds': clean_elapsed,
                      'clean_solver_work': report['stats']['work'],
                      'inputs_sha256': {p.name: sha(p) for p in sorted(inputs.iterdir())},
                      'projection_sha256': {p.name: sha(p) for p in sorted(generated.iterdir())},
                      'clean_report_and_projection_equal': True})
        print(f"{name}: {'reused' if reused else 'solved'}, "
              f"{incremental['solver_work']} solver steps ({report['stats']['work']} clean)", flush=True)
        return generated, report

    try:
        frame, tone = ['--source', 'frame'], ['--source', 'tone']
        solve('frame-first', frame, False)
        solve('tone-first', tone, False)
        solve('frame-repeat', frame, True)
        (inputs / 'palette.txt').write_text('5678\n')
        solve('tone-after-palette-edit', tone, True)
        solve('frame-after-palette-edit', frame, False)
        (inputs / 'motion.vx').write_text('&~position(tick:#u8)->#u8 { (#u8)(tick*5+7) }\n')
        solve('frame-after-source-edit', frame, False)
        # Full static import closure is deliberate: even a body edit in a
        # visible module invalidates the tone group at this coarse granularity.
        solve('tone-after-source-edit', tone, False)
        for budget in (0, 128):
            flags = ['--source', 'frame', '--source', 'tone', '--budget', '8',
                     '--frame-ram-bytes', str(budget)]
            solve('plan-' + str(budget), flags, False)
            generated, report = solve('plan-repeat-' + str(budget), flags, True)
            project = directory / ('project-' + str(budget))
            support.backend.package(generated, project, source, args.machine)
            library = directory / ('host-' + str(budget) + '.so')
            support.run(['cc', '-O2', '-std=c11', '-shared', '-fPIC', '-I' + str(generated),
                         '-I' + str(project / 'runtime'), generated / 'vx_bridges.c',
                         project / 'runtime/vx_runtime.c', '-o', library])
            dll = ctypes.CDLL(str(library))
            for name in ('frame', 'tone'):
                getattr(dll, 'vx_' + name).argtypes = [ctypes.c_uint8]
                reader = getattr(dll, 'vx_' + name + '_read')
                reader.argtypes = [ctypes.c_uint16]; reader.restype = ctypes.c_uint32
            for tick in range(256):
                dll.vx_frame(tick)
                assert [dll.vx_frame_read(i) for i in range(3)] == [(tick*5+7) & 255, 64+tick%32, 5+tick%4]
                dll.vx_tone(tick)
                assert dll.vx_tone_read(0) == 65+2*(tick%4)
            # Independent arithmetic oracle; there is no Vexel-derived answer
            # table. The completion marker distinguishes success from a trap.
            (project / 'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    uint16_t tick;
    vx_last_result=0;
    for(tick=0;tick<256;++tick) {
        vx_frame((uint8_t)tick);
        if(vx_frame_read(0)!=((tick*5+7)&255) || vx_frame_read(1)!=64+tick%32 || vx_frame_read(2)!=5+tick%4) {
            vx_last_result=1+tick; __asm halt __endasm;
        }
        vx_tone((uint8_t)tick);
        if(vx_tone_read(0)!=65+2*(tick%4)) {
            vx_last_result=257+tick; __asm halt __endasm;
        }
    }
    vx_last_result=65535UL;
    __asm halt __endasm;
}
''')
            built = support.run(['make', '-C', project, '-j2', 'simulate', 'SIMFLAGS=--max-ticks=1000000000'])
            (directory / ('build-' + str(budget) + '.log')).write_text(built.stdout + built.stderr)
            placement = json.loads((project / 'build/resolution.json').read_text())
            execution = json.loads((project / 'build/execution.json').read_text())
            observed = execution['observation']
            assert observed['last_result'] == 65535 and observed['fault'] == 0, execution
            assert execution['rom_sha256'] == sha(project / 'build/game.rom')
            candidates.append({'frame_ram_budget': budget, 'project': str(project),
                'host_activations': 512, 'z80_activations': 512,
                'rom_bytes': placement['rom_bytes'],
                'rom_section_bytes': sum(a['bytes'] for a in placement['areas'] if a['rom_offset'] is not None),
                'resident_bytes': placement['resident_bytes'], 'resident_free_bytes': placement['resident_free_bytes'],
                'stack_reserved_bytes': placement['stack_reserved_bytes'],
                'observed_stack_bytes': observed['observed_stack_bytes'],
                'model_ticks': observed['ticks'], 'mapper_word_writes': observed['mapper_word_writes'],
                'stack_proof': 'unproven', 'frame_planning': report['frame_planning'],
                'rom_sha256': execution['rom_sha256'],
                'placement_sha256': sha(project / 'build/resolution.json'),
                'execution_sha256': sha(project / 'build/execution.json')})
            print(f"frame budget {budget}: {candidates[-1]['rom_section_bytes']} ROM section bytes, "
                  f"{placement['resident_bytes']} resident RAM bytes, {observed['observed_stack_bytes']} observed stack bytes", flush=True)
    finally:
        session.stdin.close()
        try:
            session.wait(timeout=10)
        finally:
            if session.poll() is None:
                session.kill(); session.wait()
        errors = session.stderr.read()
        session.stdout.close(); session.stderr.close()
        if session.returncode or errors:
            raise RuntimeError(f'session exit {session.returncode}: {errors}')
    evidence = {'format': 'vexel-incremental-msx-1', 'machine_sha256': sha(args.machine),
                'driver_sha256': sha(__file__), 'packager_sha256': sha(REPO / 'experimental/relational/project_msx.py'),
                'final_inputs_sha256': {p.name: sha(p) for p in sorted(inputs.iterdir())},
                'steps': steps, 'candidates': candidates,
                'scope': 'Serial sprite-state and tone-data workload; no VDP or audio device emulation. Observed stack is not a bound.'}
    (directory / 'comparison.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('Evidence:', directory / 'comparison.json')


if __name__ == '__main__':
    main()
