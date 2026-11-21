#!/usr/bin/env python3
"""Resolve one program and retry only the bank partitions rejected by measurement."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from project_msx import package


def measured_feedback(report, project, resources):
    """Attribute target/host failures through emitter-owned module identities."""
    modules = {m['module']: m for m in report['bank_modules']}
    functions = {f['name']: f for f in report.get('frame_planning', {}).get('functions', [])}
    feedback, oversized = [], []
    bank_bytes = report['configuration']['rom_bank_bytes']['value']
    for name, module in modules.items():
        obj = project / 'build' / (name + '.rel')
        if not obj.exists():
            continue
        size = sum(int(match[1], 16) for match in re.finditer(
            r'^A _(?:CODE|CONST) size ([0-9a-fA-F]+)\b', obj.read_text(), re.M))
        if size > bank_bytes:
            oversized.append({'module': name, 'bytes': size})
            feedback.append({'module': name, 'reason': 'bank-size',
                             'bytes': size, 'limit_bytes': bank_bytes})
    for row in resources:
        if row['reason'] not in ('timeout', 'cpu-limit', 'memory-limit'):
            continue
        command = row['command']
        output = command[command.index('-o')+1] if '-o' in command else ''
        feedback.append({'module': Path(output).stem, 'reason': row['reason']})
    updates = {}
    for item in feedback:
        module = modules.get(item['module'])
        if module is None:
            item['blocked'] = 'failure is outside a generated bank partition'
            continue
        item['owner'] = module['partition_owner']
        item['chunk_operations'] = module['chunk_operations']
        function = functions.get(item['owner'], {})
        if function.get('division_unroll') and function.get('unrolled_division_helpers', 0):
            # Constant-specific straight-line helpers can dominate a module.
            # Smaller statement groups cannot split one helper or expression.
            # Try the equivalent compact arithmetic realization for this owner
            # before changing its continuation boundaries.
            item['next_preferences'] = {'calculation_division_unroll.'+item['owner']: 0}
        elif module['partition_role'] == 'dispatcher':
            item['blocked'] = 'smaller continuation groups do not shrink this dispatcher'
        elif module['chunk_operations'] == 1:
            item['blocked'] = 'minimum continuation group already reached'
        else:
            next_limit = max(1, module['chunk_operations'] // 2)
            item['next_chunk_operations'] = next_limit
            updates[item['owner']] = next_limit
    return feedback, oversized, updates


def install_user_inputs(directory, project):
    """Copy editable embedding inputs and identify their exact source revision."""
    fingerprints = {}
    for path in sorted(directory.rglob('*')):
        if not path.is_file():
            continue
        target = project / 'user' / path.relative_to(directory)
        target.parent.mkdir(parents=True, exist_ok=True)
        digest = hashlib.sha256()
        with path.open('rb') as source, target.open('wb') as output:
            while data := source.read(1024 * 1024):
                output.write(data)
                digest.update(data)
        fingerprints[str(path)] = digest.hexdigest()
    return fingerprints


def realize(solver, source, output, options=(), build=False, user_directory=None):
    solver, source, output = Path(solver).resolve(), Path(source).resolve(), Path(output).resolve()
    if user_directory is not None:
        user_directory = Path(user_directory).resolve()
        if not user_directory.is_dir():
            raise ValueError('user directory must exist and contain editable embedding files')
        if output == user_directory or user_directory in output.parents:
            raise ValueError('output project must be outside the user directory')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ValueError('output project must be new or empty')
    output.parent.mkdir(parents=True, exist_ok=True)
    attempts, limits, preferences, input_identity = [], {}, {}, None

    def failed(reason, log=''):
        output.mkdir(exist_ok=True)
        (output/'realization-failure.json').write_text(json.dumps(attempts, indent=2)+'\n')
        (output/'build-failure.log').write_text(log)
        raise ValueError(reason + '; evidence saved in ' + str(output))

    with tempfile.TemporaryDirectory(prefix='vexel-realize-',dir=output.parent) as temp:
        scratch=Path(temp)
        # All attempts and their provenance use the same executable, even if
        # the workspace frontend is rebuilt during a long SDCC realization.
        pinned=scratch/'vexel';shutil.copy2(solver,pinned);solver=pinned
        while True:
            number = len(attempts)+1
            print(f'Resolving target attempt {number}: '+json.dumps({'partitions': limits, 'preferences': preferences}, sort_keys=True), file=sys.stderr, flush=True)
            generated=scratch/f'c-{number}'; project=scratch/f'project-{number}'
            command=[str(solver),str(source),*map(str,options)]
            for owner, limit in sorted(limits.items()):
                command += ['--prefer', f'bank_chunk_operations.{owner}={limit}']
            for key, value in sorted(preferences.items()):
                command += ['--prefer', f'{key}={value}']
            command += ['--emit-c',str(generated)]
            solved=subprocess.run(command,capture_output=True,text=True)
            if solved.returncode:
                failed('source obligations did not resolve', solved.stderr+solved.stdout)
            report=package(generated,project,source,solver)
            user_inputs = install_user_inputs(user_directory, project) if user_directory is not None else None
            attempt={'chunk_operations':report['configuration']['bank_chunk_operations']['value'],
                     'partition_limits': dict(limits), 'realization_preferences': dict(preferences),
                     'solver_work':report['stats']['work']}
            attempts.append(attempt)
            # A generated symbol is a valid feedback identity only within this
            # exact input revision. Never transfer a rejection to an edited game.
            provenance=json.loads((project/'generated/provenance.json').read_text())
            identity={key:provenance[key] for key in ('machine_sha256', 'source_inputs_sha256',
                      'resource_inputs_sha256', 'directory_inputs', 'process_inputs')}
            identity['input_selections']=report.get('input_selections', [])
            if user_inputs is not None:
                identity['user_inputs_sha256'] = user_inputs
            attempt['input_identity']=identity
            if input_identity is not None and identity != input_identity:
                attempt['rejected']='inputs changed during realization'
                failed(attempt['rejected'])
            input_identity=identity
            if build:
                compiled=subprocess.run(['make','-C',str(project)],capture_output=True,text=True)
                attempt['build_status']=compiled.returncode
                (project/'build').mkdir(exist_ok=True)
                (project/'build/realization-build.log').write_text(compiled.stdout+compiled.stderr)
                resource_log=project/'build/sdcc-resources.jsonl'
                resources=[json.loads(line) for line in resource_log.read_text().splitlines()] if resource_log.exists() else []
                attempt['compiler_resources']=resources
                feedback, oversized, updates=measured_feedback(report,project,resources)
                attempt['feedback']=feedback
                attempt['oversized_modules']=oversized
                if feedback:
                    attempt['rejected']=('generated module exceeds an 8 KiB bank' if oversized
                                         else 'host compiler resource limit')
                    choices = {key: value for item in feedback for key, value in item.get('next_preferences', {}).items()}
                    # Feedback must make strict progress even if an external
                    # emitter publishes an inconsistent effective preference.
                    choices = {key: value for key, value in choices.items() if preferences.get(key) != value}
                    if not any('blocked' in item for item in feedback) and (updates or choices):
                        # Each choice is disabled once and each affected group
                        # decreases strictly toward one. Other owners retain
                        # their original physical choices.
                        limits.update(updates)
                        preferences.update(choices)
                        print('Retrying affected realizations: '+json.dumps({'partitions': updates, 'preferences': choices}, sort_keys=True), file=sys.stderr, flush=True)
                        continue
                if compiled.returncode or feedback:
                    failed('target realization failed', compiled.stdout+compiled.stderr)
            attempt['selected']=True
            shutil.copytree(project,output,dirs_exist_ok=True)
            (output/'realization.json').write_text(json.dumps(attempts,indent=2)+'\n')
            return attempts


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--solver',type=Path,default=Path(__file__).resolve().parents[2]/'build/vexel')
    parser.add_argument('--build',action='store_true')
    parser.add_argument('--user-directory',type=Path,help='files copied into user/ before every measured build')
    args,options=parser.parse_known_args()
    try:
        result=realize(args.solver,args.source,args.output,options,args.build,args.user_directory)
        print(f"Editable MSX project: {args.output} ({len(result)} realization attempt(s))")
    except (OSError,ValueError) as error:
        parser.exit(1,str(error)+'\n')
