#!/usr/bin/env python3
"""Ordinary C retains shared DAGs inside their original evaluation intervals.

Pass --msx after the solver path to run storage, effect, lazy-initializer,
recursive and bank-continuation fixtures through guarded SDCC and the Z80.
The mixed source-ABI matrix is a separate host-only check.
"""
import importlib.util
import sys
import json
import subprocess
from pathlib import Path
import tempfile
import unittest
import test_tuples as support

RT = support.ROOT/'backends/msx-neo8/runtime'
MSX = '--msx' in sys.argv
if MSX:
    sys.argv.remove('--msx')
    spec=importlib.util.spec_from_file_location('calculation_backend',Path(__file__).parents[1]/'project_msx.py')
    backend=importlib.util.module_from_spec(spec);spec.loader.exec_module(backend)


def target_run(project):
    # A whole project can wait for other serialized builds. Individual SDCC
    # jobs retain the exported project's 180-second / 2048-MiB guard.
    return subprocess.run(['make','-C',str(project),'-j1','simulate','SIMFLAGS=--max-ticks=250000000'],
                          text=True,capture_output=True,timeout=600)


class CalculationTests(unittest.TestCase):
    def execute(self, source, oracle, budget=128, split=False, depth=0, extra="", preferences=()):
        with tempfile.TemporaryDirectory(prefix='vexel-calculations-') as temp:
            root=Path(temp);path=root/'source.vx';path.write_text(source);generated=root/'generated'
            result=support.run([support.EXE,path,'--source','entry','--budget',16,'--mode','serial',
                                '--frame-ram-bytes',budget,'--prefer','bank_chunk_operations='+('2' if split else '64'),
                                *[x for preference in preferences for x in ('--prefer',preference)],
                                '--emit-c',generated])
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            report=json.loads(result.stdout)
            functions=report['frame_planning']['functions']
            self.assertTrue(all(f['projected_local_bytes']>=f['calculation_scratch_bytes'] for f in functions))
            if not budget:self.assertTrue(all(f['frame_policy']=='automatic' for f in functions))
            text=(generated/'vx_bridges.c').read_text()
            if depth:self.assertLess(len(text),25000, 'shared DAG expanded into an exponential C tree')
            files=[generated/'vx_bridges.c'] if not split else [generated/'vx_banked.c',*[generated/(m['module']+'.c') for m in report['bank_modules']]]
            harness=root/'check.c'
            observed=[oracle(n) for n in range(256)]
            if isinstance(observed[0], (tuple,list)):
                width=len(observed[0])
                expected=','.join('{'+','.join(str(n & 0xffffffff)+'UL' for n in row)+'}' for row in observed)
                table='static const uint32_t expected[]['+str(width)+']={'+expected+'};\n'
                checks=''.join('if(vx_entry_read('+str(i)+')!=expected[n]['+str(i)+'])return '+str(i+1)+';' for i in range(width))
            else:
                expected=[n & 0xffffffff for n in observed]
                table='static const uint32_t expected[]={'+','.join(str(n)+'UL' for n in expected)+'};\n'
                checks='if(vx_entry_read(0)!=expected[n])return 1;'
            harness.write_text('#include "vexel.h"\n'+extra+'\n'+table+
                               'int main(void){unsigned n;for(n=0;n<256;++n){vx_entry((uint8_t)n);'+checks+'}return 0;}\n')
            binary=root/'check'
            built=support.run(['cc','-std=c11','-O2','-Wall','-Werror=sequence-point','-fsanitize=undefined',
                               '-fno-sanitize-recover=all','-I'+str(generated),'-I'+str(RT),*files,
                               RT/'vx_runtime.c',harness,'-o',binary])
            self.assertEqual(built.returncode,0,built.stderr)
            run=support.run([binary]);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            if MSX:
                project=root/'project';backend.package(generated,project,path,support.EXE)
                client=harness.read_text().replace('int main(void)','static uint8_t check(void)')
                (project/'user/main.c').write_text(client+'\nvolatile uint32_t vx_last_result;\n'
                    'void main(void) VX_ABI VX_RESIDENT {vx_last_result=check();__asm halt __endasm;}\n')
                target=target_run(project)
                self.assertEqual(target.returncode,0,target.stdout+target.stderr)
                result=json.loads((project/'build/execution.json').read_text())['observation']
                self.assertEqual((result['last_result'],result['fault']),(0,0),result)
            return functions

    def test_result_groups_share_complete_producers(self):
        source='&^entry(n:#u8){x:#u16=(#u16)n+257;y:#u16=x*x;(y+1,y+2,y&255)}'
        def oracle(n):
            y=(n+257)**2 & 65535
            return ((y+1)&65535,(y+2)&65535,y&255)
        for budget,split,share in ((0,False,1),(128,False,1),(0,True,1),(128,True,0)):
            with self.subTest(budget=budget,split=split,share=share):
                functions=self.execute(source,oracle,budget,split,
                    preferences=['calculation_result_sharing.vx_entry='+str(share)])
                entry=next(f for f in functions if f['name']=='vx_entry')
                self.assertEqual(entry['result_sharing'],bool(share))
                self.assertEqual(entry['result_groups'],share)
                self.assertEqual(entry['result_policy'],'calculation_result_sharing.vx_entry')
        # The same scheduling contract applies to a private materialized
        # function, including its logical-to-physical result mapping.
        private='#Triple(a:#u16,b:#u16,c:#u16);&~parts(n:#u8)->#Triple{'+\
            'x:#u16=(#u16)n+257;y:#u16=x*x;#Triple(y+1,y+2,y&255)}'+\
            '&^entry(n:#u8){r=parts(n);(r.a,r.b,r.c)}'
        functions=self.execute(private,oracle,128,True)
        self.assertTrue(any(f['name'].startswith('vx_flow_') and f['result_groups'] for f in functions))

    def test_result_groups_keep_effect_snapshots(self):
        source='&!reset(n:#u8);&!take()->#u8;&^entry(n:#u8){reset(n);'+\
            'a:#u8=take();b:#u8=take();p:#u8=a*b;(p+1,p+2,a+b)}'
        def oracle(n):
            a,b=(n+1)&255,(n+2)&255;p=a*b
            return ((p+1)&255,(p+2)&255,(a+b)&255)
        extra='static uint8_t count;void vx_external_reset(uint32_t *out,uint8_t n){count=n;out[0]=0;}'+\
            'void vx_external_take(uint32_t *out){out[0]=++count;}'
        self.execute(source,oracle,128,True,extra=extra)

    def test_result_sharing_allowances_intersect(self):
        with tempfile.TemporaryDirectory(prefix='vexel-result-policies-') as temp:
            source=Path(temp)/'source.vx'
            source.write_text('&^entry(n:#u8){x:#u16=(#u16)n+257;y:#u16=x*x;(y+1,y+2)}')
            for preferences,enabled in (([],True),(['calculation_result_sharing.vx_entry=0'],False),
                    (['calculation_result_sharing=0','calculation_result_sharing.vx_entry=1'],False)):
                run=support.run([support.EXE,source,'--program',
                    *[x for preference in preferences for x in ('--prefer',preference)]])
                self.assertEqual(run.returncode,0,run.stdout+run.stderr)
                entry=next(f for f in json.loads(run.stdout)['frame_planning']['functions'] if f['name']=='vx_entry')
                self.assertEqual(entry['result_sharing'],enabled)
                self.assertEqual(bool(entry['result_groups']),enabled)
            run=support.run([support.EXE,source,'--program','--prefer','calculation_result_sharing.missing=0'])
            self.assertNotEqual(run.returncode,0)
            self.assertIn('unknown result sharing function scope',run.stdout+run.stderr)


    def test_input_types_belong_to_each_source(self):
        with tempfile.TemporaryDirectory(prefix='vexel-calculation-types-') as temp:
            root=Path(temp);source=root/'source.vx';generated=root/'generated'
            source.write_text('&^byte(a:#u8,b:#u8){x:#u8=a+b;x*x+a}'
                              '&^word(a:#u16,b:#u16){x:#u16=a+b;x*x+a}')
            result=support.run([support.EXE,source,'--program','--emit-c',generated])
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            harness=root/'check.c';harness.write_text('''#include "vexel.h"
int main(void){uint32_t a,b,x;
    for(a=0;a<65536;a+=257)for(b=0;b<65536;b+=257){
        vx_byte((uint8_t)a,(uint8_t)b);x=(uint8_t)((uint8_t)a+(uint8_t)b);
        if(vx_byte_read(0)!=(uint8_t)(x*x+(uint8_t)a))return 1;
        vx_word((uint16_t)a,(uint16_t)b);x=(uint16_t)(a+b);
        if(vx_word_read(0)!=(uint16_t)(x*x+a))return 2;
    }return 0;
}''')
            binary=root/'check';built=support.run(['cc','-O2','-std=c11','-fsanitize=undefined',
                '-fno-sanitize-recover=all','-I'+str(generated),'-I'+str(RT),
                generated/'vx_bridges.c',RT/'vx_runtime.c',harness,'-o',binary])
            self.assertEqual(built.returncode,0,built.stderr)
            run=support.run([binary]);self.assertEqual(run.returncode,0,run.stderr)

    def test_shared_loop_and_storage(self):
        depth=12
        source='&^entry(n:#u8){key:#u8=n;x:#u8=n;i:#u8=0;(i<1)!@{'+('x=x*x+key;'*depth)+'i+=1;};x}'
        def oracle(n):
            x=n
            for _ in range(depth):x=(x*x+n)&255
            return x
        for budget,split,reuse in ((0,False,1),(128,False,1),(128,True,1),(128,False,0),(0,True,0)):
            with self.subTest(budget=budget,split=split,reuse=reuse):
                functions=self.execute(source,oracle,budget,split,depth,
                    preferences=['calculation_scratch_reuse.vx_entry='+str(reuse)])
                entry=next(f for f in functions if f['name']=='vx_entry')
                self.assertEqual(entry['retained_calculations'],11*entry['calculation_intervals'])
                self.assertEqual(entry['calculation_scratch_bytes'],2 if reuse else 11)
                self.assertEqual(entry['scratch_reuse'],bool(reuse))
                self.assertEqual(entry['scratch_reuses'],9*entry['calculation_intervals'] if reuse else 0)
                self.assertEqual(entry['scratch_policy'],'calculation_scratch_reuse.vx_entry')

    def test_storage_preferences_are_scoped_and_constrained(self):
        with tempfile.TemporaryDirectory(prefix='vexel-scratch-policies-') as temp:
            source=Path(temp)/'source.vx'
            declarations=''.join('[[c_binding('+name+',full,1,exclusive)]] &^'+name+'(n:#u8){x:#u8=n;'+
                'x=x*x+n;'*6+'x}' for name in ('left','right'))
            source.write_text(declarations)
            policies=[([], (True,True,True,True)),
                (['value_scratch_reuse.left=0'],(False,True,True,True)),
                (['calculation_scratch_reuse.vx_left=0'],(True,True,False,True)),
                (['value_scratch_reuse=0','value_scratch_reuse.left=1'],(False,False,True,True)),
                (['calculation_scratch_reuse=0','calculation_scratch_reuse.vx_left=1'],(True,True,False,False))]
            for preferences,expected in policies:
                result=support.run([support.EXE,source,'--program','--prefer','value_table_bytes=0',
                    *[x for preference in preferences for x in ('--prefer',preference)]])
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                report=json.loads(result.stdout)
                bindings={b['name']:b for b in report['value_bindings']['bindings']}
                functions={f['name']:f for f in report['frame_planning']['functions']}
                self.assertEqual(tuple(bindings[n]['codegen']['scratch_reuse'] for n in ('left','right'))+
                    tuple(functions['vx_'+n]['scratch_reuse'] for n in ('left','right')),expected)
                for name in ('left','right'):
                    b=bindings[name];self.assertEqual(b['scratch_policy'],'value_scratch_reuse.'+name)
                    self.assertEqual(b['bytes'],4 if b['codegen']['scratch_reuse'] else 7)
            # Physical alternatives must participate in the closed overlay and
            # budget plan. A larger binding can be free inside an existing
            # slot, or fall back to automatic storage when that slot cannot fit.
            source.write_text('[[c_disjoint(left,right)]] ^exclusions=0;'+declarations)
            for budget,reuse,expected_bytes,expected_storage in (
                (6,1,4,('fixed','fixed')), (6,0,4,('automatic','fixed')),
                (8,0,7,('fixed','fixed'))):
                result=support.run([support.EXE,source,'--program','--prefer','value_table_bytes=0',
                    '--prefer','value_frame_bytes='+str(budget),
                    '--prefer','value_scratch_reuse.left='+str(reuse)])
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                plan=json.loads(result.stdout)['value_bindings']
                bindings={b['name']:b for b in plan['bindings']}
                self.assertEqual(plan['static_bytes'],expected_bytes)
                self.assertEqual(tuple(bindings[n]['storage'] for n in ('left','right')),expected_storage)
            for prefix in ('value_scratch_reuse','calculation_scratch_reuse'):
                scope='left' if prefix.startswith('value') else 'vx_left'
                for order in (0,1):
                    fact='[[prefer("'+prefix+'.'+scope+'",0)]] ^policy=0;'
                    source.write_text(fact+declarations if order==0 else declarations+fact)
                    result=support.run([support.EXE,source,'--program','--prefer',prefix+'.'+scope+'=1'])
                    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                    self.assertEqual(json.loads(result.stdout)['configuration'][prefix+'.'+scope]['value'],0)
            source.write_text(declarations)
            for preference,reason in [('value_scratch_reuse.missing=0','unknown scratch reuse binding scope'),
                ('calculation_scratch_reuse.missing=0','unknown scratch reuse function scope'),
                ('value_scratch_reuse.bad-name=0','invalid scratch reuse scope')]:
                result=support.run([support.EXE,source,'--program','--prefer',preference])
                self.assertNotEqual(result.returncode,0);self.assertIn(reason,result.stderr+result.stdout)
            result=support.run([support.EXE,source,'--program','--prefer','value_scratch_reuse=2'])
            self.assertNotEqual(result.returncode,0)
            self.assertEqual(json.loads(result.stdout)['status'],'contradictory')

    def test_output_schedules_are_scoped(self):
        with tempfile.TemporaryDirectory(prefix='vexel-output-schedules-') as temporary:
            source=Path(temporary)/'source.vx'
            declarations=''.join('[[c_binding('+name+',full,1,exclusive)]] &^'+name+
                '(n:#u8,m:#u8){x:#u8=n*n+n;u:#u8=x*x+n;'
                'y:#u8=m*m+m;v:#u8=y*y+m;(u,u+x,v,v+y)}' for name in ('left','right'))
            source.write_text(declarations)
            def solve(preferences):
                return support.run([support.EXE,source,'--program','--prefer','value_table_bytes=0',
                    '--prefer','value_linear_terms=0',
                    *[x for p in preferences for x in ('--prefer',p)]])
            for preferences,expected in (([],(True,True)),
                (['value_output_schedule.left=0'],(False,True)),
                (['value_output_schedule=0','value_output_schedule.left=1'],(False,False))):
                report=json.loads(solve(preferences).stdout)
                bindings={b['name']:b for b in report['value_bindings']['bindings']}
                self.assertEqual(tuple(bindings[n]['codegen']['output_schedule'] for n in ('left','right')),expected)
                self.assertEqual(tuple(bool(bindings[n]['codegen']['early_outputs']) for n in ('left','right')),expected)
                if expected==(False,True):self.assertLess(bindings['right']['bytes'],bindings['left']['bytes'])
                self.assertEqual(bindings['left']['output_policy'],'value_output_schedule.left')
            fact='[[prefer("value_output_schedule.left",0)]] ^policy=0;'
            for text in (fact+declarations,declarations+fact):
                source.write_text(text);report=json.loads(solve(['value_output_schedule.left=1']).stdout)
                self.assertEqual(report['configuration']['value_output_schedule.left']['value'],0)
            source.write_text(declarations)
            for preference in ('value_output_schedule.missing=0','value_output_schedule.bad-name=0','value_output_schedule=2'):
                self.assertNotEqual(solve([preference]).returncode,0)

    def test_value_and_region_lifetimes(self):
        # Long chains reuse storage; earlier values must survive fan-out and
        # fixed outputs followed by factored branches. Word stores exercise
        # SDCC's byte-copy paths as well as native host arithmetic.
        with tempfile.TemporaryDirectory(prefix='vexel-value-lifetimes-') as temp:
            root=Path(temp);source=root/'source.vx'
            source.write_text('''[[c_binding(chain,full,20,exclusive)]]
&^chain(n:#u16){x:#u16=n;'''+('x=x*x+n;'*12)+'''x}
[[c_binding(branches,full,20,exclusive)]]
&^branches(n:#u16,flag:#u8){
 a:#u16=n*n+n;b:#u16=a*a+n;c:#u16=b*b+n;d:#u16=c*c+n;e:#u16=d*d+n;
 (e,e+1,flag!=0?a+e:e+3,flag!=0?b+e:e+7)
}
[[c_binding(region,region,20,exclusive)]]
&^region(n:#u8){x:#u8=n;i:#u8=0;(i<2)!@{'''+('x=x*x+n;'*12)+'''i+=1;};x}
''')
            rows=[];region=[]
            for k in range(256):
                n=k*257;x=n;steps=[]
                for _ in range(12):x=(x*x+n)&65535;steps.append(x)
                rows.append([x,steps[0],steps[1],steps[4]])
                r=k
                for _ in range(24):r=(r*r+k)&255
                region.append(r)
            # Precompute the independent oracle: the freestanding target has
            # no SDCC library multiplication routine for handwritten C.
            tables='static const uint16_t expected[][4]={'+','.join('{'+','.join(map(str,row))+'}' for row in rows)+'};\n'
            tables+='static const uint8_t expected_region[]={'+','.join(map(str,region))+'};\n'
            client='''#include "vx_bind_chain.h"
#include "vx_bind_branches.h"
#include "vx_bind_region.h"
'''+tables+'''
static uint8_t check(void){
 uint16_t k,n,a,b,e;uint8_t f;
 VX_BIND_chain_LOCAL;VX_BIND_branches_LOCAL;VX_BIND_region_LOCAL;
 for(k=0;k<256;++k){
   n=(uint16_t)(k|(k<<8));
   vx_bind_chain_frame.a0=n;vx_bind_chain_run();
   if(vx_bind_chain_frame.result.v0!=expected[k][0])return 1;
   a=expected[k][1];b=expected[k][2];e=expected[k][3];
   for(f=0;f<2;++f){
     vx_bind_branches_frame.a0=n;vx_bind_branches_frame.a1=f;vx_bind_branches_run();
     if(vx_bind_branches_frame.result.v0!=e || vx_bind_branches_frame.result.v1!=(uint16_t)(e+1) ||
        vx_bind_branches_frame.result.v2!=(uint16_t)(e+(f?a:3)) ||
        vx_bind_branches_frame.result.v3!=(uint16_t)(e+(f?b:7)))return 2;
   }
   vx_bind_region_frame.a0=(uint8_t)k;vx_bind_region_run();
   if(vx_bind_region_frame.result.v0!=expected_region[k])return 3;
 }return 0;
}
'''
            for budget,reuse in ((0,1),(128,1),(0,0),(128,0)):
                tag=str(budget)+'-'+str(reuse);generated=root/('generated-'+tag)
                result=support.run([support.EXE,source,'--program','--prefer','value_table_bytes=0',
                    '--prefer','value_scratch_reuse='+str(reuse),
                    '--prefer','value_output_schedule='+str(reuse),
                    '--prefer','value_frame_bytes='+str(budget),'--emit-c',generated])
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                bindings={b['name']:b for b in json.loads(result.stdout)['value_bindings']['bindings']}
                self.assertEqual(bindings['chain']['bytes'],8 if reuse else 26)
                self.assertEqual(bindings['region']['codegen']['region_scratch_bytes'],2 if reuse else 11)
                self.assertEqual(bindings['region']['codegen']['scratch_reuse'],bool(reuse))
                harness=root/'check.c';harness.write_text(client+'int main(void){return check();}\n')
                binary=root/'check';built=support.run(['cc','-O2','-std=c11','-fsanitize=undefined',
                    '-fno-sanitize-recover=all','-I'+str(generated),'-I'+str(RT),harness,
                    generated/'vx_binding_frames.c','-o',binary])
                self.assertEqual(built.returncode,0,built.stderr)
                run=support.run([binary]);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
                if MSX:
                    project=root/('project-'+tag);backend.package(generated,project,source,support.EXE)
                    (project/'user/frames.c').write_text('#include "vx_binding_frames.c"\n')
                    (project/'user/main.c').write_text(client+'volatile uint32_t vx_last_result;\n'
                        'void main(void) VX_ABI VX_RESIDENT {vx_last_result=check();__asm halt __endasm;}\n')
                    target=target_run(project)
                    self.assertEqual(target.returncode,0,target.stdout+target.stderr)
                    observed=json.loads((project/'build/execution.json').read_text())['observation']
                    self.assertEqual((observed['last_result'],observed['fault']),(0,0),observed)

    def test_lazy_traps_and_sibling_arguments(self):
        source='''&~sum(a:#u8,b:#u8)->#u8{a+b}
        &^entry(n:#u8){x:#u8=n*n+n;y:#u8=x*x+n;
            n==0 ? (#u8)7 : sum(y*y+n,(y*y+n)/n)}'''
        self.execute(source,oracle)

    def test_modular_linear_combinations_preserve_boundaries(self):
        cases=[
            ('&^entry(n:#u8){a:#u8=n+17;b:#u8=n*3;a+b+a-b+a}',
             lambda n:3*((n+17)&255)&255),
            ('&^entry(n:#u8){s:#i8=(#i8)n;a:#u16=(#u16)s;b:#u16=(#u16)n;a+b+a+a-b}',
             lambda n:(3*(n if n<128 else n-256))&65535),
            # A wrapped word cannot be distributed into a wider sum. Its
            # conversion stays an opaque atom in the 32-bit combination.
            ('&^entry(n:#u8){x:#u16=(#u16)n+65400;w:#u16=x+200;(#u32)w+(#u32)w+(#u32)x}',
             lambda n:2*((n+65600)&65535)+((n+65400)&65535)),
            # Collection must re-enter a constant product introduced by an
            # earlier sum. The 32-bit coefficient has a bounded shift/add
            # realization instead of requiring general multiplication.
            ('&^entry(n:#u8){x:#u32=(#u32)n*(#u32)16843009;x+x+x+x+x}',
             lambda n:(n*16843009*5)&0xffffffff),
            ('counter:#u8;&~take(n:#u8)->#u8{counter+=1;counter+n}'
             '&^entry(n:#u8){counter=0;a:#u8=take(n);b:#u8=take(n);a+a+b+counter}',
             lambda n:(3*n+6)&255)]
        for index,(source,oracle) in enumerate(cases):
            for limit in (0,16):
                with self.subTest(case=index,limit=limit):
                    functions=self.execute(source,oracle,128 if index%2 else 0,split=index>=2,
                        preferences=['calculation_linear_terms='+str(limit)])
                    if not limit:self.assertTrue(all(f['linear_combinations']==0 for f in functions))
                    elif index<4:self.assertGreater(sum(f['linear_combinations'] for f in functions),0)
    def test_linear_initializer_without_scratch(self):
        # A linear rewrite can need a helper but no scratch declaration. The
        # resident lazy initializer must still receive that helper's support.
        functions=self.execute('&!seed()->#u8;state:#u8={n:#u8=seed();n+n+n};'
            '&^entry(n:#u8){state+n}',lambda n:(21+n)&255,split=True,
            extra='void vx_external_seed(uint32_t *out){out[0]=7;}')
        initializers=[f for f in functions if f['name'].startswith('vx_ensure_')]
        self.assertTrue(any(f['linear_combinations'] and not f['calculation_scratch_bytes'] for f in initializers))

    def test_linear_choices_are_scoped_allowances(self):
        with tempfile.TemporaryDirectory(prefix='vexel-linear-policies-') as temporary:
            source=Path(temporary)/'source.vx'
            declarations=''.join('[[c_binding('+name+',full,1,exclusive)]] &^'+name+
                '(n:#u16,d:#i8){n+(#u16)d+(#u16)d}' for name in ('left','right'))
            source.write_text(declarations)
            for preferences,expected in (
                ([],(True,True,True,True)),
                (['value_linear_terms.left=0'],(False,True,True,True)),
                (['calculation_linear_terms.vx_left=0'],(True,True,False,True)),
                (['value_linear_terms=0','value_linear_terms.left=16'],(False,False,True,True))):
                report=json.loads(support.run([support.EXE,source,'--program',
                    *[x for p in preferences for x in ('--prefer',p)]]).stdout)
                bindings={b['name']:b for b in report['value_bindings']['bindings']}
                functions={f['name']:f for f in report['frame_planning']['functions']}
                actual=tuple(bool(bindings[n]['codegen']['linear_combinations']) for n in ('left','right'))
                actual+=tuple(bool(functions['vx_'+n]['linear_combinations']) for n in ('left','right'))
                self.assertEqual(actual,expected)
                self.assertEqual(bindings['left']['linear_policy'],'value_linear_terms.left')
            for prefix,scope in (('value_linear_terms','left'),('calculation_linear_terms','vx_left')):
                fact='[[prefer("'+prefix+'.'+scope+'",0)]] ^allowance=0;'
                for text in (fact+declarations,declarations+fact):
                    source.write_text(text)
                    report=json.loads(support.run([support.EXE,source,'--program','--prefer',prefix+'.'+scope+'=16']).stdout)
                    self.assertEqual(report['configuration'][prefix+'.'+scope]['value'],0)
            source.write_text(declarations)
            for preference in ('value_linear_terms.missing=0','calculation_linear_terms.missing=0',
                               'value_linear_terms.bad-name=0','value_linear_terms=65'):
                result=support.run([support.EXE,source,'--program','--prefer',preference])
                self.assertNotEqual(result.returncode,0)

    def test_effects_keep_separate_snapshots(self):
        source='''counter:#u8;
        &~advance(k:#u8)->#u8{counter+=1;x:#u8=k*k+counter;x*x+k}
        &^entry(n:#u8){counter=0;a:#u8=advance(n);b:#u8=advance(n);a+b+counter}'''
        self.execute(source,lambda n:(sum((((n*n+i)&255)**2+n)&255 for i in (1,2))+2)&255,split=True)

    def test_initializer_scratch_is_automatic(self):
        source='''&!seed()->#u8;
        state:#u8={n:#u8=seed();x:#u8=n*n+n;x*x+n};
        &^entry(n:#u8){state+n}'''
        functions=self.execute(source,lambda n:((((7*7+7)&255)**2+7)+n)&255,split=True,
                               extra='void vx_external_seed(uint32_t *out){out[0]=7;}')
        self.assertTrue(any(f['name'].startswith('vx_ensure_') and f['calculation_scratch_bytes'] and
                            f['frame_policy']=='automatic' for f in functions))

    def test_signed_and_recursive_frames(self):
        source='''&~recur(n:#u8)->#u16 {
            x:#u16=(#u16)n*(#u16)n+(#u16)n;
            n==0 ? (#u16)0 : x*x+recur(n-1)
        }
        &^entry(n:#u8){s:#i16=(#i16)((#i8)n);q:#i16=s*s+s;
            (#u16)(q*q)+recur(n%5)}'''
        def oracle(n):
            s=n if n<128 else n-256;q=(s*s+s)&65535
            return (q*q+sum((i*i+i)**2 for i in range(1,n%5+1)))&65535
        functions=self.execute(source,oracle,128,True)
        self.assertTrue(any(f['recursive'] and f['frame_policy']=='automatic' and f['calculation_scratch_bytes'] for f in functions))


def oracle(n):
    if not n:return 7
    x=(n*n+n)&255;y=(x*x+n)&255;z=(y*y+n)&255
    return (z+z//n)&255


if __name__=='__main__':unittest.main(verbosity=2)
