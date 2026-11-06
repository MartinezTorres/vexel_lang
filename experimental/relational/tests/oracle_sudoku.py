#!/usr/bin/env python3
"""Check complete Sudoku observations under declaration and search permutations."""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import subprocess
import tempfile
import time
ROOT=Path(__file__).resolve().parents[3]
def main():
    parser=argparse.ArgumentParser();parser.add_argument('machine',type=Path);parser.add_argument('--output',type=Path,default=Path('build/relational-sudoku-oracle.json'));args=parser.parse_args()
    machine=args.machine.resolve();source=(ROOT/'examples/sudoku.vx').read_text();split=source.index('solution =')
    expected=(5,3,4,6,7,8,9,1,2,6,7,2,1,9,5,3,4,8,1,9,8,3,4,2,5,6,7,8,5,9,7,6,1,4,2,3,4,2,6,8,5,3,7,9,1,7,1,3,9,2,4,8,5,6,9,6,1,5,3,7,2,8,4,2,8,7,4,1,9,6,3,5,3,4,5,2,8,6,1,7,9)
    records=[]
    with tempfile.TemporaryDirectory(prefix='vexel-sudoku-oracle-') as temp:
        for number,(schedule,branches) in enumerate(itertools.product(('fifo','lifo','random'),('forward','reverse'))):
            path=Path(temp)/'puzzle.vx';path.write_text(source if number%2==0 else source[split:]+'\n'+source[:split])
            start=time.monotonic();r=subprocess.run([str(machine),str(path),'--query','value:solution','--schedule',schedule,'--branch-order',branches,'--max-work','20000000','--max-decisions','0'],capture_output=True,text=True,timeout=180)
            assert r.returncode==0,r.stdout+r.stderr
            report=json.loads(r.stdout);assert report['status']=='resolved',report
            answer=report['queries'][0]['answer'];assert answer.startswith('Values(')
            grid=tuple(map(int,answer[7:-1].split(',')));assert grid==expected,(schedule,branches,grid)
            groups=[grid[r*9:r*9+9] for r in range(9)]+[grid[c::9] for c in range(9)]+[tuple(grid[(r+i)*9+c+j] for i in range(3) for j in range(3)) for r in (0,3,6) for c in (0,3,6)]
            assert all(set(group)==set(range(1,10)) for group in groups)
            records.append({'schedule':schedule,'branches':branches,'clues_declared_first':number%2==0,'seconds':time.monotonic()-start,'stats':report['stats']})
            print(schedule,branches,'passed',flush=True)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps({'machine_sha256':hashlib.sha256(machine.read_bytes()).hexdigest(),'source_sha256':hashlib.sha256(source.encode()).hexdigest(),'solution':expected,'runs':records},indent=2)+'\n')
    print('Six complete Sudoku observations agree:',args.output)
if __name__=='__main__':main()
