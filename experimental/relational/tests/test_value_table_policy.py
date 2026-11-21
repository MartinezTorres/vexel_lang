#!/usr/bin/env python3
"""Finite realization allowances change physical choices, never values."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXE=Path(sys.argv.pop(1)).resolve()
ROOT=Path(__file__).resolve().parents[3]

def run(args):
    return subprocess.run([str(x) for x in args],capture_output=True,text=True,timeout=120)

class PolicyTests(unittest.TestCase):
    def test_native_rule_requires_net_saving(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);source=root/'native.vx';generated=root/'generated'
            source.write_text('''
                [[c_binding(plain,full,1,exclusive)]]
                &^plain(x:#u8){x<5 ? x+1 : (#u8)0}
                [[c_binding(hidden,full,1,exclusive)]]
                &^hidden(x:#u8){z:#u8=(x^85)^85;z<5 ? z+1 : (#u8)0}
            ''')
            result=run([EXE,source,'--program','--emit-c',generated])
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            plans={b['name']:b['codegen'] for b in json.loads(result.stdout)['value_bindings']['bindings']}
            self.assertEqual(sum(plans['plain'][k] for k in ('finite_tables','finite_constants','finite_guards','finite_arithmetic')),0)
            self.assertGreater(plans['hidden']['finite_arithmetic'],0)
            self.assertEqual(plans['hidden']['finite_table_bytes'],0)
            (root/'check.c').write_text('''
                #include "vx_bind_plain.h"
                #include "vx_bind_hidden.h"
                int main(void) {
                    unsigned x;VX_BIND_plain_LOCAL;VX_BIND_hidden_LOCAL;
                    for(x=0;x<256;++x) {
                        vx_bind_plain_frame.a0=x;vx_bind_plain_run();
                        vx_bind_hidden_frame.a0=x;vx_bind_hidden_run();
                        if(vx_bind_plain_frame.result.v0!=(x<5 ? x+1 : 0) ||
                           vx_bind_hidden_frame.result.v0!=vx_bind_plain_frame.result.v0)return 1;
                    }return 0;
                }
            ''')
            result=run(['cc','-std=c11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',
                        '-I'+str(generated),'-I'+str(ROOT/'backends/msx-neo8/runtime'),
                        root/'check.c',generated/'vx_binding_frames.c','-o',root/'check'])
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual(run([root/'check']).returncode,0)

    def test_shared_producers_do_not_count_as_eliminated_work(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);source=root/'marginal.vx';generated=root/'generated'
            source.write_text('''
                [[c_binding(shared,full,1,exclusive)]]
                &^shared(x:#u8){y:#u8=(x&96)==0 ? x|96 : x;(y,(#u8)((y&144)==0))}
                [[c_binding(sole,full,1,exclusive)]]
                &^sole(x:#u8){y:#u8=(x&96)==0 ? x|96 : x;(#u8)((y&144)==0)}
            ''')
            result=run([EXE,source,'--program','--emit-c',generated])
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            plans={b['name']:b['codegen'] for b in json.loads(result.stdout)['value_bindings']['bindings']}
            self.assertEqual(plans['shared']['finite_tables'],0)
            self.assertEqual(plans['sole']['finite_tables'],1)
            (root/'check.c').write_text('''
                #include "vx_bind_shared.h"
                #include "vx_bind_sole.h"
                int main(void) {
                    unsigned x,y;VX_BIND_shared_LOCAL;VX_BIND_sole_LOCAL;
                    for(x=0;x<256;++x) {
                        y=(x&96)==0 ? x|96 : x;
                        vx_bind_shared_frame.a0=x;vx_bind_shared_run();
                        vx_bind_sole_frame.a0=x;vx_bind_sole_run();
                        if(vx_bind_shared_frame.result.v0!=y ||
                           vx_bind_shared_frame.result.v1!=((y&144)==0) ||
                           vx_bind_sole_frame.result.v0!=((y&144)==0))return 1;
                    }return 0;
                }
            ''')
            result=run(['cc','-std=c11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',
                        '-I'+str(generated),'-I'+str(ROOT/'backends/msx-neo8/runtime'),
                        root/'check.c',generated/'vx_binding_frames.c','-o',root/'check'])
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual(run([root/'check']).returncode,0)

    def test_finite_realization_policy_is_scoped_and_semantically_inert(self):
        declarations = '''
            [[c_binding(left,full,1,exclusive)]]
            &^left(x:#u8,y:#u8){z:#u8=x^y;z*7+2}
            [[c_binding(right,full,1,exclusive)]]
            &^right(x:#u8,y:#u8){z:#u8=x+y;z*11}
            [[c_binding(legacy,full,1,exclusive)]]
            &^legacy(x:#u8){(#u8)(x<32 ? 1 : (x<64 ? 2 : (x<96 ? 3 : (x<128 ? 4 : (x<160 ? 5 : 6)))))}
        '''
        policies = [
            ([], (True, True, True)),
            (['value_table_search=0'], (False, False, True)),
            (['value_table_bytes=0'], (False, False, False)),
            (['value_table_search.left=0'], (False, True, True)),
            (['value_table_bytes=128'], (False, False, True)),
            (['value_table_bytes.right=0'], (True, False, True)),
            (['value_table_bytes=0', 'value_table_bytes.left=2048'], (False, False, False)),
        ]
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp);source=d/'main.vx'
            # A source allowance and a CLI allowance are contributions to the
            # same equation; neither silently overwrites the other.
            source.write_text('[[prefer(value_table_bytes,2048)]] ^policy=0;'+declarations)
            for i,(preferences,expected) in enumerate(policies):
                generated=d/f'generated-{i}'
                command=[EXE,source,'--program','--prefer','value_frame_bytes=64','--emit-c',generated]
                for preference in preferences:command+=['--prefer',preference]
                result=run(command)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                report=json.loads(result.stdout)
                bindings={b['name']:b for b in report['value_bindings']['bindings']}
                self.assertEqual(tuple(bindings[n]['codegen']['finite_tables']>0 for n in ('left','right','legacy')),expected)
                (d/'check.c').write_text('''
                    #include "vx_bind_left.h"
                    #include "vx_bind_right.h"
                    #include "vx_bind_legacy.h"
                    int main(void) {
                        unsigned x,y,z; VX_BIND_left_LOCAL; VX_BIND_right_LOCAL; VX_BIND_legacy_LOCAL;
                        for(x=0;x<256;++x) {
                            vx_bind_legacy_frame.a0=x;vx_bind_legacy_run();
                            if(vx_bind_legacy_frame.result.v0!=(x<160 ? 1+x/32 : 6))return 1;
                            for(y=0;y<256;++y) {
                                vx_bind_left_frame.a0=x;vx_bind_left_frame.a1=y;vx_bind_left_run();z=x^y;
                                if(vx_bind_left_frame.result.v0!=(uint8_t)((z<<3)-z+2))return 2;
                                vx_bind_right_frame.a0=x;vx_bind_right_frame.a1=y;vx_bind_right_run();z=(x+y)&255;
                                if(vx_bind_right_frame.result.v0!=(uint8_t)((z<<3)+(z<<1)+z))return 3;
                            }
                        }return 0;
                    }
                ''')
                result=run(['cc','-std=c11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',
                            '-I'+str(generated),'-I'+str(ROOT/'backends/msx-neo8/runtime'),
                            d/'check.c',generated/'vx_binding_frames.c','-o',d/'check'])
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertEqual(run([d/'check']).returncode,0)
            for order in (False,True):
                policy='[[prefer("value_table_search.left",0)]] ^policy=0;'
                source.write_text(declarations+policy if order else policy+declarations)
                for schedule in ('fifo','lifo','random'):
                    result=run([EXE,source,'--program','--schedule',schedule,
                                '--prefer','value_table_search.left=64'])
                    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                    report=json.loads(result.stdout)
                    self.assertEqual(report['configuration']['value_table_search.left']['value'],0)
            for preference,diagnostic in [('value_table_search.missing=0','unknown value table binding scope'),
                                         ('value_table_bytes.bad-name=1','invalid value table binding scope')]:
                result=run([EXE,source,'--program','--prefer',preference])
                self.assertNotEqual(result.returncode,0)
                self.assertIn(diagnostic,result.stderr)
            for role,key,value in [('prefer','value_table_search',65),('prefer','value_table_bytes',2049),
                                   ('require','value_table_bytes',64)]:
                source.write_text(f'[[{role}({key},{value})]] ^policy=0;'+declarations)
                result=run([EXE,source,'--program'])
                self.assertNotEqual(result.returncode,0)
                if role=='require':self.assertIn('requires the prefer relationship',result.stderr)
                else:self.assertEqual(json.loads(result.stdout)['status'],'contradictory')


if __name__=='__main__':unittest.main(verbosity=2)
