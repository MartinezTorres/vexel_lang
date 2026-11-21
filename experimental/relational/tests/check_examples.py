#!/usr/bin/env python3
"""Exercise every example as a program, plus the supported target workloads.

Each run keeps its reports, generated images and logs. A baseline executable
can check that discovering exports agrees with requesting those same roots
explicitly. Historical drivers for the retired frontend are recorded separately.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('program_image', Path(__file__).parents[1] / 'program_image.py')
images = importlib.util.module_from_spec(spec)
spec.loader.exec_module(images)
RETIRED = {
    'measure_msx.py': 'retired --call/--emit-msx frontend; neo8_frames.vx is checked as a program',
    'relational_msx.py': 'bootstrap adapter for retired --emit-msx frontend; replaced by relational_source_msx.py',
}


def run(command, log, timeout=600):
    env = os.environ.copy()
    for key in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'MAKELEVEL', 'BUILD'):
        env.pop(key, None)
    result = subprocess.run([str(x) for x in command], cwd=ROOT, env=env,
                            capture_output=True, text=True, timeout=timeout)
    log.write_text(result.stdout + '\n' + result.stderr)
    return result


def comparable(report):
    # Term trees and the program image are additive publication products.
    # Optional proof traffic measures derivation work, not source meaning.
    result = {key: ([{k: v for k, v in q.items() if k != 'term'} for q in value]
                  if key == 'queries' else value)
            for key, value in report.items() if key not in
            ('program_image', 'stats', 'conditional_proofs', 'native_proofs')}
    if 'value_ranges' in result and 'contexts' in result['value_ranges']:
        ranges = dict(result['value_ranges'])
        # Preserving a shared term can avoid querying the same expression
        # again. Cache traffic is performance evidence, not a source answer.
        # Keep limits, exhaustion, inferred ranges and all other plan fields.
        ranges['contexts'] = {k:v for k,v in ranges['contexts'].items()
                              if k not in ('query_hits','query_misses')}
        result['value_ranges'] = ranges
    return result


def program(args, source):
    name = str(source.relative_to(ROOT / 'examples'))
    parent = args.output / 'programs' / name
    parent.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    limits = ['--max-work', '100000000', '--max-nodes', '2000000'] if name == 'bank_pressure.vx' else ['--max-work', '20000000']
    options = ['--program', *limits, '--allow-processes']
    result = run([args.solver, source, *options], directory/'resolve.log')
    if not result.stdout.strip():
        raise RuntimeError(result.stderr)
    report = json.loads(result.stdout)
    (directory/'resolution.json').write_text(json.dumps(report, indent=2)+'\n')
    row = {'name': name, 'status': report['status'], 'entries': len(report.get('sources', [])),
           'observations': len(report.get('queries', [])), 'image': False}
    if name == 'type_equations.vx' and report['status'] == 'unresolved':
        # Existing selective-type example: its partial export cannot yet close
        # as a value. Record this limit rather than omit it or claim success.
        if not any('uninitialized execution location' in p for p in report['pending']):
            raise RuntimeError('type-equation example changed its unresolved obligation')
        query = run([args.solver, source, '--query', 'type:partial'], directory/'partial-type.log')
        if query.returncode or json.loads(query.stdout)['queries'][0]['answer'] != '#u8':
            raise RuntimeError('the selective type observation no longer resolves')
        if args.baseline:
            prior = run([args.baseline, source, '--query', 'value:partial'], directory/'baseline-open.log')
            if json.loads(prior.stdout)['status'] != 'unresolved':
                raise RuntimeError('the open value is a regression from the baseline')
        row.update(known_limitation='partial export value remains open; selective type resolves to #u8')
        return row
    if report['status'] != 'resolved':
        # An open program is not an accepted example. Preserve the diagnostic.
        raise RuntimeError('program did not close: '+result.stdout[-3000:])
    if args.baseline:
        explicit = [*limits, '--allow-processes']
        for source in report.get('sources', []):
            explicit += ['--source', source['name']]
        for query in report.get('queries', []):
            explicit += ['--query', query['query']]
        baseline = run([args.baseline, ROOT/'examples'/name, *explicit], directory/'baseline.log')
        if baseline.returncode or comparable(json.loads(baseline.stdout)) != comparable(report):
            raise RuntimeError('explicit baseline and program observations disagree; see baseline.log')
        row['baseline'] = 'equal'
    if row['entries']:
        generated = directory/'generated'
        emitted = run([args.solver, ROOT/'examples'/name, *options, '--emit-c', generated], directory/'emit.log')
        if emitted.returncode:
            raise RuntimeError('image emission failed: '+emitted.stderr)
        image = images.load(generated)
        row.update(image=True, artifacts=len(image['artifacts']))
        # Compile all ordinary units together; target workloads below exercise
        # banked continuations, runtime execution and their boundary adapters.
        compiled = run(['cc', '-std=c11', '-Werror=implicit-function-declaration',
                        '-I'+str(generated), '-I'+str(ROOT/'backends/msx-neo8/runtime'),
                        '-c', generated/'vx_bridges.c', '-o', directory/'projection.o'], directory/'compile.log')
        if compiled.returncode:
            raise RuntimeError('C compilation failed: '+compiled.stderr[-3000:])
    return row


def workload(args, driver):
    parent = args.output/'workloads'/driver.stem
    parent.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='attempt-', dir=parent))
    option = '--project' if driver.name == 'bank_pressure_msx.py' else '--output'
    result = run(['python3', driver, args.solver, option, directory/'run'], directory/'run.log', timeout=1800)
    if result.returncode:
        raise RuntimeError('workload failed; see '+str(directory/'run.log'))
    return {'name': driver.name, 'status': 'passed'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('solver', type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--output', type=Path, required=True, help='new directory for evidence')
    parser.add_argument('--msx', action='store_true', help='also run every supported host/Z80 example driver')
    parser.add_argument('--jobs', type=int, default=1)
    parser.add_argument('--resume', action='store_true', help='retry only failed or unfinished checks')
    parser.add_argument('--recheck', action='append', default=[], help='also repeat a completed example/driver by name when resuming')
    args = parser.parse_args()
    args.solver = args.solver.resolve()
    args.baseline = args.baseline.resolve() if args.baseline else None
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=args.resume)
    tasks = [(program, p) for p in sorted((ROOT/'examples').rglob('*.vx'))]
    if args.msx:
        tasks += [(workload, p) for p in sorted((ROOT/'examples').glob('*_msx.py')) if p.name not in RETIRED]
    evidence = {'programs': [], 'workloads': [], 'retired_drivers': RETIRED, 'failures': []}
    if args.resume:
        evidence = json.loads((args.output/'summary.json').read_text())
        evidence['previous_failures'] = evidence.pop('failures')
        evidence['failures'] = []
        for kind in ('programs','workloads'):
            repeated = [r for r in evidence[kind] if r['name'] in args.recheck]
            evidence.setdefault('superseded_checks', []).extend(repeated)
            evidence[kind] = [r for r in evidence[kind] if r['name'] not in args.recheck]
        passed_programs = {r['name'] for r in evidence['programs']}
        passed_workloads = {r['name'] for r in evidence['workloads']}
        tasks = [(fn, p) for fn, p in tasks if
                 (str(p.relative_to(ROOT/'examples')) not in passed_programs if fn == program
                  else p.name not in passed_workloads)]
    started = time.monotonic()
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(fn, args, path): (fn, path) for fn, path in tasks}
        for future in as_completed(futures):
            fn, path = futures[future]
            try:
                row = future.result()
                evidence['programs' if fn == program else 'workloads'].append(row)
                print('KNOWN OPEN' if row.get('known_limitation') else 'PASS', path.relative_to(ROOT), flush=True)
            except Exception as error:
                evidence['failures'].append({'name': str(path.relative_to(ROOT)), 'error': str(error)})
                print('FAIL', path.relative_to(ROOT), str(error)[:500], flush=True)
            evidence['seconds'] = round(time.monotonic()-started, 2)
            (args.output/'summary.json').write_text(json.dumps(evidence, indent=2)+'\n')
    return bool(evidence['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
