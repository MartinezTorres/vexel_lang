#!/usr/bin/env python3
"""Independent declaration/worklist permutation and linear-shape source oracle."""
import itertools, json, subprocess, sys, tempfile
from pathlib import Path
exe=Path(sys.argv[1]).resolve()
chunks=[
 'left:#u8[|right|+1];',
 'right:#u8[7-|left|];',
 '&packet(offset:#u8,count:#u8,x:#u16){#Packet(count:#u8,data:#u16[count+offset]);#Packet(count,[x,x+1,x+2])}',
 '&^first([[range(0,250)]] x:#u8){packet(1,|right|-1,(#u16)x)}',
 '&^second([[range(0,250)]] x:#u8){packet(2,|left|-3,(#u16)x)}',
 '^dimensions=|left|*10+|right|;',
]
checked=0
with tempfile.TemporaryDirectory(prefix='vexel-source-oracle-') as directory:
 source=Path(directory)/'main.vx'
 for order in (chunks,list(reversed(chunks)),chunks[3:]+chunks[:3]):
  source.write_text('\n'.join(order))
  for schedule,branch in itertools.product(('fifo','lifo','random'),('forward','reverse')):
   result=subprocess.run([str(exe),str(source),'--query','value:dimensions','--source','first','--source','second','--mode','overlap','--budget','6','--schedule',schedule,'--branch-order',branch],capture_output=True,text=True,timeout=90)
   assert result.returncode==0,result.stderr+result.stdout
   report=json.loads(result.stdout)
   assert report['queries'][0]['answer']=='43',report
   assert [(s['element_bits'],s['bytes'],s['offset']) for s in report['sources']]==[(8,3,0),(8,3,3)],report
   assert all('data:#u16[3]' in s['logical_type'] for s in report['sources']),report
   checked+=1
 for delta,total in itertools.product(range(1,5),range(1,12)):
  a=(total+delta)//2;b=total-a
  valid=(total+delta)%2==0 and a>=0 and b>=0
  declarations=[f'a:#u8[|b|+{delta}];',f'b:#u8[{total}-|a|];']
  for order in (declarations,declarations[::-1]):
   source.write_text(''.join(order))
   for schedule,branch in itertools.product(('fifo','lifo','random'),('forward','reverse')):
    result=subprocess.run([str(exe),str(source),'--query','length:a','--query','length:b','--schedule',schedule,'--branch-order',branch],capture_output=True,text=True,timeout=30)
    assert result.returncode in (0,1),result.stderr
    report=json.loads(result.stdout)
    assert report['status']==('resolved' if valid else 'contradictory'),(delta,total,report)
    if valid: assert [q['answer'] for q in report['queries']]==[str(a),str(b)],report
    checked+=1
print(f'{checked} independent source cases passed: 18 complete declaration/search permutations and 528 linear-shape cases')
