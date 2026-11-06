#!/usr/bin/env python3
"""Independent physical preferences preserve joint semantic observations."""
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support

class OutputEncodingTests(unittest.TestCase):
    def report(self,path,names,budget,schedule,branch,mode='serial'):
        args=[support.EXE,path,'--budget',budget,'--mode',mode,'--schedule',schedule,
              '--branch-order',branch,'--max-work','1000000']
        for name in names:args.extend(['--source',name])
        result=support.run(args);self.assertIn(result.returncode,(0,1),result.stdout+result.stderr)
        return json.loads(result.stdout)

    def test_preference_constants_signed_and_shared_lifetimes(self):
        cases=[
            ('&^entry(n:#u8){100000}',0,[(16,0)]),
            ('&^entry(n:#u8){n;}',0,[(16,0)]),
            ('&^entry(n:#u8){n}',1,[(8,1)]),
            ('&^entry(n:#u8){n}',2,[(16,2)]),
            ('&^entry(n:#u8){(#u32)n+100000}',4,[(32,4)]),
            ('&^entry(n:#u8){(#i8)n}',1,[(8,1)]),
            ('&^entry(n:#u8){((#i8)n,n,(#u32)100000)}',2,[(8,2)]),
            ('&^entry(n:#u8){((#i8)n,n,(#u32)100000)}',4,[(16,4)]),
        ]
        for source,budget,expected in cases:
            for schedule,branch in support.ORDERS:
                with self.subTest(source=source,budget=budget,schedule=schedule,branch=branch),tempfile.TemporaryDirectory(prefix='vexel-encoding-') as temp:
                    path=Path(temp)/'source.vx';path.write_text(source)
                    report=self.report(path,['entry'],budget,schedule,branch)
                    self.assertEqual(report['status'],'resolved',report)
                    self.assertEqual([(s['element_bits'],s['bytes']) for s in report['sources']],expected)
        for schedule,branch in support.ORDERS:
            with tempfile.TemporaryDirectory(prefix='vexel-encoding-overlap-') as temp:
                path=Path(temp)/'source.vx';path.write_text('&^a(n:#u8){n}&^b(n:#u8){n+1}')
                report=self.report(path,['a','b'],3,schedule,branch,'retained')
                self.assertEqual(report['status'],'resolved',report)
                self.assertEqual([(s['element_bits'],s['bytes'],s['offset']) for s in report['sources']],[(16,2,0),(8,1,2)])

    def test_backend_facts_remain_joint_and_do_not_hide_ambiguity(self):
        source='&^entry(n:#u8){?{flag=?(#b,[0,1]);flag?((#u32)n+100000):(#u8)n}}'
        for schedule,branch in support.ORDERS:
            with tempfile.TemporaryDirectory(prefix='vexel-encoding-joint-') as temp:
                path=Path(temp)/'source.vx';path.write_text(source)
                constrained=self.report(path,['entry'],1,schedule,branch)
                self.assertEqual(constrained['status'],'resolved',constrained)
                self.assertEqual(constrained['sources'][0]['logical_type'],'#u8')
                self.assertEqual(constrained['sources'][0]['element_bits'],8)
                roomy=self.report(path,['entry'],4,schedule,branch)
                self.assertEqual(roomy['status'],'ambiguous',roomy)

    def test_eight_sources_avoid_encoding_cartesian_product(self):
        names=['entry'+str(i) for i in range(8)]
        with tempfile.TemporaryDirectory(prefix='vexel-many-encodings-') as temp:
            source=Path(temp)/'source.vx'
            source.write_text(''.join('&^'+name+'(n:#u8){(n,n+'+str(i)+')}'
                                      for i,name in enumerate(names)))
            for schedule,branch in support.ORDERS:
                with self.subTest(schedule=schedule,branch=branch):
                    report=self.report(source,names,4,schedule,branch)
                    self.assertEqual(report['status'],'resolved',report)
                    self.assertEqual([s['element_bits'] for s in report['sources']],[16]*8)
                    self.assertEqual(report['extent'],4)

if __name__=='__main__':unittest.main(verbosity=2)
