#!/usr/bin/env python3
"""Exercise publication using disposable repositories and local bare remotes."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).with_name('publish_repositories.py').resolve()


def git(path, *args):
    return subprocess.run(['git','-C',str(path),*args],check=True,text=True,capture_output=True).stdout.strip()


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='vexel-publish-')
        self.addCleanup(self.temp.cleanup)
        self.base=Path(self.temp.name);self.root=self.base/'vexel';self.game=self.root/'ext/edelweiss'
        self.remotes=[]
        for path in (self.root,self.game):
            path.mkdir(parents=True)
            git(path,'init','-b','main');git(path,'config','user.name','Test Author');git(path,'config','user.email','test@example.invalid')
            (path/'.gitignore').write_text('ext/\n')
            (path/'README.md').write_text('before\n')
            if path==self.game:(path/'sdcc_msx').symlink_to('../../sdcc_msx')
            git(path,'add','.');git(path,'commit','-m','Initial')
            remote=self.base/(path.name+'.git');remote.mkdir();git(remote,'init','--bare')
            self.remotes.append(remote)
            (path/'README.md').write_text('after\n')
            directory=path/('tools' if path==self.root else 'gameplay');directory.mkdir()
            (directory/'new [file].txt').write_text('new\n')
        (self.game/'sdcc_msx').unlink();(self.game/'sdcc_msx').mkdir()
        (self.game/'sdcc_msx/unrelated.txt').write_text('leave alone\n')
        (self.game/'mdl.jar').write_text('unrelated\n')

    def run_script(self,*args):
        return subprocess.run([sys.executable,str(SCRIPT),'--root',str(self.root),*args],text=True,capture_output=True)

    def arguments(self):
        return ['--vexel-url',str(self.remotes[0]),'--edelweiss-url',str(self.remotes[1])]

    def test_preview_apply_and_repeat(self):
        legacy = self.base/'legacy'
        legacy.mkdir()
        git(legacy, 'init', '-b', 'main')
        git(legacy, 'config', 'user.name', 'Legacy Author')
        git(legacy, 'config', 'user.email', 'legacy@example.invalid')
        (legacy/'README.md').write_text('Original implementation\n')
        git(legacy, 'add', 'README.md')
        git(legacy, 'commit', '-m', 'Original implementation')
        git(legacy, 'push', str(self.remotes[0]), 'main')
        legacy_head = git(legacy, 'rev-parse', 'HEAD')

        heads=[git(p,'rev-parse','HEAD') for p in (self.root,self.game)]
        result=self.run_script(*self.arguments())
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('Preview only',result.stdout)
        self.assertIn('publication branch: relational-solver', result.stdout)
        self.assertEqual(git(self.root, 'branch', '--list'), '* main')
        self.assertEqual(heads,[git(p,'rev-parse','HEAD') for p in (self.root,self.game)])
        self.assertEqual(git(self.root,'diff','--cached','--name-only'),'')
        result = self.run_script('--apply', '--vexel-branch', 'main', *self.arguments())
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('remote main has changes missing locally', result.stderr)
        self.assertEqual(heads, [git(p, 'rev-parse', 'HEAD') for p in (self.root, self.game)])

        result=self.run_script('--apply',*self.arguments());self.assertEqual(result.returncode,0,result.stderr)
        for path, remote, branch in zip((self.root, self.game), self.remotes, ('relational-solver', 'main')):
            self.assertEqual(git(path, 'branch', '--show-current'), branch)
            self.assertEqual(git(path,'rev-parse','HEAD'),git(remote,'rev-parse',f'refs/heads/{branch}'))
            self.assertEqual(git(path,'show','HEAD:README.md'),'after')
        self.assertEqual(git(self.root, 'rev-parse', 'main'), heads[0])
        self.assertEqual(git(self.remotes[0], 'rev-parse', 'refs/heads/main'), legacy_head)
        self.assertEqual(git(self.game,'show','HEAD:sdcc_msx'),'../../sdcc_msx')
        self.assertNotIn('mdl.jar',git(self.game,'ls-tree','--name-only','HEAD'))
        heads=[git(p,'rev-parse','HEAD') for p in (self.root,self.game)]
        result=self.run_script('--apply');self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(heads,[git(p,'rev-parse','HEAD') for p in (self.root,self.game)])

    def test_missing_remote_and_staged_changes_fail_before_commits(self):
        heads=[git(p,'rev-parse','HEAD') for p in (self.root,self.game)]
        result=self.run_script('--apply','--vexel-url',str(self.remotes[0]));self.assertNotEqual(result.returncode,0)
        self.assertIn('edelweiss: no remote configured', result.stderr)
        git(self.game,'add','README.md')
        result=self.run_script('--apply',*self.arguments());self.assertNotEqual(result.returncode,0)
        self.assertIn('staged changes',result.stderr)
        self.assertEqual(heads,[git(p,'rev-parse','HEAD') for p in (self.root,self.game)])
        self.assertEqual(git(self.root,'diff','--cached','--name-only'),'')

    def test_remote_ahead_stops_both_before_staging(self):
        result=self.run_script('--apply',*self.arguments());self.assertEqual(result.returncode,0,result.stderr)
        clone=self.base/'other';subprocess.run(['git','clone',str(self.remotes[1]),str(clone),'--branch','main'],check=True,capture_output=True)
        git(clone,'config','user.name','Other');git(clone,'config','user.email','other@example.invalid')
        (clone/'remote.txt').write_text('remote change\n');git(clone,'add','.');git(clone,'commit','-m','Remote change');git(clone,'push')
        (self.root/'README.md').write_text('pending\n')
        head=git(self.root,'rev-parse','HEAD')
        result=self.run_script('--apply');self.assertNotEqual(result.returncode,0)
        self.assertIn('changes missing locally',result.stderr)
        self.assertEqual(git(self.root,'rev-parse','HEAD'),head)
        self.assertEqual(git(self.root,'diff','--cached','--name-only'),'')


if __name__=='__main__':unittest.main(verbosity=2)
