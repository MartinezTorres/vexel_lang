#!/usr/bin/env python3
"""Division sharing remains a scoped, bounded physical choice."""
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support


class DivisionPolicyTests(unittest.TestCase):
    def test_division_sharing_is_a_scoped_physical_choice(self):
        with tempfile.TemporaryDirectory(prefix='vexel-division-policy-') as temporary:
            root=Path(temporary);source=root/'source.vx'
            source.write_text('[[c_binding(qr,full,1,exclusive)]] &^entry(n:#u8){(n/7,n%7)}')
            cases=[([],False,False),
                (['calculation_division_sharing=1','value_division_sharing=1'],True,True),
                (['calculation_division_sharing=1','calculation_division_sharing.vx_entry=0','value_division_sharing=1'],False,True),
                (['calculation_division_sharing=1','value_division_sharing=1','value_division_sharing.qr=0'],True,False),
                (['calculation_division_sharing.vx_entry=1','value_division_sharing.qr=1'],False,False)]
            for preferences,ordinary,binding in cases:
                run=support.run([support.EXE,source,'--program','--prefer','value_table_bytes=0',
                    *[x for preference in preferences for x in ('--prefer',preference)]])
                self.assertEqual(run.returncode,0,run.stdout+run.stderr)
                report=json.loads(run.stdout)
                fn=next(f for f in report['frame_planning']['functions'] if f['name']=='vx_entry')
                bind=report['value_bindings']['bindings'][0]
                self.assertEqual(fn['division_policy'],'calculation_division_sharing.vx_entry')
                self.assertEqual(bind['division_policy'],'value_division_sharing.qr')
                self.assertEqual((fn['division_sharing'],bool(fn['shared_divisions'])),(ordinary,ordinary))
                self.assertEqual((bind['codegen']['division_sharing'],bool(bind['codegen']['shared_divisions'])),(binding,binding))
            for preference,diagnostic in [('calculation_division_sharing.missing=1','unknown division sharing function scope'),
                                          ('value_division_sharing.missing=1','unknown division sharing binding scope')]:
                run=support.run([support.EXE,source,'--program','--prefer',preference])
                self.assertNotEqual(run.returncode,0)
                self.assertIn(diagnostic,run.stdout+run.stderr)



if __name__ == "__main__":
    unittest.main(verbosity=2)
