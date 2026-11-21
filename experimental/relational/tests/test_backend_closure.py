#!/usr/bin/env python3
"""Bank continuation semantics, mapper ownership, and assembly stack bounds."""
import ctypes
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXE=Path(sys.argv.pop(1)).resolve()
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'backends/msx-neo8/tools'))
from stack_usage import assembly, local_usage, OpenStack, analyze


def run(args):
    return subprocess.run([str(x) for x in args],capture_output=True,text=True,timeout=120)


class BackendClosureTests(unittest.TestCase):
    def test_binding_stack_facts_distinguish_translation_units_and_unknown_calls(self):
        with tempfile.TemporaryDirectory() as temp:
            project=Path(temp);build=project/'build';build.mkdir()
            for name,code in {
                'a':'_vx_entry::\n ret\n_vx_bind_move_run::\n push hl\n call _leaf\n pop hl\n ret\n_leaf::\n push bc\n pop bc\n ret\n',
                'b':'_vx_bind_move_run::\n ret\n_vx_bind_auto_compute::\n call _outside\n ret\n',
            }.items():
                (build/(name+'.rel')).touch();(build/(name+'.asm')).write_text(code)
            plan={'source_occurrences':[{'name':'entry'}],'value_bindings':{'bindings':[
                {'name':'move','storage':'fixed','bytes':9},
                {'name':'auto','storage':'automatic','bytes':9},
                {'name':'unused','storage':'fixed','bytes':4}]}}
            result=analyze(project,plan)
            self.assertEqual(result['bytes'],2) # binding facts do not extend the source-root scope
            facts=result['value_bindings']
            self.assertEqual(facts['move']['instances'],{'build/a.asm':{'bytes':8},'build/b.asm':{'bytes':2}})
            self.assertEqual(facts['auto']['status'],'unproven')
            self.assertIsNone(facts['auto']['instances']['build/b.asm']['bytes'])
            self.assertEqual(facts['unused']['status'],'not-emitted')

    def native(self, source, oracle, mapper=False):
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp); path=d/'source.vx'; path.write_text(source); generated=d/'c'
            command=[EXE,path,'--source','entry','--prefer','bank_chunk_operations=4','--emit-c',generated]
            result=run(command)
            self.assertEqual(result.returncode,0,result.stderr+result.stdout)
            report=json.loads(result.stdout)
            if not mapper: self.assertTrue(any(m['name'].startswith('vx_chunk_') for m in report['bank_modules']))
            else: self.assertEqual(report['mapper_storage']['bytes'],8)
            runtime=ROOT/'backends/msx-neo8/runtime'
            built=run(['cc','-O2','-std=c11','-shared','-fPIC','-DVX_MAPPER_SEGMENTS=2',
                '-I'+str(generated),'-I'+str(runtime),generated/'vx_banked.c',*generated.glob('vxp_*.c'),
                runtime/'vx_runtime.c',runtime/'vx_mapper.c','-o',d/'run.so'])
            self.assertEqual(built.returncode,0,built.stderr)
            dll=ctypes.CDLL(str(d/'run.so'));dll.vx_entry.argtypes=[ctypes.c_uint8];dll.vx_entry_read.restype=ctypes.c_uint32
            if mapper:
                dll.vx_mapper_init(3,1,2)
                dll.vx_mapper_write.argtypes=[ctypes.c_uint32,ctypes.c_uint8]
                dll.vx_mapper_read.argtypes=[ctypes.c_uint32];dll.vx_mapper_read.restype=ctypes.c_uint8
                dll.vx_mapper_write(16383,11);dll.vx_mapper_write(16384,29)
                self.assertEqual((dll.vx_mapper_read(16383),dll.vx_mapper_read(16384)),(11,29))
            for n in (0,1,2,3,7,16,63,255):
                dll.vx_entry(n);self.assertEqual(dll.vx_entry_read(0),oracle(n),n)

    def test_bank_cuts_preserve_loop_transfers_and_recursion(self):
        self.native('&~f(n:#u8)->#u16{a:#u16=0;i:#u16=0;(i<(#u16)n)!@{i+=1;i==3?{->>;};a+=i;}a}'
                    '&^entry(n:#u8){f(n)}',lambda n:n*(n+1)//2-(3 if n>=3 else 0))
        self.native('&~f(n:#u8)->#u16{n==0 ? (#u16)7 : (#u16)n+f(n-1)}'
                    '&^entry(n:#u8){n>16 ? {-> f(4);};f(n)}',lambda n:17 if n>16 else 7+n*(n+1)//2)

    def test_mapper_storage_and_segment_crossing(self):
        self.native('[[require(mapper_segments,2)]] config=0;[[mapper]] state:#u16[4];'
                    '&^entry(n:#u8){state=[(#u16)n,(#u16)n+300,(#u16)n+500,(#u16)n+900];state[1]+state[3]}',
                    lambda n:1200+2*n,True)
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'missing.vx';path.write_text('[[mapper]] state:#u8; &^entry(n:#u8){state=n;state}')
            result=run([EXE,path,'--source','entry'])
            self.assertEqual(json.loads(result.stdout)['status'],'contradictory')

    def test_assembly_paths_and_local_label_scopes(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'test.asm'
            path.write_text('_first::\n push ix\n ld ix,#0\n add ix,sp\n ld hl,#-8\n add hl,sp\n ld sp,hl\n'
                ' jr z,00101$\n push hl\n call _outside\n pop hl\n00101$:\n ld sp,ix\n pop ix\n ret\n'
                '_second::\n00101$:\n push bc\n pop bc\n ret\n')
            code,labels,functions=assembly(path)
            high,calls=local_usage(code,labels,functions['_first'])
            self.assertEqual((high,calls),(14,{'_outside':14}))
            self.assertEqual(local_usage(code,labels,functions['_second']),(2,{}))
            path.write_text('_A::\n PUSH HL\n POP HL\n RET\n_a::\n ret\n')
            code,labels,functions=assembly(path)
            self.assertEqual(local_usage(code,labels,functions['_A']),(2,{}))
            self.assertEqual(local_usage(code,labels,functions['_a']),(0,{}))
            path.write_text('_hex::\n ld hl,#0xfff8\n add hl,sp\n ld sp,hl\n'
                            ' ld hl,#8\n add hl,sp\n ld sp,hl\n ret\n')
            code,labels,functions=assembly(path)
            self.assertEqual(local_usage(code,labels,functions['_hex']),(8,{}))
            path.write_text('_bad::\n ld ix,#0\n add ix,sp\n ld ixh,#0\n ld sp,ix\n ret\n')
            code,labels,functions=assembly(path)
            with self.assertRaises(OpenStack):local_usage(code,labels,functions['_bad'])
            path.write_text('_bad::\n00101$:\n push hl\n jr 00101$\n')
            code,labels,functions=assembly(path)
            with self.assertRaises(OpenStack):local_usage(code,labels,functions['_bad'])
            path.write_text('_bad::\n ld sp,hl\n ret\n')
            code,labels,functions=assembly(path)
            with self.assertRaises(OpenStack):local_usage(code,labels,functions['_bad'])
            # A raw PUSH HL opcode must not disappear from a stack proof.
            path.write_text('.module test\n.area _CODE\n_bad::\n .db 0xe5\n ret\n')
            code,labels,functions=assembly(path)
            with self.assertRaises(OpenStack):local_usage(code,labels,functions['_bad'])
            # Unreached literal pools remain ordinary data.
            path.write_text('_good::\n jr 00101$\n .db 0xe5\n00101$:\n ret\n')
            code,labels,functions=assembly(path)
            self.assertEqual(local_usage(code,labels,functions['_good']),(0,{}))


    def test_stack_restoration_closes_only_a_known_common_frame(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'restore.asm'
            for saved in ('ix','iy','hl'):
                call=' call _outside\n' if saved!='hl' else ''
                path.write_text('_frame::\n push '+saved+'\n ld '+saved+',#0\n add '+saved+',sp\n'
                    ' jr z,00101$\n push bc\n push de\n'+call+
                    '00101$:\n ld sp,'+saved+'\n pop '+saved+'\n ret\n')
                code,labels,functions=assembly(path)
                self.assertEqual(local_usage(code,labels,functions['_frame']),
                                 (8,{'_outside':8}) if call else (6,{}))
            for body in ('ld ixh,#0', 'dec ix', 'push hl\n00101$:\n call _outside'):
                # An unknown/different frame address cannot become a bound.
                # Nor may differing stack depths reach a call before cleanup.
                middle=body+'\n' if '00101$:' in body else body+'\n00101$:\n'
                path.write_text('_bad::\n push ix\n ld ix,#0\n add ix,sp\n'
                    ' jr z,00101$\n'+middle+' ld sp,ix\n pop ix\n ret\n')
                code,labels,functions=assembly(path)
                with self.assertRaises(OpenStack):local_usage(code,labels,functions['_bad'])

    def test_checked_switch_keeps_stack_restoration_but_rejects_changed_hl(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'switch.asm'
            for width,jump in ((2,'jr'),(3,'jp')):
                source=('_switch::\n push de\n ld de,#0\n ld hl,#00100$\n'+
                    ' add hl,de\n'*width+' pop de\n jp (hl)\n00100$:\n '+jump+' 00101$\n '+jump+' 00102$\n'
                    '00101$:\n push bc\n pop bc\n ret\n'
                    '00102$:\n push bc\n push de\n pop de\n pop bc\n ret\n')
                path.write_text(source);code,labels,functions=assembly(path)
                self.assertEqual(local_usage(code,labels,functions['_switch']),(4,{}))
                with self.assertRaises(OpenStack):local_usage(code,labels,functions['_switch'],False)
                for changed in ('pop hl', 'pop de\n ld hl,#0'):
                    path.write_text(source.replace('pop de\n jp (hl)',changed+'\n jp (hl)'))
                    code,labels,functions=assembly(path)
                    with self.assertRaises(OpenStack):local_usage(code,labels,functions['_switch'])



if __name__=='__main__':unittest.main(verbosity=2)
