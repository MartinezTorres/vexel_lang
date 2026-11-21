#!/usr/bin/env python3
"""Algebraic equality removes transport at a proved return or loop clock."""
import ctypes
from pathlib import Path
import tempfile
import unittest
import test_result_demand as support


class ModularTransportTests(unittest.TestCase):
    project=support.ResultDemandTests.project
    execute=support.ResultDemandTests.execute
    simulate=support.ResultDemandTests.simulate

    def test_immutable_aliases_cross_calls_and_loop_induction(self):
        source=('&!observe(n:#u8);&~identity(n:#u8)->#u8{observe(n);(n+37)-37}'
                '&~walk(n:#u8)->#u8{x:#u8=n;i:#u8=0;(i<3)!@{x=identity(x);i+=1;};x}'
                '&^entry(n:#u8){walk(n)}')
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp);generated,report=self.project(d,source)
            self.assertTrue(all(fn['transported_result_slots']==0 for fn in report['runtime_functions']))
            self.assertEqual(report['interface_demand']['loops'][0]['retained_carried_leaves'],1)
            dll=self.execute(d,generated,report,lambda n:n,
                'unsigned calls;void vx_external_observe(uint32_t *out,uint8_t n){(void)out;(void)n;++calls;}\n',split=True)
            self.assertEqual(ctypes.c_uint.in_dll(dll,'calls').value,3*256)

    def test_alias_reduction_keeps_narrowing_and_prefix_traps(self):
        cases=[('&~identity(n:#u8)->#u8{1/n;(n+200)-200}',lambda n:n if n else (1,),0),
               ('&~identity(n:#u8)->#u16{(#u16)(n+1)-1}',lambda n:n if n!=255 else 65535,2),
               ('&~identity(n:#u8)->#u8{(#u8)(((#u16)n+1000)-1000)}',lambda n:n,0)]
        for declaration,oracle,bytes_ in cases:
            with self.subTest(declaration=declaration),tempfile.TemporaryDirectory() as temp:
                d=Path(temp);generated,report=self.project(d,declaration+'&^entry(n:#u8){identity(n)}')
                self.assertEqual(report['runtime_functions'][0]['transported_result_bytes'],bytes_)
                self.execute(d,generated,report,oracle)

    def test_commutative_returns_keep_the_call_effect(self):
        source=('&!observe(n:#u8);&~pair(n:#u8){observe(n);(n+3,3+n)}'
                '&^entry(n:#u8){a,b=pair(n);(#u16)a|((#u16)b<<8)}')
        for options in ((),('--schedule','lifo','--branch-order','reverse')):
            with self.subTest(options=options),tempfile.TemporaryDirectory() as temp:
                d=Path(temp);generated,report=self.project(d,source,*options)
                self.assertEqual(report['runtime_functions'][0]['transported_result_bytes'],1)
                dll=self.execute(d,generated,report,lambda n:((n+3)&255)*257,
                    'unsigned calls;void vx_external_observe(uint32_t *out,uint8_t n){(void)out;(void)n;++calls;}\n',split=True)
                self.assertEqual(ctypes.c_uint.in_dll(dll,'calls').value,256)

    def test_distributed_word_results_share_one_slot(self):
        source=('&~pair(n:#u16){((n+1)*(n+2),n*n+3*n+2)}'
                '&^entry(n:#u8){a,b=pair((#u16)n);(#u32)a|((#u32)b<<16)}')
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp);generated,report=self.project(d,source)
            self.assertEqual(report['runtime_functions'][0]['transported_result_bytes'],2)
            self.execute(d,generated,report,lambda n:(((n+1)*(n+2))&65535)*65537,split=True)
            if support.MSX:self.simulate(d,generated,
                expected='((uint32_t)square|((uint32_t)square<<16))',
                setup='uint16_t square=2,delta=4;',
                advance='square=(uint16_t)(square+delta);delta=(uint16_t)(delta+2);')

    def test_equal_loop_transfers_have_an_entry_anchor(self):
        source=('&^entry(n:#u8){x:#u8=n;y:#u8=n;i:#u8=0;'
                '(i<3)!@{x=(x+2)+1;y=1+(y+2);i+=1;};'
                '(#u16)x|((#u16)y<<8)}')
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp);generated,report=self.project(d,source)
            self.assertEqual(report['interface_demand']['loops'][0]['coalesced_carried_leaves'],1)
            self.execute(d,generated,report,lambda n:((n+9)&255)*257)
            if support.MSX:self.simulate(d,generated,expected='(((n+9)&255)*257U)')

    def test_narrower_wrap_does_not_join_a_wider_transfer(self):
        source=('&^entry(n:#u8){x:#u16=(#u16)n;y:#u16=(#u16)n;i:#u8=0;'
                '(i<3)!@{x=(#u16)((#u8)x+1);y+=1;i+=1;};'
                '(#u32)x|((#u32)y<<16)}')
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp);generated,report=self.project(d,source)
            self.assertEqual(report['interface_demand']['loops'][0]['coalesced_carried_leaves'],0)
            self.execute(d,generated,report,lambda n:((n+3)&255)|((n+3)<<16))
            if support.MSX:self.simulate(d,generated,expected='(((n+3)&255)|((uint32_t)(n+3)<<16))')

    def test_distinct_reads_are_distinct_atoms(self):
        source=('&!read(n:#u8)->#u8;&~pair(n:#u8){a=read(n);b=read(n);(a+3,3+b)}'
                '&^entry(n:#u8){a,b=pair(n);(#u16)a|((#u16)b<<8)}')
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp);generated,report=self.project(d,source)
            self.assertEqual(report['runtime_functions'][0]['transported_result_bytes'],2)
            dll=self.execute(d,generated,report,lambda n:((n+3)&255)|(((n+4)&255)<<8),
                'unsigned calls;void vx_external_read(uint32_t *out,uint8_t n){out[0]=(uint8_t)(n+(calls++&1));}\n')
            self.assertEqual(ctypes.c_uint.in_dll(dll,'calls').value,512)

    def test_equal_results_do_not_discharge_prefix_traps_or_recursion(self):
        cases=[('&~pair(n:#u8){1/n;(n+3,3+n)}',lambda n:((n+3)&255)*257 if n else (1,),''),
               ('&!pulse();&~pair(n:#u8)->(#u8,#u8){pulse();pair(n);(n+3,3+n)}',lambda n:(73,),
                'unsigned calls;void vx_external_pulse(uint32_t *out){(void)out;if((++calls&3)==0)vx_trap(73);}\n')]
        for declaration,oracle,binding in cases:
            with self.subTest(declaration=declaration),tempfile.TemporaryDirectory() as temp:
                d=Path(temp);generated,report=self.project(d,declaration+
                    '&^entry(n:#u8){a,b=pair(n);(#u16)a|((#u16)b<<8)}')
                self.assertEqual(report['runtime_functions'][0]['transported_result_bytes'],1)
                self.execute(d,generated,report,oracle,binding)

if __name__=='__main__':unittest.main(verbosity=2)
