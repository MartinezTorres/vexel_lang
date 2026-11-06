#!/usr/bin/env python3
"""Record dimensions are field relationships, independent of argument order."""
import ctypes
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_msx import EXE, project_environment, run

SCHEMA='#Packet(count:#u8,data:#u8[count]);'
CAPTURED_INTERFACE='''&^captured_interface(n:#u8)->#u8{
    offset:#u8=1;#P(count:#u8,data:#u8[count+offset]);
    &~descend(k:#u8,item:#[#P(2,[1,2,3])])->#[#P(2,[1,2,3])]{
        k==0?item:descend(k-1,item)
    }
    p=descend(n,#P(2,[1,2,3]));p.count=2;p.data[0]
}'''
RUNTIME=SCHEMA+'''
&^make(count:#u8,x:#u8)->#u8{p=#Packet(count,[x,2,3]);p.data[0]+p.count}
&^update(count:#u8)->#u8{p=#Packet(3,[1,2,3]);p.count=count;p.data[2]}
&^element(x:#u8)->#u8{p=#Packet(3,[1,2,3]);p.data[1]=x;p.data[1]}
&^discard(count:#u8)->#u8{p=#Packet(count,[1,2,3]);7}
&^decode(count:#u8)->#u8{raw:#u8[4]=[count,1,2,3];p=(#[#Packet(3,[1,2,3])])raw;p.data[2]}
&width(count:#u8,unused:#u8){count+=1;count}
#WithHelper(count:#u8,data:#u8[width(count,0)]);
&^helper_guard(count:#u8)->#u8{p=#WithHelper(count,[4,5,6]);p.data[0]}
&(self)#u8::extent(){self+=1;self}
#WithReceiver(count:#u8,data:#u8[count.extent()]);
&^receiver_guard(count:#u8)->#u8{p=#WithReceiver(count,[7,8,9]);p.data[0]}
#Result(value:#u8);
&~walk(count:#u8)->#Result{(count==0)?#Result(11):walk(count-1)}
&^closed_interface(count:#u8)->#u8{walk(count).value}
'''+CAPTURED_INTERFACE

class DependentRecordTests(unittest.TestCase):
    def check(self,source,expected=None,error=None):
        with tempfile.TemporaryDirectory(prefix='vexel-dependent-source-') as directory:
            path=Path(directory)/'main.vx';path.write_text(source)
            for schedule in ('fifo','lifo','random'):
                result=subprocess.run([str(EXE),str(path),'--json','--schedule',schedule],capture_output=True,text=True,timeout=30)
                if error:
                    self.assertEqual(result.returncode,1,result.stderr);self.assertIn(error,result.stderr)
                else:
                    self.assertEqual(result.returncode,0,result.stderr)
                    self.assertEqual(json.loads(result.stdout)['roots'][0]['value'],expected)

    def test_constructor_shapes_and_forward_field_names(self):
        self.check(SCHEMA+'^answer:#u8={p=#Packet(3,[1,2,3]);p.data[2]+p.count};','6')
        self.check('#Packet(data:#u8[count],count:#u8);^answer:#u8={p=#Packet([1,2,3],3);p.data[2]+p.count};','6')
        self.check(SCHEMA+'^answer=#Packet(2,[1,2,3]);',error='dimension does not match')
        self.check('#Grid(rows:#u8,cols:#u8,data:#u8[cols][rows]);^answer:#u8={p=#Grid(2,3,[[1,2,3],[4,5,6]]);p.data[1][2]};','6')

    def test_types_do_not_read_dimension_values(self):
        self.check(SCHEMA+'^answer:#u8={n:#u8;v:#[#Packet(n,[1,2,3]).data]=[4,5,6];v[1]};','5')
        self.check(SCHEMA+'^answer:#u8={v:#[#Packet(3,unavailable()).count]=42;v};','42')
        self.check('#Pair(a:#[b],b:#u8);^answer:#u8={p=#Pair(1,2);p.a+p.b};','3')
        self.check('#Packet(count:#[data[0]],data:#u8[count]);^answer:#u8={p=#Packet(3,[1,2,3]);p.count};','3')
        self.check('#P(n:#u8,data:(#u8[n],#u8));^answer:#u8={p=#P(2,([4,5],7));p.data.__0[1]};','5')

    def test_field_types_do_not_close_unrelated_schema_captures(self):
        prefix='&make(offset:#u8){#P(count:#u8,data:#u8[count+offset]);#P(2,[1,2,3])}'
        self.check(prefix+'^answer:#u8={offset:#u8;v:#[make(offset).count]=7;v};','7')
        self.check(prefix+'^answer:#u8={offset:#u8;v:#[make(offset).data]=[4,5,6];v[1]};','5')
        self.check(prefix+'&!read()->#u8;^answer:#u8={v:#[make(read()).data[0]]=7;v};','7')
        self.check(prefix+'&identity(p){p}^answer:#u8={offset:#u8;v:#[identity(make(offset)).count]=7;v};','7')
        self.check(prefix+'^answer:#u8={offset:#u8;v:#[[make(offset)][0].count]=7;v};','7')
        self.check('#P(count:#u8,data:#Missing);^answer:#u8={v:#[#P(2,unavailable()).count]=7;v};','7')
        self.check('#P(count:#u8,data:#Missing);^answer:#b=#P(2,unavailable()).?count;','1')
        self.check('&make(offset:#u8){#P(count:#u8,data:#u8[offset]);#P(2,[1,2,3])}^answer:#u8={offset:#u8;v:#[make(offset).count]=7;v};','7')

    def test_whole_record_types_still_close_their_schema(self):
        prefix='&make(offset:#u8){#P(count:#u8,data:#u8[count+offset]);#P(2,[1,2,3])}'
        self.check(prefix+'^answer:#u8={offset:#u8;v:#[make(offset)];7};',error='uninitialized local: offset')
        self.check(prefix+'^answer:#u8={offset:#u8;p=make(offset);p.count};',error='uninitialized local: offset')
        self.check('&make(offset:#u8){#P(count:#u8,data:#u8[offset]);#P(2,[1,2,3])}^answer:#u8={offset:#u8;v:#[make(offset).data];7};',error='uninitialized local: offset')

    def test_recursive_interface_closes_unchanged_lexical_captures(self):
        self.check(CAPTURED_INTERFACE,'runtime')
        function=CAPTURED_INTERFACE.replace('&^captured_interface','&captured_interface')
        self.check(function+'^answer:#u8=captured_interface(3);','1')
        self.check(function+'^answer:#u8={n:#u8;v:#[captured_interface(n)]=7;v};','7')

    def test_nested_representation_casts_preserve_invariants(self):
        self.check(SCHEMA+'^answer:#u8={raw:#u8[8]=[3,1,2,3,3,4,5,6];p=(#[#Packet(3,[1,2,3])][2])raw;p[1].data[2]};','6')
        self.check(SCHEMA+'^answer:#u8={raw:#u8[8]=[3,1,2,3,2,4,5,6];p=(#[#Packet(3,[1,2,3])][2])raw;p[1].data[2]};',error='dimension does not match')

    def test_implicit_aggregate_conversions_recheck_invariants(self):
        prefix='#P(n,offset,data:#u8[n+offset]);'
        target='#[#P((#u16)0,(#u16)0,[])]'
        narrow='#P((#u8)255,(#u8)1,[])'
        self.check(prefix+'&read(p:'+target+'){p.n}^answer:#u16=read('+narrow+');',error='dimension does not match')
        self.check(prefix+'&!read(p:'+target+')->#u16;&^answer()->#u16{read('+narrow+')}',error='dimension does not match')
        self.check(prefix+'^answer:#u16=['+narrow+',#P((#u16)0,(#u16)0,[])][0].n;',error='dimension does not match')
        self.check(prefix+'#Holder(p:'+target+');^answer:#u16=#Holder('+narrow+').p.n;',error='dimension does not match')
        self.check(prefix+'&read(p:'+target+'){p.n}^answer:#u8={v:#[read('+narrow+')]=7;(#u8)v};','7')

    def test_cyclic_field_type_aliases_join_all_argument_types(self):
        self.check('#Pair(a:#[b],b:#[a]);^answer:#u16={p=#Pair((#u8)255,(#u16)2);p.a+1};','256')
        self.check('#Pair(b:#[a],a:#[b]);^answer:#u16={p=#Pair((#u16)2,(#u8)255);p.a+1};','256')
        self.check('#Triple(a:#[b],b:#[c],c:#[b]);^answer:#u16={p=#Triple((#u16)2,(#u8)255,1);p.b+1};','256')
        self.check('#Self(x:#[x]);^answer:#u8=#Self(42).x;','42')
        self.check('#Pair(a:#[b],b:#[a]);^answer:#u8={p=#Pair((#u8)2,300);p.a};',error='not representable')
        self.check('#Pair(a:#[b],b:#u8);^answer:#u8={p=#Pair((#u16)2,3);p.a};',error='without narrowing')

    def test_cyclic_field_types_do_not_demand_argument_values(self):
        self.check('#Pair(a:#[b],b:#[a]);^answer:#u8={v:#[#Pair(1/0,(#u8)2).a]=7;v};','7')

    def test_symbolic_dimensions_constrain_choices(self):
        self.check(SCHEMA+'^answer:#u8=?{n=?(#u8,0..6);p=#Packet(n,[1,2,3]);p.count};','3')
        self.check(SCHEMA+'^answer:#u8=?{n=?(#u8,0..6);p=#Packet(3,[1,2,3]);p.count=n;n};','3')
        self.check(SCHEMA+'^answer:#u8=?{n=?(#u8,0..2);p=#Packet(n,[1,2,3]);p.count};',error='no solution')

    def test_updates_preserve_shape_relationship(self):
        self.check(SCHEMA+'^answer:#u8={p=#Packet(3,[1,2,3]);p.count=3;p.data[1]=7;p.data[1]};','7')
        self.check(SCHEMA+'^answer:#u8={p=#Packet(3,[1,2,3]);p.count=2;7};',error='dimension does not match')
        self.check(SCHEMA+'&(p)#Packet::bad(){p.count=2;} ^answer:#u8={p=#Packet(3,[1,2,3]);p.bad();7};',error='dimension does not match')

    def test_scoped_schema_captures_and_nominal_constraints(self):
        self.check('^answer:#u8={offset:#u8=1;#P(n:#u8,data:#u8[n+offset]);p=#P(2,[1,2,3]);p.data[0]=9;p.data[0]};','9')
        self.check(SCHEMA+'&read(p:#Packet){p.data[1]}^answer:#u8=read(#Packet(3,[1,2,3]));','2')

    def test_schema_captures_are_frozen_and_specialize_identity(self):
        self.check('^answer:#u8={unused:#u8[1];#P(n:#u8,data:#u8[n+|unused|]);p=#P(2,[1,2,3]);p.data[1]};','2')
        self.check('^answer:#u8={offset:#u8=1;#P(n:#u8,data:#u8[n+offset]);p=#P(2,[1,2,3]);offset=2;p.n=2;p.data[0]};','1')
        prefix='&make(offset:#u8,n:#u8){#P(count:#u8,data:#u8[count+offset]);#P(n,[1,2,3])}'
        self.check(prefix+'&!flag()->#b;&^read()->#u8{p=flag()?make(1,2):make(1,2);p.count=2;p.data[0]}','1')
        self.check(prefix+'&^read(flag:#b)->#u8{p=flag?make(1,2):make(2,1);p.count=2;p.data[0]}',error='same type')
        self.check('^answer:#u8={offset:#u8=1;&width(n:#u8){n+offset}#P(count:#u8,data:#u8[width(count)]);p=#P(2,[1,2,3]);p.data[1]};','2')

    def test_runtime_schema_inputs_are_owned_fields(self):
        prefix='&!offset()->#u8;&~make(n:#u8){local=offset();#P(count:#u8,data:#u8[count+local]);#P(n,[1,2,3])}'
        self.check(prefix+'&^read(n:#u8)->#u8{p=make(n);p.count=n;p.data[0]}',error='runtime dependencies in explicit fields')
        self.check('&^read(offset:#u8)->#u8{&width(n:#u8){n+offset}#P(count:#u8,data:#u8[width(count)]);p=#P(2,[1,2,3]);p.data[0]}',error='runtime dependencies in explicit fields')
        self.check('offset:#u8=1;^answer:#u8={#P(count:#u8,data:#u8[count+offset]);p=#P(2,[1,2,3]);p.count};','2')

    def test_schema_captures_use_the_selected_overload(self):
        for overloads in ('&width(n:#u8){n+1}&width(n:#u16){n+unused}',
                          '&width(n:#u16){n+unused}&width(n:#u8){n+1}'):
            self.check('^answer:#u8={unused:#u8;'+overloads+'#P(count:#u8,data:#u8[width(count)]);p=#P(2,[1,2,3]);p.data[0]};','1')
        self.check('^answer:#u8={unused:#u8;&width(n:#u8){n+1}&width(n:#u16){n+unused}&forward(n){width(n)}#P(count:#u8,data:#u8[forward(count)]);p=#P(2,[1,2,3]);p.data[0]};','1')
        self.check('&!read()->#u8;&^answer()->#u8{unused=read();&width(n:#u8){n+1}&width(n:#u16){n+unused}#P(count:#u8,data:#u8[width(count)]);p=#P(2,[1,2,3]);p.data[0]}','1')
        self.check('^answer:#u8={unused:#u8;&width(n:#u8){n+unused}&width(n:#u16){n+1}#P(count:#u8,data:#u8[width(count)]);p=#P(2,[1,2,3]);p.data[0]};',error='uninitialized local')

    def test_invariants_keep_mutation_inside_their_private_region(self):
        self.check('^answer:#u8={offset:#u8=1;&width(n:#u8){offset+=1;n+offset}#P(count:#u8,data:#u8[width(count)]);p=#P(2,[1,2,3,4]);p.count};',error='cannot modify enclosing storage')
        self.check('^answer:#u8={offset:#u8=1;&width(){offset+=1;offset}#P(count:#u8,data:#u8[count+width()]);p=#P(2,[1,2,3,4]);p.count};',error='cannot modify enclosing storage')
        self.check('^answer:#u8={offset:#u8=1;&width(n:#u8){n+=offset;n}#P(count:#u8,data:#u8[width(count)]);p=#P(2,[1,2,3]);p.data[0]};','1')
        self.check('^answer:#u8={offset:#u8=1;&width(n:#u8){(n==0)?{offset+=1;};n+1}#P(count:#u8,data:#u8[width(count)]);p=#P(2,[1,2,3]);p.data[0]};','1')
        self.check('^answer:#u8=?{offset=?(#u8,1..3);#P(n:#u8,data:#u8[n+offset]);p=#P(2,[1,2,3]);offset};','1')

    def test_invariant_methods_mutate_only_private_receivers(self):
        method='&(self)#u8::extent(){self+=1;self}'
        self.check(method+'#P(n:#u8,data:#u8[n.extent()]);^answer:#u8={p=#P(2,[4,5,6]);p.data[0]+p.n};','6')
        self.check(method+'^answer:#u8={offset:#u8=1;#P(n:#u8,data:#u8[n+offset.extent()]);p=#P(2,[1,2,3,4]);p.n};',error='cannot modify enclosing storage')

    def test_runtime_projection_and_retained_checks(self):
        with tempfile.TemporaryDirectory(prefix='vexel-dependent-host-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            run([EXE,source,'--emit-msx',project])
            library=root/'test.so'
            run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(project/'generated'),'-I'+str(project/'runtime'),
                 *sorted((project/'generated').glob('*.c')),project/'runtime/vx_runtime.c','-o',library])
            dll=ctypes.CDLL(str(library))
            for name,args,expected in [('make',[3,17],20),('update',[3],3),('element',[42],42),('discard',[3],7),('decode',[3],3),('helper_guard',[2],4),('receiver_guard',[2],7),('closed_interface',[3],11),('captured_interface',[3],1)]:
                fn=getattr(dll,'vx_'+name);fn.argtypes=[ctypes.c_uint8]*len(args);fn.restype=ctypes.c_uint8
                self.assertEqual(fn(*args),expected)
            for name in ('make','update','discard','decode','helper_guard','receiver_guard'):
                self.assertIn('vx_trap(9)',(project/f'generated/vxp_{name}.c').read_text())

    def test_owned_schema_inputs_survive_materialized_calls(self):
        with tempfile.TemporaryDirectory(prefix='vexel-owned-schema-') as directory:
            root=Path(directory);source=root/'main.vx';project=root/'project'
            source.write_text('''&!schema_offset()->#u8;
&~make(n:#u8){#P(offset:#u8,count:#u8,data:#u8[count+offset]);#P(schema_offset(),n,[1,2,3])}
&^read(n:#u8)->#u8{p=make(n);p.count=n;p.offset+p.count}''')
            run([EXE,source,'--emit-msx',project])
            stub=root/'stub.c';stub.write_text('#include <stdint.h>\nunsigned calls;\nuint8_t vx_ext_schema_offset(void){++calls;return 1;}\n')
            library=root/'test.so'
            run(['cc','-std=c11','-O2','-shared','-fPIC','-I'+str(project/'generated'),'-I'+str(project/'runtime'),
                 *sorted((project/'generated').glob('*.c')),project/'runtime/vx_runtime.c',stub,'-o',library])
            dll=ctypes.CDLL(str(library));fn=dll.vx_read;fn.argtypes=[ctypes.c_uint8];fn.restype=ctypes.c_uint8
            self.assertEqual(fn(2),3);self.assertEqual(ctypes.c_uint.in_dll(dll,'calls').value,1)
            self.assertEqual(fn(2),3);self.assertEqual(ctypes.c_uint.in_dll(dll,'calls').value,2)

    def test_z80_dimensions_and_failed_invariant(self):
        with tempfile.TemporaryDirectory(prefix='vexel-dependent-z80-') as directory:
            root=Path(directory);source=root/'main.vx';source.write_text(RUNTIME);project=root/'project'
            run([EXE,source,'--emit-msx',project])
            (project/'user/main.c').write_text('''#include "vexel.h"
volatile uint32_t vx_last_result;
void main(void) VX_ABI VX_RESIDENT {
    if(vx_make(3,17)!=20 || vx_update(3)!=3 || vx_element(42)!=42 || vx_decode(3)!=3 || vx_helper_guard(2)!=4 || vx_receiver_guard(2)!=7) vx_trap(90);
    if(vx_closed_interface(3)!=11 || vx_captured_interface(3)!=1) vx_trap(90);
    vx_last_result=vx_discard(3); __asm halt __endasm;
}''')
            run(['make','-C',project,'-j2','simulate','SIMFLAGS=--irq-period 997 --expanded-slot'],env=project_environment(),timeout=180)
            for call in ('vx_make(2,17)','vx_update(2)','vx_discard(2)','vx_decode(2)','vx_helper_guard(1)','vx_receiver_guard(1)'):
                (project/'user/main.c').write_text('#include "vexel.h"\nvolatile uint32_t vx_last_result;\n'
                    'void main(void) VX_ABI VX_RESIDENT {vx_last_result='+call+';__asm halt __endasm;}\n')
                result=subprocess.run(['make','-C',str(project),'-j2','simulate'],env=project_environment(),capture_output=True,text=True,timeout=180)
                self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIn('runtime trapped with reason 9',result.stdout+result.stderr)
                self.assertFalse((project/'build/execution.json').exists())

    def test_z80_converted_arguments_arrays_and_nested_fields(self):
        with tempfile.TemporaryDirectory(prefix='vexel-converted-invariants-') as directory:
            root=Path(directory);source=root/'main.vx';project=root/'project'
            source.write_text('''#P(n,offset,data:#u8[n+offset]);
&~wide(p:#[#P((#u16)0,(#u16)0,[])])->#u16{p.n}
#Holder(p:#[#P((#u16)0,(#u16)0,[])]);
&^call_guard(n:#u8,o:#u8)->#u16{wide(#P(n,o,[]))}
&^array_guard(n:#u8,o:#u8)->#u16{[#P(n,o,[]),#P((#u16)0,(#u16)0,[])][0].n}
&^nested_guard(n:#u8,o:#u8)->#u16{#Holder(#P(n,o,[])).p.n}''')
            run([EXE,source,'--emit-msx',project])
            (project/'user/main.c').write_text('#include "vexel.h"\nvolatile uint32_t vx_last_result;\n'
                'void main(void) VX_ABI VX_RESIDENT {vx_last_result=vx_call_guard(0,0)+vx_array_guard(0,0)+vx_nested_guard(0,0);__asm halt __endasm;}\n')
            run(['make','-j2','simulate'],cwd=project,env=project_environment(),timeout=180)
            self.assertEqual(json.loads((project/'build/execution.json').read_text())['observation']['last_result'],0)
            for name in ('call_guard','array_guard','nested_guard'):
                (project/'user/main.c').write_text('#include "vexel.h"\nvolatile uint32_t vx_last_result;\n'
                    f'void main(void) VX_ABI VX_RESIDENT {{vx_last_result=vx_{name}(255,1);__asm halt __endasm;}}\n')
                result=subprocess.run(['make','-j2','simulate'],cwd=project,env=project_environment(),capture_output=True,text=True,timeout=180)
                self.assertNotEqual(result.returncode,0);self.assertIn('runtime trapped with reason 9',result.stderr)

if __name__=='__main__':unittest.main(verbosity=2)
