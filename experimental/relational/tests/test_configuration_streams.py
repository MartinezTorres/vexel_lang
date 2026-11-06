#!/usr/bin/env python3
"""Configuration equations and dynamic result ownership, with a C oracle."""
import ctypes
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXE = Path(sys.argv.pop(1)).resolve()
ROOT = Path(__file__).resolve().parents[3]


def run(args):
    return subprocess.run([str(x) for x in args], capture_output=True, text=True, timeout=120)


class ClosureTests(unittest.TestCase):
    def test_configuration_is_order_independent_source_data(self):
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            (d/'policy.vx').write_text('#Limits(bytes:#u16); [[require(payload_bytes, limits.bytes)]] limits=#Limits(4);')
            declarations = ['::policy;', '[[source]] &^entry([[range(1,limits.bytes+2)]] n:#u8){n+1}']
            for order in (declarations, declarations[::-1]):
                (d/'main.vx').write_text(''.join(order))
                for schedule in ('fifo', 'lifo', 'random'):
                    result = run([EXE, d/'main.vx', '--schedule', schedule])
                    self.assertEqual(result.returncode, 0, result.stderr+result.stdout)
                    plan = json.loads(result.stdout)
                    self.assertEqual(plan['payload_budget'], 4)
                    self.assertEqual(plan['sources'][0]['parameters'][0]['max'], 6)
                    result = run([EXE, d/'main.vx', '--budget', 5])
                    self.assertEqual(json.loads(result.stdout)['status'], 'contradictory')
                    result = run([EXE, d/'main.vx', '--budget', 4])
                    self.assertEqual(result.returncode, 0, result.stderr+result.stdout)

    def test_unknown_requirements_and_invalid_hardware_facts_fail(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp)/'main.vx'
            for tag, status in [('require(paylod_bytes,4)', 'error'), ('fact(ram_bytes,32768)', 'contradictory'),
                                ('require(stack_bytes,"large")', 'contradictory')]:
                source.write_text('[['+tag+']] ^answer=3;')
                result = run([EXE, source])
                self.assertNotEqual(result.returncode, 0)
                if status == 'error': self.assertIn('unknown configuration', result.stderr)
                else: self.assertEqual(json.loads(result.stdout)['status'], status)

    def test_backend_requirement_refines_a_source_choice(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'main.vx'
            path.write_text('[[require(payload_bytes, capacity)]] capacity=?{?(#u16,[1,2])};'
                            '[[source]] &^entry(n:#u16){n}')
            for schedule in ('fifo','lifo','random'):
                result=run([EXE,path,'--query','value:capacity','--schedule',schedule])
                self.assertEqual(result.returncode,0,result.stderr+result.stdout)
                report=json.loads(result.stdout)
                self.assertEqual(report['queries'][0]['answer'],'2')
                self.assertEqual(report['extent'],2)
            # Address bindings and pool counts use the same ordinary values.
            path.write_text('base=128; [[msx_port(base+1)]] !!port:#u8; slots=1+1;'
                            '[[stream(slots)]] &^entry(n:#u8){port=n;n}')
            result=run([EXE,path]);self.assertEqual(result.returncode,0,result.stderr+result.stdout)
            report=json.loads(result.stdout)
            self.assertEqual(report['sources'][0]['stream']['slots'],2)
            self.assertEqual(report['boundary_storage'][0]['binding']['address'],129)

    def test_pool_backpressure_transfer_and_stale_lease(self):
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp); source = d/'main.vx'; generated = d/'generated'
            source.write_text('[[reads(device)]] &!sample()->#u8;'
                              '[[stream(3)]] &^entry(n:#u8){(n+1,sample())}'
                              '[[stream(1)]] &^other(n:#u8){n}')
            result = run([EXE, source, '--emit-c', generated])
            self.assertEqual(result.returncode, 0, result.stderr+result.stdout)
            plan = json.loads(result.stdout)
            self.assertEqual(plan['activation'], 'stream')
            self.assertTrue(plan['activation_graph']['execution_exclusive'])
            runtime = ROOT/'backends/msx-neo8/runtime'
            (d/'runtime.c').write_text((runtime/'vx_runtime.c').read_text().replace('void vx_trap(uint8_t reason)', 'void unused_trap(uint8_t reason)', 1))
            (d/'harness.c').write_text('#include "vexel.h"\n'
                'unsigned rejected; uint8_t fault; void vx_trap(uint8_t r){fault=r;}\n'
                'void vx_external_sample(uint32_t *out){rejected += vx_other_submit(9)==0; out[0]=23;}\n')
            built = run(['cc', '-std=c11', '-O2', '-shared', '-fPIC', '-I'+str(generated), '-I'+str(runtime),
                         generated/'vx_banked.c', *generated.glob('vxp_*.c'), d/'runtime.c', d/'harness.c', '-o', d/'run.so'])
            self.assertEqual(built.returncode, 0, built.stderr)
            dll = ctypes.CDLL(str(d/'run.so'))
            for name in ('entry', 'other'):
                submit = getattr(dll, 'vx_'+name+'_submit'); submit.argtypes = [ctypes.c_uint8]; submit.restype = ctypes.c_uint32
                read = getattr(dll, 'vx_'+name+'_read_lease'); read.argtypes = [ctypes.c_uint32, ctypes.c_uint16]; read.restype = ctypes.c_uint32
                release = getattr(dll, 'vx_'+name+'_release'); release.argtypes = [ctypes.c_uint32]; release.restype = ctypes.c_uint8
            leases = [dll.vx_entry_submit(n) for n in (7, 8, 9)]
            self.assertTrue(all(leases)); self.assertEqual(dll.vx_entry_submit(10), 0)
            for lease, value in zip(leases, (8, 9, 10)):
                self.assertEqual([dll.vx_entry_read_lease(lease, i) for i in range(2)], [value, 23])
            self.assertEqual(dll.vx_other_release(leases[0]), 0)
            self.assertEqual(dll.vx_entry_release(leases[0]), 1)
            self.assertEqual(dll.vx_entry_release(leases[0]), 0)
            replacement = dll.vx_entry_submit(11)
            self.assertNotIn(replacement, [0, *leases])
            self.assertEqual(dll.vx_entry_read_lease(replacement, 0), 12)
            dll.vx_entry_read_lease(leases[0], 0)
            self.assertEqual(ctypes.c_uint8.in_dll(dll, 'fault').value, 21)
            self.assertEqual(ctypes.c_uint.in_dll(dll, 'rejected').value, 4)
            # Recycling a slot changes its token, never a retained result.
            for n in range(40):
                self.assertEqual(dll.vx_entry_release(replacement), 1)
                replacement = dll.vx_entry_submit(n)
                self.assertEqual(dll.vx_entry_read_lease(leases[1], 0), 9)


if __name__ == '__main__':
    unittest.main(verbosity=2)
