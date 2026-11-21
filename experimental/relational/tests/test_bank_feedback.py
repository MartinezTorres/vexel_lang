#!/usr/bin/env python3
"""Local target feedback preserves the whole program and unrelated partitions."""
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

EXE = Path(sys.argv.pop(1)).resolve()
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT/'experimental/relational'))
from realize_msx import realize, measured_feedback

SOURCE = ('&~sum(n:#u8)->#u16{a:#u16=0;i:#u16=0;'
          '(i<(#u16)n)!@{i+=1;i==3?{->>;};a+=i;}a}'
          '&^entry(n:#u8){sum(n)}&^other(n:#u8){sum(n)+1}')
RUN = subprocess.run


class BankFeedbackTests(unittest.TestCase):
    def project(self, directory, name, *options):
        generated = directory/name
        result = RUN([EXE, directory/'source.vx', '--program', '--mode', 'retained',
                      '--emit-c', generated, *options], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr+result.stdout)
        return generated, json.loads(result.stdout)

    def test_scoped_partition_preserves_other_units_and_execution(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp); (d/'source.vx').write_text(SOURCE)
            base, original = self.project(d, 'base')
            owner = next(m['name'] for m in original['bank_modules'] if m['name'].startswith('vx_flow_'))
            generated, report = self.project(d, 'scoped', '--prefer', f'bank_chunk_operations.{owner}=4')
            affected = [m for m in report['bank_modules'] if m['partition_owner'] == owner]
            self.assertTrue(any(m['partition_role']=='continuation' for m in affected))
            self.assertTrue(all(m['chunk_operations']==4 for m in affected))
            for module in original['bank_modules']:
                if module['partition_owner'] != owner:
                    file = module['module']+'.c'
                    self.assertEqual((base/file).read_bytes(), (generated/file).read_bytes())
            # Scopes are ordinary configuration, including conservative merging
            # of source and CLI contributions, independent of their order.
            (d/'source.vx').write_text(f'[[prefer("bank_chunk_operations.{owner}",4)]] policy=0;'+SOURCE)
            _, merged = self.project(d, 'metadata', '--prefer', f'bank_chunk_operations.{owner}=8')
            self.assertEqual(merged['configuration']['bank_chunk_operations.'+owner]['value'], 4)
            bad = RUN([EXE, d/'source.vx', '--program', '--prefer', 'bank_chunk_operations.typo=4'],
                      capture_output=True, text=True)
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn('no emitted function: typo', bad.stderr)
            runtime = ROOT/'backends/msx-neo8/runtime'
            library = d/'run.so'
            built = RUN(['cc', '-O2', '-std=c11', '-shared', '-fPIC', '-fsanitize=undefined',
                         '-fno-sanitize-recover=all', '-I'+str(generated), '-I'+str(runtime),
                         generated/'vx_banked.c', *generated.glob('vxp_*.c'),
                         runtime/'vx_runtime.c', '-o', library], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            dll = ctypes.CDLL(str(library))
            for name in ('entry', 'other'):
                getattr(dll, 'vx_'+name).argtypes = [ctypes.c_uint8]
                getattr(dll, 'vx_'+name+'_read').restype = ctypes.c_uint32
            for n in range(256):
                dll.vx_entry(n); dll.vx_other(n)
                expected = n*(n+1)//2-(3 if n>=3 else 0)
                self.assertEqual(dll.vx_entry_read(0), expected)
                self.assertEqual(dll.vx_other_read(0), expected+1)

    def test_measurements_refine_only_rejected_owners_at_effective_limits(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp); source = d/'source.vx'; source.write_text(SOURCE)
            reports = []

            def build(command, **kwargs):
                if command[0] != 'make':
                    return RUN(command, **kwargs)
                project = Path(command[2]); target = project/'build'; target.mkdir()
                report = json.loads((project/'generated/source-plan.json').read_text())
                reports.append(report)
                if len(reports) == 1:
                    # CODE and CONST together form this bank allocation.
                    (target/'vxp_source_entry.rel').write_text('A _CODE size 2000 flags 0\nA _CONST size 1 flags 0\n')
                elif len(reports) == 2:
                    (target/'sdcc-resources.jsonl').write_text(json.dumps({
                        'command': ['sdcc', '-c', 'generated/vxp_source_other.c', '-o', 'build/vxp_source_other.rel'],
                        'reason': 'memory-limit'})+'\n')
                return subprocess.CompletedProcess(command, 2 if len(reports)<3 else 0, '', '')

            with patch('realize_msx.subprocess.run', side_effect=build):
                attempts = realize(EXE, source, d/'project', ['--program', '--prefer', 'bank_chunk_operations=16'], True)
            self.assertEqual([a['partition_limits'] for a in attempts], [{}, {'vx_entry':8}, {'vx_entry':8, 'vx_other':8}])
            self.assertEqual([a['chunk_operations'] for a in attempts], [16]*3)
            self.assertEqual(attempts[0]['feedback'][0]['bytes'], 8193)
            self.assertEqual(attempts[1]['feedback'][0]['owner'], 'vx_other')
            self.assertTrue(attempts[-1]['selected'])
            for report in reports:
                self.assertEqual(next(m for m in report['bank_modules'] if m['name'].startswith('vx_flow_'))['chunk_operations'], 16)

    def test_user_inputs_are_installed_and_revision_checked_for_every_attempt(self):
        for changed in (False, True):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as tmp:
                d = Path(tmp); source = d/'source.vx'; source.write_text(SOURCE)
                user = d/'embedding'; (user/'include').mkdir(parents=True)
                main = user/'main.c'; main.write_text('/* supplied main */\n')
                header = user/'include/binding.h'; header.write_text('#define RESPONSE 7\n')
                expected = {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (main,header)}
                builds = []

                def build(command, **kwargs):
                    if command[0] != 'make':
                        return RUN(command, **kwargs)
                    project = Path(command[2]); builds.append(project)
                    self.assertEqual((project/'user/main.c').read_bytes(), main.read_bytes())
                    self.assertEqual((project/'user/include/binding.h').read_bytes(), header.read_bytes())
                    target = project/'build'; target.mkdir()
                    if len(builds) == 1:
                        (target/'vxp_source_entry.rel').write_text('A _CODE size 2001 flags 0\n')
                        if changed:
                            header.write_text('#define RESPONSE 8\n')
                    return subprocess.CompletedProcess(command, 2 if len(builds)==1 else 0, '', '')

                with patch('realize_msx.subprocess.run', side_effect=build):
                    if changed:
                        with self.assertRaisesRegex(ValueError, 'inputs changed during realization'):
                            realize(EXE, source, d/'project', ['--program'], True, user)
                    else:
                        attempts = realize(EXE, source, d/'project', ['--program'], True, user)
                if changed:
                    self.assertEqual(len(builds), 1)
                    attempts = json.loads((d/'project/realization-failure.json').read_text())
                    self.assertNotEqual(attempts[-1]['input_identity']['user_inputs_sha256'], expected)
                    self.assertFalse((d/'project/generated').exists())
                else:
                    self.assertEqual(len(builds), 2)
                    self.assertTrue(all(a['input_identity']['user_inputs_sha256']==expected for a in attempts))
                    self.assertEqual((d/'project/user/main.c').read_bytes(), main.read_bytes())

    def test_user_inputs_cannot_include_realization_outputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            user = Path(tmp); source = user/'source.vx'; source.write_text(SOURCE)
            # Otherwise the scratch project, including its pinned executable,
            # becomes an embedding input and changes identity on every retry.
            with patch('realize_msx.subprocess.run') as run:
                for output in (user, user/'project', user/'build/project'):
                    with self.subTest(output=output), self.assertRaisesRegex(
                            ValueError, 'outside the user directory'):
                        realize(EXE, source, output, ['--program'], True, user)
                run.assert_not_called()
            self.assertEqual(list(user.iterdir()), [source])

    def test_terminal_failures_do_not_trigger_global_repartition(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp); (d/'build').mkdir()
            report = {'configuration': {'rom_bank_bytes': {'value':8192}}, 'bank_modules': [
                {'name':'original', 'module':'anything', 'partition_owner':'original',
                 'partition_role':'continuation', 'chunk_operations':8}]}
            # Ownership is explicit; module spelling is not a function parser.
            resources = [{'command':['sdcc','-o','build/anything.rel'], 'reason':'timeout'}]
            for reason in ('timeout', 'cpu-limit', 'memory-limit'):
                resources[0]['reason'] = reason
                feedback, _, updates = measured_feedback(report, d, resources)
                self.assertEqual(updates, {'original':4})
                self.assertEqual(feedback[0]['reason'], reason)
            for role, limit in (('dispatcher',8), ('continuation',1)):
                report['bank_modules'][0].update(partition_role=role, chunk_operations=limit)
                feedback, _, updates = measured_feedback(report, d, resources)
                self.assertFalse(updates)
                self.assertIn('blocked', feedback[0])
            resources[0]['command'][-1] = 'build/vx_runtime.rel'
            feedback, _, updates = measured_feedback(report, d, resources)
            self.assertFalse(updates)
            self.assertIn('outside', feedback[0]['blocked'])

    def test_input_edits_invalidate_partition_evidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp); source = d/'source.vx'; source.write_text(SOURCE)
            builds = []

            def build(command, **kwargs):
                if command[0] != 'make':
                    return RUN(command, **kwargs)
                builds.append(command)
                target = Path(command[2])/'build'; target.mkdir()
                (target/'vxp_source_entry.rel').write_text('A _CODE size 2001 flags 0\n')
                source.write_text(SOURCE+'\n// edited during compilation\n')
                return subprocess.CompletedProcess(command, 2, '', '')

            with patch('realize_msx.subprocess.run', side_effect=build):
                with self.assertRaisesRegex(ValueError, 'inputs changed during realization'):
                    realize(EXE, source, d/'project', ['--program'], True)
            self.assertEqual(len(builds), 1)
            evidence = json.loads((d/'project/realization-failure.json').read_text())
            self.assertEqual(evidence[-1]['rejected'], 'inputs changed during realization')
            self.assertFalse((d/'project/generated').exists())


if __name__ == '__main__':
    unittest.main(verbosity=2)
