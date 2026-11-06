#!/usr/bin/env python3
"""Type equality proofs and common witnesses under declaration/search changes."""
import itertools
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
EXE=Path(sys.argv.pop(1)).resolve()
ORDERS=list(itertools.product(('fifo','lifo','random'),('forward','reverse')))
class TypeClosureTests(unittest.TestCase):
    def check(self,source,queries,answers=None,status='resolved',extra=()):
        with tempfile.TemporaryDirectory(prefix='vexel-type-closure-') as temp:
            path=Path(temp)/'source.vx';path.write_text(source)
            for schedule,branch in ORDERS:
                with self.subTest(source=source,schedule=schedule,branch=branch):
                    args=[str(EXE),str(path),'--schedule',schedule,'--branch-order',branch,*extra]
                    for query in queries:args+=['--query',query]
                    r=subprocess.run(args,capture_output=True,text=True,timeout=120)
                    report=json.loads(r.stdout);self.assertEqual(report['status'],status,r.stdout+r.stderr)
                    if answers is not None:self.assertEqual([q['answer'] for q in report['queries']],answers)
    def test_scalar_and_array_components(self):
        for decl in itertools.permutations(('a:#[b]=(#u8)255;','b:#[a]=(#u16)2;')):
            for queries,answers in [(['type:a','type:b','value:answer'],['#u16','#u16','256']),(['value:answer','type:b','type:a'],['256','#u16','#u16'])]:
                self.check(''.join(decl)+'answer=a+1;',queries,answers)
        self.check('a:#[b]=[(#u8)255];b:#[a]=[(#u16)2];answer=a[0]+1;',['type:a','value:answer'],['#u16[1]','256'])
        self.check('a:#[a]=(#u8)255;b:#[a]=(#u16)2;answer=a+1;',['value:answer'],['256'])
    def test_array_properties_use_the_joined_interface(self):
        self.check('a:#[b]=[(#u8)255];b:#[a]=[(#u16)2];answer={v:#[a[0]]=300;v};',['length:a','value:answer'],['1','300'])
        self.check('a:#[b]=[1,2];b:#[a]=[1,2,3];',['length:a'],status='contradictory')
    def test_outer_category_and_member_shapes_are_independent(self):
        self.check('a:#Missing[2];answer=|a|;', ['value:answer'], ['2'])
        self.check('&!get()->#Missing[3];answer=|get()|;', ['value:answer'], ['3'])
        self.check('#P(data:#Missing[3],other:#Missing);p:#P;answer=|p.data|;', ['value:answer'], ['3'])
        for fields in itertools.permutations(('a:#Missing[|b|+1]', 'b:#Missing[7-|a|]')):
            self.check('#P('+','.join(fields)+');p:#P;answer=|p.a|*10+|p.b|;', ['value:answer'], ['43'])
        for declarations in itertools.permutations(('a:#[b]=#P([1,2],missing());', 'b:#[a]=#P([3,4],missing());')):
            self.check('#P(data,other:#Missing);'+''.join(declarations)+'answer=|a.data|;', ['value:answer'], ['2'])
        self.check('#P(data,other:#Missing);a:#[b]=#P([1,2],missing());b:#[a]=#P([3],missing());answer=|a.data|;', ['value:answer'], status='contradictory')
    def test_ordinary_helper_proofs(self):
        for helper in ('&identity(x){x}', '&inner(x){x}&identity(x){inner(x)}', '&identity($x){$x}'):
            self.check(helper+'a:#[identity(b)]=(#u8)255;b:#[identity(a)]=(#u16)2;answer=a+1;',['type:a','type:b','value:answer'],['#u16','#u16','256'])
        self.check('#P(a:#[b],b:#[a]);answer={p=#P((#u8)255,(#u16)2);p.a+1};',['value:answer'],['256'])
        self.check('&identity(x){x}#P(a:#[identity(b)],b:#[identity(a)]);answer={p=#P((#u8)255,(#u16)2);p.a+1};',['value:answer'],['256'])
    def test_member_properties_preserve_the_owner_interface(self):
        for decl in itertools.permutations(('a:#[b]=#Box((#u8)255);','b:#[a]=#Box((#u16)2);')):
            self.check('#Box(x);'+''.join(decl)+'answer={v:#[a.x]=300;v};',
                       ['type:a.x','type:b.x','value:answer','value:a.x'],['#u16','#u16','300','255'])
        self.check('#Box(x,y);missing:#u8=missing;a:#[b]=#Box((#u8)255,missing);b:#[a]=#Box((#u16)2,missing);',
                   ['type:a.x'],['#u16'])
        self.check('#Inner(x);#Outer(inner);a:#[b]=#Outer(#Inner((#u8)255));b:#[a]=#Outer(#Inner((#u16)2));',
                   ['type:a.inner.x','value:a.inner.x'],['#u16','255'])
        self.check('#A(x);#B(x);a:#[b]=#A(1);b:#[a]=#B(1);',['type:a.x'],status='contradictory')
    def test_projection_proofs_do_not_read_opaque_initializers(self):
        # Selecting from a newly constructed wrapper really is an identity.
        self.check('#Box(x);&identity(x){#Box(x).x}a:#[identity(b)]=(#u8)255;b:#[identity(a)]=(#u16)2;answer=a+1;',
                   ['type:a','value:answer'],['#u16','256'])
        # A projection from an unknown owner is not a proof about its witness.
        self.check('#Box(x);a:#[b.x]=#Box((#u8)1);b:#[a.x]=#Box((#u16)2);',
                   ['type:a'],status='contradictory',extra=('--max-nodes','1500'))
    def test_member_types_use_state_after_captured_writes(self):
        self.check('#P(v);&f(){|{->7;0}|;#P(2)}a=f();', ['type:a.v'], status='contradictory')
        for tail in ('',',data:#Missing'):
            extra=',missing()' if tail else ''
            self.check('&f(){x:#u8=0;&set(){x=1;}set();#P(v:#[x==0 ? (#u8)1 : (#u16)1]'+tail+');#P(42'+extra+')}answer={v:#[f().v]=300;v};',
                       ['value:answer'],['300'])
        self.check('&f(){x:#u8=0;&set(){x=1;}set();#P(v:#[x==0 ? (#u8)1 : (#u16)1]);#P(42)}p=f();',
                   ['type:p'],['#P(v:#u16){Captures(type:x(UInt(8)),x(1))}'])
    def test_record_array_elements_join_structurally(self):
        self.check('#Box(x);a=[#Box((#u8)255),#Box((#u16)2)];answer=a[0].x+1;',
                   ['type:a','value:answer'],['#Box(x:#u16)[2]','256'])
    def test_transforming_equations_revise_together(self):
        for decl in itertools.permutations(('a:#[b+1]=(#u8)255;','b:#[a+1]=(#u16)2;')):
            self.check(''.join(decl)+'answer=a+1;',['type:a','type:b','value:answer'],['#u16','#u16','256'])
        self.check('&copy(n){n}a:#[copy(b)+1]=(#u8)255;b:#[copy(a)+1]=(#u16)2;answer=a+1;',
                   ['value:answer'],['256'])
        self.check('a:#[b[0]]=(#u8)255;b:#[[a]]=[(#u16)2];answer=a+1;',
                   ['value:answer'],['256'])
        self.check('#Pair(a:#[b+1],b:#[a+1]);answer={p=#Pair((#u8)255,(#u16)2);p.a+1};',
                   ['value:answer'],['256'])
    def test_transforming_structural_properties(self):
        self.check('#Box(x);a:#[#Box(b.x+1)]=#Box((#u8)255);b:#[#Box(a.x+1)]=#Box((#u16)2);answer=a.x+1;',
                   ['type:a.x','value:answer'],['#u16','256'])
        self.check('#Box(x,data:#Missing);a:#[#Box(b.x+1,b.data)]=#Box((#u8)255,missing());b:#[#Box(a.x+1,a.data)]=#Box((#u16)2,missing());answer={v:#[a.x]=300;v};',
                   ['value:answer'],['300'])
    def test_nonmonotone_alternatives_and_consumers(self):
        for helper in ('&flip(x:#u8)->#u16{0}&flip(x:#u16)->#u8{0}',
                       '&flip(x:#u8){(#u16)0}&flip(x:#u16){(#u8)0}'):
            for declarations in itertools.permutations(('a:#[flip(b)]=(#u8)0;', 'b:#[flip(a)]=(#u8)0;')):
                source=helper+''.join(declarations)
                self.check(source,['type:a'],status='ambiguous')
                # The entire type vector need not be unique if the demanded
                # observations agree. No inner solver may choose it early.
                self.check(source+'answer=a+b;',['type:answer','value:answer'],['#u16','0'])
                self.check(source+'&only8(x:#u8){x}answer=only8(a);',
                           ['value:answer','type:a','type:b'],['0','#u8','#u16'])
            self.check(helper+'a:#[flip(b)]=(#u8)0;b:#[flip(a)]=(#u16)0;',
                       ['type:a','type:b'],['#u8','#u16'])
            self.check(helper+'a:#[flip(b)]=(#u8)0;b:#[flip(a)]=300;',
                       ['value:b','type:a','type:b'],['300','#u8','#u16'])
            self.check(helper+'a:#[flip(a)]=(#u8)0;',['type:a'],status='contradictory')
            self.check(helper+'a:#[flip(b)]=(#u8)0;b:#[flip(c)]=(#u8)0;c:#[flip(a)]=(#u8)0;',
                       ['type:a'],status='contradictory')
            self.check(helper+'#Box(x,data:#Missing);a:#[#Box(flip(b.x),b.data)]=#Box((#u8)0,missing());b:#[#Box(flip(a.x),a.data)]=#Box((#u8)0,missing());',
                       ['type:a.x'],status='ambiguous')
    def test_finite_type_domain_oracle(self):
        widths=(8,16,32)
        for mapping in itertools.product(widths,repeat=3):
            f=dict(zip(widths,mapping));domain={8}
            while domain != domain | {f[x] for x in domain}:domain |= {f[x] for x in domain}
            # Same-sign scalar joins are maximum, already in this domain.
            solutions=[(a,b) for a,b in itertools.product(sorted(domain),repeat=2) if a==f[b] and b==f[a]]
            status='ambiguous' if len(solutions)>1 else 'resolved' if solutions else 'contradictory'
            source=''.join('&step(x:#u'+str(a)+')->#u'+str(b)+'{0}' for a,b in f.items())
            source+='a:#[step(b)]=(#u8)0;b:#[step(a)]=(#u8)0;'
            answers=['#u'+str(x) for x in solutions[0]] if len(solutions)==1 else None
            self.check(source,['type:a','type:b'],answers,status)
    def test_provisional_errors_and_witness_obligations(self):
        self.check('&select(n:#u16){n}&select(n:#u32){n}a:#[select(b)]=(#u8)1;b:#[a+(#u32)0]=(#u8)2;answer=a;',
                   ['type:a','value:answer'],['#u32','1'])
        self.check('a:#[b+1]=(#u8)(1/0);b:#[a+1]=(#u16)2;answer={v:#[a]=300;v};',
                   ['value:answer'],['300'])
        self.check('a:#[b+1]=(#u8)(1/0);b:#[a+1]=(#u16)2;', ['value:a'],status='contradictory')
        self.check('a:#[b+1]=300;b:#[a+1]=(#u8)2;',['value:a'],status='contradictory')
        self.check('a:#[b+1]=b;b:#[a+1]=a;',['type:a'],status='unresolved')
        self.check('a:#[{data:#u8[1/(b+1)];data}]=(#u8[0])[];b:#[a[0]+(#u16)0]=(#u8)255;answer=|a|;',
                   ['value:answer'],['0'])
    def test_partial_transformations_and_nominal_constraints(self):
        self.check('answer={unknown:#u8;#Box(x,data:#u8[x+unknown]);#Pair(a:#[#Box(b.x+1,b.data)],b:#[#Box(a.x+1,a.data)]);v:#[#Pair(#Box((#u8)2,[1,2,3]),#Box((#u16)2,[1,2,3])).a.x]=300;v};',
                   ['value:answer'],['300'])
        self.check('#A(x);#B(x);a:#[#B(b.x+1)]=#A((#u8)1);b:#[#A(a.x+1)]=#B((#u16)2);answer={v:#[a.x]=300;v};',
                   ['value:answer'],status='contradictory')
    def test_witnesses_follow_the_revised_context(self):
        self.check('&pick(x:#u8){(#u8)1}&pick(x:#u16){(#u16)1}a:#[b+1]=pick(b);b:#[a+1]=(#u16)2;answer=a+1;',
                   ['type:a','value:answer'],['#u16','2'])
        self.check('#Box(x);&f(x:#Box){x.x}&f(x){x}a:#[f(b)]=(#u8)255;b:#[a]=(#u16)2;answer=a+1;',
                   ['value:answer'],['256'])
        self.check('a:#[b+1]=(#u8)255;b:#[a+1]=(#u16)2;answer=a+1;',
                   ['value:answer'],status='exhausted',extra=('--max-work','100'))
    def test_incomplete_incoming_relationship_prevents_publication(self):
        base='a:#[a]=(#u8)255;b:#[flag ? a : (#u16)0]=(#u16)2;answer=a+1;'
        self.check('flag:#b=flag;'+base,['value:answer'],status='unresolved')
        self.check('flag:#b=1;'+base,['value:answer'],['256'])
        self.check('flag:#b=0;'+base,['value:answer'],['0'])
        self.check('flag:#b=flag;&select(){flag ? a : (#u16)0}a:#[a]=(#u8)255;b:#[select()]=(#u16)2;answer=a+1;',['value:answer'],status='unresolved')
    def test_anchors_missing_witnesses_and_undemanded_groups(self):
        self.check('a:#u8=255;b:#[a]=(#u16)2;answer=b;',['value:answer'],status='contradictory')
        self.check('a:#[b];b:#[a];answer=1;',['value:answer'],['1'])
        self.check('a:#[b];b:#[a];',['type:a'],status='unresolved')
        self.check('a:#[b]=unknown();b:#[a]=(#u16)2;',['type:a'],status='unresolved')
        self.check('a:#[b]=(#u8)255;b:#[a]=(#u8)2;unused:#[missing()]=bad();answer=a+1;',['value:answer'],['0'])
        self.check('#A(x);#B(x);a:#[b]=#A(1);b:#[a]=#B(1);',['type:a'],status='contradictory')
if __name__=='__main__':unittest.main(verbosity=2)
