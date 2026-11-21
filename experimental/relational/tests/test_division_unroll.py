#!/usr/bin/env python3
"""A measured bank rejection can choose compact, equivalent arithmetic."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('solver', type=Path)
parser.add_argument('--msx', action='store_true')
parser.add_argument('--output', type=Path)
options, remaining = parser.parse_known_args()
sys.argv = [sys.argv[0], *remaining]
EXE = options.solver.resolve()
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT/'experimental/relational'))
from realize_msx import realize
RUN = subprocess.run


class DivisionUnrollTests(unittest.TestCase):
    def test_global_and_scoped_choices_preserve_power_of_two_realization(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory/'source.vx'
            source.write_text('[[c_binding(qr,full,1,exclusive)]] '
                              '&^entry(n:#u32){(n/7,n%7,n/8,n%8)}')
            cases = [([], True, True),
                     (['calculation_division_unroll=0', 'value_division_unroll=0'], False, False),
                     (['calculation_division_unroll.vx_entry=0'], False, True),
                     (['value_division_unroll.qr=0'], True, False),
                     (['calculation_division_unroll=0', 'calculation_division_unroll.vx_entry=1'], False, True)]
            for index, (preferences, ordinary, binding) in enumerate(cases):
                generated = directory/str(index)
                command = [EXE, source, '--program', '--emit-c', generated, '--prefer', 'value_table_bytes=0']
                command += [x for p in preferences for x in ('--prefer', p)]
                result = RUN(list(map(str, command)), capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stderr)
                report = json.loads(result.stdout)
                fn = next(f for f in report['frame_planning']['functions'] if f['name']=='vx_entry')
                bound = report['value_bindings']['bindings'][0]
                self.assertEqual(fn['division_unroll'], ordinary)
                self.assertEqual(fn['unrolled_division_helpers'], 2 if ordinary else 0)
                self.assertEqual(bound['codegen']['division_unroll'], binding)
                self.assertEqual(bound['division_unroll_policy'], 'value_division_unroll.qr')
                for filename, unrolled in [('vx_bridges.c', ordinary), ('vx_bind_qr.h', binding)]:
                    code = (generated/filename).read_text()
                    self.assertEqual('vx_value_divu32_p7' in code, unrolled)
                    self.assertEqual('vx_value_divu32_runtime' in code, not unrolled)
                    self.assertNotIn('vx_value_divu32_p8', code)
            for scope in ('calculation_division_unroll.missing', 'value_division_unroll.missing'):
                result = RUN([str(EXE), str(source), '--program', '--prefer', scope+'=0'], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('unknown division unroll', result.stderr)

    def test_measured_rejection_changes_only_its_owner_before_repartitioning(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary); source = directory/'source.vx'
            source.write_text('&^entry(n:#u32){n/3}&^other(n:#u32){n/7}')
            reports, unrelated = [], []

            def build(command, **kwargs):
                if command[0] != 'make':
                    return RUN(command, **kwargs)
                project = Path(command[2]); target = project/'build'; target.mkdir()
                reports.append(json.loads((project/'generated/source-plan.json').read_text()))
                unrelated.append((project/'generated/vxp_source_other.c').read_bytes())
                if len(reports)==1:
                    (target/'vxp_source_entry.rel').write_text('A _CODE size 2001 flags 0\n')
                return subprocess.CompletedProcess(command, 2 if len(reports)==1 else 0, '', '')

            with patch('realize_msx.subprocess.run', side_effect=build):
                attempts = realize(EXE, source, directory/'project', ['--program'], True)
            self.assertEqual(len(attempts), 2)
            self.assertEqual([a['partition_limits'] for a in attempts], [{}, {}])
            self.assertEqual(attempts[-1]['realization_preferences'], {'calculation_division_unroll.vx_entry': 0})
            self.assertEqual(unrelated[0], unrelated[1])
            functions = {f['name']: f for f in reports[-1]['frame_planning']['functions']}
            self.assertFalse(functions['vx_entry']['division_unroll'])
            self.assertTrue(functions['vx_other']['division_unroll'])
            self.assertTrue(attempts[-1]['selected'])

    @unittest.skipUnless(options.msx, 'optional SDCC and Z80 realization')
    def test_large_expression_retries_compact_and_executes_independent_oracle(self):
        context = (tempfile.TemporaryDirectory(prefix='vexel-division-bank-') if options.output is None else None)
        if options.output is not None:
            options.output.mkdir(parents=True, exist_ok=True)
            directory = Path(tempfile.mkdtemp(prefix='run-', dir=options.output)).resolve()
        else:
            directory = Path(context.name)
        try:
            divisors = list(range(3, 67, 2))
            source = directory/'source.vx'
            source.write_text('&^entry(n:#u32){'+'+'.join('(n/'+str(d)+')' for d in divisors)+'}')
            user = directory/'embedding'; user.mkdir()
            values = [0, 1, 63, 65535, 0x80000000, 0xffffffff]
            rows = ','.join('{'+str(n)+'UL,'+str(sum(n//d for d in divisors)&0xffffffff)+'UL}' for n in values)
            (user/'main.c').write_text('#include "vexel.h"\n'
                'static const uint32_t data[][2]={'+rows+'};\nvolatile uint32_t vx_last_result;\n'
                'void main(void) VX_ABI VX_RESIDENT {uint8_t i;vx_last_result=0;'
                'for(i=0;i<6;++i){vx_entry(data[i][0]);if(vx_entry_read(0)!=data[i][1]){vx_last_result=i+1;break;}}'
                '__asm halt __endasm;}\n')
            project = directory/'project'
            command = ['python3', ROOT/'experimental/relational/realize_msx.py', source, project,
                       '--solver', EXE, '--build', '--user-directory', user, '--program']
            result = RUN(list(map(str, command)), capture_output=True, text=True, timeout=1800)
            (directory/'realize.log').write_text(result.stdout+result.stderr)
            self.assertEqual(result.returncode, 0, result.stderr)
            attempts = json.loads((project/'realization.json').read_text())
            self.assertEqual(len(attempts), 2)
            self.assertTrue(attempts[0]['oversized_modules'])
            self.assertEqual(attempts[-1]['realization_preferences'], {'calculation_division_unroll.vx_entry': 0})
            result = RUN(['make', '-C', str(project), '-j1', 'simulate', 'SIMFLAGS=--max-ticks=400000000'],
                         capture_output=True, text=True, timeout=1800)
            (directory/'simulate.log').write_text(result.stdout+result.stderr)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            observation = json.loads((project/'build/execution.json').read_text())['observation']
            self.assertEqual((observation['fault'], observation['last_result']), (0, 0), observation)
        finally:
            if context is not None:
                context.cleanup()


if __name__ == '__main__':
    unittest.main(verbosity=2)
