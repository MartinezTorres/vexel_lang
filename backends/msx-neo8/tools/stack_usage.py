#!/usr/bin/env python3
"""Conservative stack analysis of emitted SDCC ABI-0 Z80 assembly.

Unknown control/SP effects fail closed. A result is conditional on explicitly
supplied external contracts and on execution entering through the named roots.
No measured workload maximum is used as a bound.
"""
from collections import deque
import json
from pathlib import Path
import re


class OpenStack(ValueError):
    pass


def assembly(path):
    instructions, labels, functions = [], {}, {}
    scope=''
    executable=True
    for raw in path.read_text().splitlines():
        line = raw.split(';', 1)[0].strip()
        if line.lower().startswith('.area'):
            executable=any(name in line.upper() for name in ('_CODE','_HOME','_GSINIT','_GSFINAL','_HEADER'))
            continue
        if not line or line.startswith('.') or '=' in line:
            continue
        label = re.fullmatch(r'([\w$]+):{1,2}', line)
        if label:
            name = label[1]
            if name.endswith('$'): name=scope+'@'+name
            else: scope=name
            if executable: labels[name] = len(instructions)
            if executable and name.startswith('_') and not name.endswith('$'):
                functions[name] = len(instructions)
            continue
        op, _, operands = line.partition('\t')
        if not operands:
            parts = line.split(None, 1); op = parts[0]; operands = parts[1] if len(parts)>1 else ''
        registers={'a','f','b','c','d','e','h','l','af','bc','de','hl','ix','iy','ixh','ixl','iyh','iyl','sp','i','r','nz','z','nc','po','pe','p','m'}
        operands=re.sub(r'[A-Za-z_][\w$]*',lambda m:m[0].lower() if m[0].lower() in registers else m[0],operands.replace(' ', ''))
        operands=re.sub(r'[\w]+\$',lambda m:scope+'@'+m[0],operands)
        instructions.append((op.lower(), operands, raw.strip()))
    return instructions, labels, functions


def local_usage(code, labels, start, allow_tables=True):
    # Values track constants or addresses relative to entry SP. Register
    # uncertainty is joined at each PC; SP must agree on every incoming edge.
    initial = (0, {})
    seen, queue, high, calls = {}, deque([(start, initial)]), 0, {}
    pairs = ('af', 'bc', 'de', 'hl', 'ix', 'iy')
    halves = {'af':('a','f'), 'bc':('b','c'), 'de':('d','e'), 'hl':('h','l'),
              'ix':('ixh','ixl'), 'iy':('iyh','iyl')}
    def signed_word(value): return ((value+32768)&65535)-32768
    def immediate(text):
        if not text.startswith('#'): return None
        try: return ('c', signed_word(int(text[1:], 0)))
        except ValueError: return None
    while queue:
        pc, (sp, registers) = queue.popleft()
        if pc >= len(code): raise OpenStack('execution falls out of assembly')
        old = seen.get(pc)
        if old is not None:
            if old[0] != sp: raise OpenStack('unbalanced or unknown stack at a control join: '+code[pc][2])
            joined = {k:v for k,v in registers.items() if old[1].get(k)==v}
            if joined == old[1]: continue
            registers = joined
        seen[pc] = (sp, dict(registers))
        if len(seen)>100000: raise OpenStack('assembly analysis budget exhausted')
        registers = dict(registers)
        op, operands, raw = code[pc]; args = operands.split(',') if operands else []
        successors = [pc+1]
        if op in ('push', 'pop'):
            if operands not in pairs: raise OpenStack('unknown stack operand: '+raw)
            sp += -2 if op=='push' else 2
            if op=='pop': registers.pop(operands, None)
        elif op in ('inc', 'dec'):
            delta = 1 if op=='inc' else -1
            if operands=='sp': sp += delta
            elif operands in pairs:
                if operands in registers:
                    kind, value=registers[operands]; registers[operands]=(kind,value+delta)
            else:
                for pair in pairs:
                    if operands in halves[pair]: registers.pop(pair, None)
        elif op=='ld' and len(args)==2:
            dest, source=args
            value=('s',sp) if source=='sp' else registers.get(source, immediate(source))
            if dest=='sp':
                if value is None or value[0]!='s': raise OpenStack('unknown SP assignment: '+raw)
                sp=signed_word(value[1])
            elif dest in pairs:
                registers.pop(dest,None)
                if value is not None: registers[dest]=value
            else:
                for pair in pairs:
                    if dest in halves[pair]: registers.pop(pair,None)
        elif op=='add' and len(args)==2 and args[0] in pairs:
            dest, source=args
            a=registers.get(dest); b=('s',sp) if source=='sp' else registers.get(source)
            registers.pop(dest,None)
            if a and b and (a[0]=='c' or b[0]=='c'):
                registers[dest]=('s' if a[0]=='s' or b[0]=='s' else 'c',a[1]+b[1])
        elif op=='ex' and operands=='de,hl':
            a,b=registers.pop('de',None),registers.pop('hl',None)
            if a: registers['hl']=a
            if b: registers['de']=b
        elif op=='ex' and 'sp' in operands:
            if len(args)!=2 or args[0]!='(sp)' or args[1] not in pairs:
                raise OpenStack('unknown stack exchange: '+raw)
            registers.pop(args[1],None)
        elif op=='call':
            target=args[-1]
            if target=='___sdcc_enter_ix' and len(args)==1:
                high=max(high,-sp+4);sp-=2;registers['ix']=('s',sp)
            else:
                calls[target]=max(calls.get(target,0),-sp+2)
                high=max(high,-sp+2)
            for pair in ('af','bc','de','hl'): registers.pop(pair,None)
        elif op in ('jp','jr','djnz'):
            if op=='djnz': registers.pop('bc',None)
            target=args[-1]
            if target.startswith('('):
                # SDCC's checked switch: HL = table + index * entry-width,
                # followed immediately by uniform JR/JP entries. The C switch
                # contract supplies the index range; visit every alternative.
                cursor=pc-1; stride=0; pair=None
                while cursor>=0 and code[cursor][0]=='add' and code[cursor][1] in ('hl,de','hl,bc'):
                    if pair is not None and pair!=code[cursor][1]: break
                    pair=code[cursor][1];stride+=1;cursor-=1
                table=code[cursor][1].removeprefix('hl,#') if cursor>=0 else ''
                if not allow_tables or target!='(hl)' or stride not in (2,3) or cursor<0 or code[cursor][0]!='ld' or labels.get(table)!=pc+1:
                    raise OpenStack('indirect control transfer: '+raw)
                end=min((index for index in labels.values() if index>pc+1),default=len(code))
                successors=[]
                for index in range(pc+1,end):
                    jump,destination,_=code[index]
                    if jump!=('jr' if stride==2 else 'jp') or destination not in labels:
                        raise OpenStack('unrecognized SDCC switch table')
                    successors.append(labels[destination])
                if not successors: raise OpenStack('empty SDCC switch table')
            else:
                conditional=len(args)>1 or op=='djnz'
                successors=[pc+1] if conditional else []
                if target in labels: successors.append(labels[target])
                else: calls[target]=max(calls.get(target,0),-sp)
        elif op in ('ret','reti','retn'):
            if sp!=0: raise OpenStack('return does not restore entry SP')
            successors=[pc+1] if operands else []
        elif op=='halt': successors=[]
        elif op=='rst': raise OpenStack('uncontracted restart: '+raw)
        else:
            # These instructions cannot write SP. Conservatively discard any
            # register they could alter; an SP reconstruction then fails closed.
            safe={'nop','di','ei','or','and','xor','cp','sub','add','sbc','adc','bit','set','res','rl','rr','rlc','rrc',
                  'rla','rra','rlca','rrca','sla','sra','srl','cpl','neg','scf','ccf','daa','in','out',
                  'ldi','ldir','ldd','lddr','cpi','cpir','cpd','cpdr','exx','ex','rld','rrd'}
            if op not in safe: raise OpenStack('unknown instruction: '+raw)
            if 'sp' in args: raise OpenStack('unmodelled SP operation: '+raw)
            for pair in ('af','bc','de','hl'): registers.pop(pair,None)
            for pair in ('ix','iy'):
                if any(arg in halves[pair] for arg in args): registers.pop(pair,None)
        high=max(high,-sp)
        for successor in successors: queue.append((successor,(sp,registers)))
    return high, calls


def analyze(project, plan):
    build = project/'build'
    definitions, locations = {}, {}
    # C .asm files and handwritten .s modules selected by the object list.
    for obj in sorted(build.glob('*.rel')):
        path=obj.with_suffix('.asm')
        # A freshly assembled override can coexist with an old SDCC .asm file.
        # Honor the actual project selection instead of analyzing that stale C.
        if obj.stem.startswith('vxp_') and (project/'user'/(obj.stem+'.s')).exists():
            path=project/'user'/(obj.stem+'.s')
        elif not path.exists():
            name=obj.stem.removeprefix('user_')+'.s'
            path=next((p for p in (project/'user'/name,project/'runtime'/name) if p.exists()), None)
        if path is None or not path.exists(): continue
        code, labels, functions=assembly(path)
        for name, start in functions.items():
            qualified=str(path)+':'+name
            definitions[qualified]=(code,labels,start)
            locations.setdefault(name,[]).append(qualified)
    contract_path=project/'user/stack-contracts.json'
    contracts=json.loads(contract_path.read_text()) if contract_path.exists() else {}
    external=contracts.get('functions',{})
    environment=contracts.get('environment_bytes',0)
    if type(environment) is not int or environment<0: raise ValueError('invalid environment stack allowance')
    for name,bound in external.items():
        if type(bound) is not int or bound<0: raise ValueError('invalid stack contract: '+name)
    config=plan.get('configuration',{})
    depth=config.get('call_depth',0)
    originals_path=project/'generated/source-fingerprints.json'
    if depth and originals_path.exists():
        originals=json.loads(originals_path.read_text())
        for name in ('generated/vx_banked.c','runtime/vx_runtime.c','runtime/vx_runtime.h','Makefile','local.mk'):
            digest=14695981039346656037
            for byte in (project/name).read_bytes(): digest=((digest^byte)*1099511628211)&0xffffffffffffffff
            if originals.get(name)!=f'{digest:016x}':
                depth=0;break  # Changed guards/traps do not inherit the old depth proof.
    guarded={'_'+fn['name'] for fn in plan.get('functions',[])} if depth else set()
    summaries, memo, active, assumptions={}, {}, set(), set()
    def total(name, owner='', remaining=depth):
        if name in external:
            assumptions.add(name);return external[name]
        targets=locations.get(name,[])
        target=next((q for q in targets if q.rsplit(':',1)[0]==owner), None)
        if target is None:
            if len(targets)!=1: raise OpenStack('missing or ambiguous callee: '+name)
            target=targets[0]
        key=(target,remaining)
        if key in memo: return memo[key]
        if key in active: raise OpenStack('unbounded recursive stack: '+name)
        if len(memo)+len(active)>100000: raise OpenStack('call-graph analysis budget exhausted')
        active.add(key)
        if target not in summaries:
            code,labels,start=definitions[target]
            summaries[target]=local_usage(code,labels,start,target.rsplit(':',1)[0].endswith('.asm'))
        local, calls=summaries[target]
        bound=local
        # A guarded wrapper at depth zero can only call the non-returning trap.
        # Its full local maximum is conservative; no implementation call runs.
        callees=calls.items() if name not in guarded or remaining else [(n,c) for n,c in calls.items() if n=='_vx_trap']
        child_depth=remaining-1 if name in guarded and remaining else remaining
        for callee, cost in callees:
            bound=max(bound,cost+total(callee,target.rsplit(':',1)[0],child_depth))
        active.remove(key);memo[key]=bound;return bound
    roots={}
    for source in plan.get('source_occurrences',[]):
        root='_vx_'+source['name']+('_submit' if source.get('stream') else '')
        try: roots[root]={'bytes':2+environment+total(root)}
        except (OpenStack,RecursionError) as error:
            roots[root]={'bytes':None,'reason':str(error)};active.clear()
    known=bool(roots) and all(root['bytes'] is not None for root in roots.values())
    maximum=max((root['bytes'] for root in roots.values() if root['bytes'] is not None),default=0)
    return {'status':'bounded' if known else 'unproven','bytes':maximum if known else None,
            'roots':roots,'environment_bytes':environment,'call_depth':depth,
            'external_contracts':{name:external[name] for name in sorted(assumptions)},
            'scope':'one source invocation including its return address; embedding frames excluded',
            'assumptions':['SDCC ABI 0, valid return addresses and checked switch dispatch',
                           'embedding and concurrent boundary frames fit environment_bytes'],
            'method':'SDCC assembly control-flow and call-graph analysis'}
