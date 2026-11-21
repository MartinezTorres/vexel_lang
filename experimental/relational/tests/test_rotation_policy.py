#!/usr/bin/env python3
"""Rotations preserve semantics while helper calls remain an explicit choice."""
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support


class RotationPolicyTests(unittest.TestCase):
    def test_scoped_choice_and_owned_support(self):
        with tempfile.TemporaryDirectory(prefix='vexel-rotation-policy-') as temporary:
            root=Path(temporary);source=root/'source.vx'
            source.write_text('[[c_binding(turn,full,1,exclusive)]] &^entry(n:#u8){(n>>2)|(n<<6)}')
            cases=[([],False,False),
                (['calculation_rotation_helpers=1','value_rotation_helpers=1'],True,True),
                (['calculation_rotation_helpers=1','calculation_rotation_helpers.vx_entry=0','value_rotation_helpers=1'],False,True),
                (['calculation_rotation_helpers=1','value_rotation_helpers=1','value_rotation_helpers.turn=0'],True,False),
                (['calculation_rotation_helpers.vx_entry=1','value_rotation_helpers.turn=1'],False,False)]
            for index,(preferences,ordinary,binding) in enumerate(cases):
                generated=root/str(index)
                run=support.run([support.EXE,source,'--program','--prefer','value_table_bytes=0',
                    '--prefer','value_choice_branches=0','--emit-c',generated,
                    *[x for preference in preferences for x in ('--prefer',preference)]])
                self.assertEqual(run.returncode,0,run.stdout+run.stderr)
                report=json.loads(run.stdout)
                fn=next(f for f in report['frame_planning']['functions'] if f['name']=='vx_entry')
                bind=report['value_bindings']['bindings'][0]
                self.assertEqual(fn['rotation_policy'],'calculation_rotation_helpers.vx_entry')
                self.assertEqual(bind['rotation_policy'],'value_rotation_helpers.turn')
                self.assertEqual(fn['rotation_helpers'],ordinary)
                self.assertEqual(bind['codegen']['rotation_helpers'],binding)
                self.assertEqual('vx_value_ror8_2' in (generated/'vx_bridges.c').read_text(),ordinary)
                unit=next(u for u in report['program_image']['units'] if u['symbol']=='vx_entry')
                if ordinary:
                    self.assertIn('vx_value_ror8_2',(generated/unit['support']).read_text())
            for preference,diagnostic in [('calculation_rotation_helpers.missing=1','unknown rotation helper function scope'),
                                          ('value_rotation_helpers.missing=1','unknown rotation helper binding scope')]:
                run=support.run([support.EXE,source,'--program','--prefer',preference])
                self.assertNotEqual(run.returncode,0)
                self.assertIn(diagnostic,run.stdout+run.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
