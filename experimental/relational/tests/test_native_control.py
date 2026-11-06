#!/usr/bin/env python3
"""Native result certificates preserve calls, state, choices and traps."""
import unittest
import test_tuples as support

class NativeControlTests(unittest.TestCase):
    check=support.TupleTests.check
    execute=support.TupleTests.execute
    def test_scalar_results(self):
        for declaration,condition in (('&~zero(k:#u8){k-k}', 'zero(n)!=0'),
                                      ('&~identity(k:#u8){k}', 'identity(n)!=n'),
                                      ('&~identity(k:#u8){k}', 'identity(n+1)-1!=n')):
            self.execute(declaration+'&^entry(n:#u8){r=n;{('+condition+')?missing();r+=1;};r}',lambda n:[(n+1)&255])
    def test_native_prefix_traps(self):
        self.execute('&~pass(k:#u8){1/k;k}&^entry(n:#u8){r=n;{pass(n)!=n?missing();r+=1;};r}',lambda n:[(n+1)&255] if n else (1,))
    def test_native_prefix_stores(self):
        self.execute('counter:#u8;&~pass(k:#u8){counter+=1;k}'
                     '&^entry(n:#u8){counter=0;r=n;{pass(n)!=n?missing();r+=1;};(r,counter)}',lambda n:[(n+1)&255,1],2)
    def test_result_leaves(self):
        self.execute('&~pair(k:#u8){(k,k+1)}&^entry(n:#u8){r=n;{pair(n).__0!=n?missing();r+=1;};r}',lambda n:[(n+1)&255])
    def test_distinct_stateful_calls_keep_distinct_results(self):
        self.execute('counter:#u8;&~advance(k:#u8){counter+=1;counter}'
                     '&^entry(n:#u8){counter=0;r=advance(n)==advance(n)?(#u8)99:n;(r,counter)}',lambda n:[n,2],2)
    def test_recursive_base_under_input_guard(self):
        self.execute('&~f(k:#u8)->#u8{k==0?(#u8)0:f(k-1)+1}'
                     '&^entry(n:#u8){r=n;n==0?{{f(n)!=0?missing();r+=1;};};r}',lambda n:[1 if n==0 else n])
    def test_lazy_initializer_keeps_native_effects(self):
        import ctypes
        import tempfile
        from pathlib import Path
        source='&!get()->#u8;&!put(n:#u8);&~zero(k:#u8){put(k);k-k}' \
               'state:#u8=zero(get())==0?7:9;&^entry(n:#u8){state}'
        for schedule,branch in support.ORDERS:
            with self.subTest(schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-native-effects-') as temp:
                root=Path(temp);path=root/'source.vx';out=root/'generated';path.write_text(source)
                r=support.run([support.EXE,path,'--source','entry','--budget','4','--emit-c',out,'--schedule',schedule,'--branch-order',branch])
                self.assertEqual(r.returncode,0,r.stdout+r.stderr)
                bindings=root/'bindings.c';bindings.write_text('#include "vexel.h"\nunsigned reads, writes, stored;\n'
                    'void vx_external_get(uint32_t *out){++reads;out[0]=37;}\n'
                    'void vx_external_put(uint32_t *out,uint8_t n){(void)out;++writes;stored=n;}\n')
                runtime=support.ROOT/'backends/msx-neo8/runtime';library=root/'run.so'
                r=support.run(['cc','-O2','-std=c11','-shared','-fPIC','-I'+str(out),'-I'+str(runtime),out/'vx_bridges.c',bindings,runtime/'vx_runtime.c','-o',library])
                self.assertEqual(r.returncode,0,r.stderr)
                dll=ctypes.CDLL(str(library));dll.vx_entry.argtypes=[ctypes.c_uint8]
                dll.vx_entry_read.argtypes=[ctypes.c_uint16];dll.vx_entry_read.restype=ctypes.c_uint32
                for n in (0,255):
                    dll.vx_entry(n);self.assertEqual(dll.vx_entry_read(0),7)
                    self.assertEqual([ctypes.c_uint.in_dll(dll,name).value for name in ('reads','writes','stored')],[1,1,37])

    def test_finite_choices_are_not_private_native_inputs(self):
        self.check('&~f(k:#u8){p=?{x=?(#u8,[0,1]);x};k+p}'
                   '&^entry(n:#u8){f(n)}',status='ambiguous',query='value:entry')

if __name__=='__main__':unittest.main(verbosity=2)
