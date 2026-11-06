#!/usr/bin/env python3
"""Resolve and export an editable project; optionally retry measured bank cuts."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from project_msx import package


def realize(solver, source, output, options=(), build=False):
    solver, source, output = Path(solver).resolve(), Path(source).resolve(), Path(output).resolve()
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ValueError('output project must be new or empty')
    output.parent.mkdir(parents=True, exist_ok=True)
    attempts=[]
    with tempfile.TemporaryDirectory(prefix='vexel-realize-',dir=output.parent) as temp:
        scratch=Path(temp)
        # All attempts and their provenance use the same executable, even if
        # the workspace frontend is rebuilt during a long SDCC realization.
        pinned=scratch/'vexel';shutil.copy2(solver,pinned);solver=pinned
        for cut in (128,64,32,16,8,4,2,1):
            print(f'Resolving bank partition with at most {cut} operations per chunk',file=sys.stderr,flush=True)
            generated=scratch/f'c-{cut}'; project=scratch/f'project-{cut}'
            command=[str(solver),str(source),*map(str,options),'--prefer',f'bank_chunk_operations={cut}','--emit-c',str(generated)]
            solved=subprocess.run(command,capture_output=True,text=True)
            if solved.returncode:
                raise ValueError('source obligations did not resolve: '+solved.stderr+solved.stdout)
            report=package(generated,project,source,solver)
            attempt={'chunk_operations':report['configuration']['bank_chunk_operations']['value'],'solver_work':report['stats']['work']}
            attempts.append(attempt)
            if build:
                compiled=subprocess.run(['make','-C',str(project)],capture_output=True,text=True)
                attempt['build_status']=compiled.returncode
                (project/'build/realization-build.log').write_text(compiled.stdout+compiled.stderr)
                oversized=[]
                for module in report['bank_modules']:
                    obj=project/'build'/(module['module']+'.rel')
                    if not obj.exists(): continue
                    size=sum(int(match[1],16) for match in re.finditer(r'^A _(?:CODE|CONST) size ([0-9a-fA-F]+)\b',obj.read_text(),re.M))
                    if size>8192: oversized.append({'module':module['module'],'bytes':size})
                attempt['oversized_modules']=oversized
                if oversized:
                    attempt['rejected']='generated module exceeds an 8 KiB bank'
                    print('Measured oversized modules: '+json.dumps(oversized),file=sys.stderr,flush=True)
                    if cut>1: continue
                if compiled.returncode or oversized:
                    output.mkdir(exist_ok=True)
                    (output/'realization-failure.json').write_text(json.dumps(attempts,indent=2)+'\n')
                    (output/'build-failure.log').write_text(compiled.stdout+compiled.stderr)
                    raise ValueError('target realization failed; evidence saved in '+str(output))
            attempt['selected']=True
            shutil.copytree(project,output,dirs_exist_ok=True)
            (output/'realization.json').write_text(json.dumps(attempts,indent=2)+'\n')
            return attempts
    raise ValueError('no bank partition fits')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--solver',type=Path,default=Path(__file__).resolve().parents[2]/'build/vexel')
    parser.add_argument('--build',action='store_true')
    args,options=parser.parse_known_args()
    try:
        result=realize(args.solver,args.source,args.output,options,args.build)
        print(f"Editable MSX project: {args.output} ({len(result)} realization attempt(s))")
    except (OSError,ValueError) as error:
        parser.exit(1,str(error)+'\n')
