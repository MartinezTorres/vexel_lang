#!/usr/bin/env python3
"""Immutable input snapshots, bounded process imports and project provenance."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import test_tuples as support

spec=importlib.util.spec_from_file_location('project',support.ROOT/'experimental/relational/project_msx.py')
project=importlib.util.module_from_spec(spec);spec.loader.exec_module(project)

class ResourceTests(unittest.TestCase):
    def check(self,path,expected,query='value:a',options=(),status='resolved'):
        reports=[]
        for schedule,branch in support.ORDERS:
            with self.subTest(schedule=schedule,branch=branch):
                r=support.run([support.EXE,path,'--query',query,'--schedule',schedule,'--branch-order',branch,*options])
                self.assertIn(r.returncode,(0,1),r.stderr);report=json.loads(r.stdout)
                self.assertEqual(report['status'],status,r.stdout+r.stderr)
                if expected is not None:self.assertEqual(report['queries'][0]['answer'],expected)
                reports.append(report)
        return reports

    def test_files_directories_shapes_and_relative_resolution(self):
        with tempfile.TemporaryDirectory(prefix='vexel-resources-') as tmp:
            root=Path(tmp);assets=root/'assets';assets.mkdir();(assets/'z.bin').write_bytes(b'\x00A\xff')
            (assets/'a.bin').write_bytes(b'BC');(assets/'ignored').mkdir()
            source=root/'source.vx';source.write_text('a=::assets::z.bin;')
            reports=self.check(source,'Bytes(0,65,255)')
            self.assertEqual(len(reports[0]['resource_inputs']),1)
            source.write_text('a=::assets;')
            reports=self.check(source,'(#s,#s)[2]','type:a')
            self.assertEqual(reports[0]['resource_inputs'],[])
            self.assertEqual(reports[0]['directory_inputs'][0]['files'],['a.bin','z.bin'])
            source.write_text('a={s:#u16=0;::assets@{s+=(#u16)|_.__1|;};s};')
            self.check(source,'5')
            source.write_text('a=::absent;');self.check(source,'(#s,#s)[0]','type:a');self.check(source,'Values')
            source.write_text('a=::assets::z.bin;');self.check(source,None,options=['--max-file-bytes','2'],status='exhausted')
            sub=root/'sub';sub.mkdir();(sub/'data.bin').write_bytes(b'R')
            (sub/'module.vx').write_text('&read(){::data.bin[0]}')
            source.write_text('::sub::module;a=read();');self.check(source,'82')
            (root/'data.bin').write_bytes(b'P');self.check(source,'80')

    def test_process_opt_in_binary_snapshots_and_budgets(self):
        with tempfile.TemporaryDirectory(prefix='vexel-process-inputs-') as tmp:
            root=Path(tmp);source=root/'source.vx';marker=root/'calls'
            command="printf 'A\\000B'; printf x >> calls"
            source.write_text('::'+json.dumps(command)+' -> output;a=output;')
            self.check(source,'#s','type:a');self.assertFalse(marker.exists())
            r=support.run([support.EXE,source,'--query','value:a'])
            self.assertEqual(r.returncode,2);self.assertIn('--allow-processes',r.stderr);self.assertFalse(marker.exists())
            r=support.run([support.EXE,source,'--allow-processes','--query','value:a','--then','--query','length:a'])
            self.assertEqual(r.returncode,0,r.stderr);reports=[json.loads(line) for line in r.stdout.splitlines()]
            self.assertEqual(reports[0]['queries'][0]['answer'],'Bytes(65,0,66)')
            self.assertEqual(reports[1]['queries'][1]['answer'],'3');self.assertEqual(marker.read_bytes(),b'x')
            self.assertEqual(reports[0]['process_inputs'],reports[1]['process_inputs'])
            source.write_text('a={::"printf abc" -> local;|local|};');self.check(source,'3',options=['--allow-processes'])
            source.write_text('::"printf abcd" -> a;')
            self.check(source,None,options=['--allow-processes','--max-file-bytes','3'],status='exhausted')
            source.write_text('::"while :; do :; done" -> a;')
            self.check(source,None,options=['--allow-processes','--process-timeout-ms','25'],status='exhausted')
            source.write_text('::"exit 7" -> a;')
            r=support.run([support.EXE,source,'--allow-processes','--query','value:a'])
            self.assertEqual(r.returncode,2);self.assertIn('provider-failed',r.stderr)

    def test_packaging_rejects_changed_input_facts_and_retains_metadata(self):
        with tempfile.TemporaryDirectory(prefix='vexel-resource-plan-') as tmp:
            root=Path(tmp);assets=root/'assets';assets.mkdir();data=assets/'a.bin';data.write_bytes(b'ABC')
            source=root/'source.vx';source.write_text('[[user_note(alpha)]] &^entry(n:#u8){(#u8)::assets[0].__1[n%3]}')
            generated=root/'generated';r=support.run([support.EXE,source,'--source','entry','--emit-c',generated])
            self.assertEqual(r.returncode,0,r.stderr+r.stdout);report=json.loads(r.stdout)
            self.assertTrue(any('user_note' in tag for node in report['source_metadata'] for tag in node['tags']))
            packaged=root/'project';project.package(generated,packaged,source,support.EXE)
            evidence=json.loads((packaged/'generated/provenance.json').read_text())
            self.assertIn(str(data),evidence['resource_inputs_sha256'])
            data.write_bytes(b'ABD')
            with self.assertRaisesRegex(ValueError,'resource input changed'):project.package(generated,root/'changed',source,support.EXE)
            data.write_bytes(b'ABC');(assets/'b.bin').write_bytes(b'')
            with self.assertRaisesRegex(ValueError,'directory changed'):project.package(generated,root/'changed',source,support.EXE)
            (assets/'b.bin').unlink()
            # Missing preferred paths also belong to the input revision.
            sub=root/'sub';sub.mkdir();(sub/'data.bin').write_bytes(b'R');(sub/'module.vx').write_text('&read(){::data.bin[0]}')
            source.write_text('::sub::module;&^entry(n:#u8){read()+n}')
            newer=root/'newer';r=support.run([support.EXE,source,'--source','entry','--emit-c',newer]);self.assertEqual(r.returncode,0,r.stderr)
            (root/'data.bin').write_bytes(b'P')
            with self.assertRaisesRegex(ValueError,'input selection changed'):project.package(newer,root/'changed',source,support.EXE)

    def test_packaging_rejects_retargeted_links(self):
        with tempfile.TemporaryDirectory(prefix='vexel-resource-links-') as tmp:
            root=Path(tmp);(root/'first.bin').write_bytes(b'A');(root/'second.bin').write_bytes(b'B')
            link=root/'asset.bin';link.symlink_to(root/'first.bin')
            source=root/'source.vx';source.write_text('&^entry(n:#u8){::asset.bin[0]+n}')
            generated=root/'generated';r=support.run([support.EXE,source,'--source','entry','--emit-c',generated])
            self.assertEqual(r.returncode,0,r.stderr+r.stdout)
            link.unlink();link.symlink_to(root/'second.bin')
            with self.assertRaisesRegex(ValueError,'input selection changed'):project.package(generated,root/'changed',source,support.EXE)
            # The entry source itself can also be selected through a link.
            first=root/'first.vx';first.write_text('&^entry(n:#u8){n+1}')
            second=root/'second.vx';second.write_text('&^entry(n:#u8){n+2}')
            main=root/'main.vx';main.symlink_to(first);newer=root/'newer'
            r=support.run([support.EXE,main,'--source','entry','--emit-c',newer]);self.assertEqual(r.returncode,0,r.stderr)
            main.unlink();main.symlink_to(second)
            with self.assertRaisesRegex(ValueError,'input selection changed'):project.package(newer,root/'changed',main,support.EXE)

if __name__=='__main__':unittest.main(verbosity=2)
