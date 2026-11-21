#!/usr/bin/env python3
"""Preview, then optionally commit and push Vexel and ext/edelweiss.

No Git mutation occurs without --apply. Both remote histories are checked
before staging; pushes are ordinary fast-forward pushes, never forced.
"""
import argparse
from dataclasses import dataclass
from pathlib import Path
import subprocess
import sys


VEXEL_BRANCH = 'relational-solver'
VEXEL_URL = 'git@github.com:MartinezTorres/vexel_lang.git'


class Failure(RuntimeError):
    pass


@dataclass
class Repository:
    path: Path
    name: str
    url: str | None
    remote: str
    message: str
    target_branch: str | None = None
    fallback_url: str = ''
    branch: str = ''
    destination: str = ''
    paths: tuple[str, ...] = ()

    def git(self, *args, check=True, data=None):
        command = ['git', '--literal-pathspecs', '-c', f'safe.directory={self.path}', '-C', str(self.path), *args]
        result = subprocess.run(command, input=data, capture_output=True, text=True)
        if check and result.returncode:
            raise Failure(f'{self.name}: git {args[0]} failed:\n{result.stderr.strip()}')
        return result

    def inspect(self):
        if self.git('rev-parse', '--show-toplevel').stdout.strip() != str(self.path):
            raise Failure(f'{self.path} must be a separate repository')
        self.branch = self.git('symbolic-ref', '--quiet', '--short', 'HEAD', check=False).stdout.strip()
        if not self.branch:
            raise Failure(f'{self.name}: detached HEAD; select a branch first')
        self.target_branch = self.target_branch or self.branch
        reference = self.git('check-ref-format', 'refs/heads/' + self.target_branch, check=False)
        if reference.returncode or self.target_branch.startswith('-'):
            raise Failure(f'{self.name}: invalid branch name {self.target_branch!r}')
        if self.target_branch != self.branch:
            local = self.git('show-ref', '--verify', '--quiet', f'refs/heads/{self.target_branch}', check=False)
            if local.returncode == 0:
                raise Failure(f'{self.name}: local branch {self.target_branch} already exists; switch to it before publishing')
        for marker in ('MERGE_HEAD', 'CHERRY_PICK_HEAD', 'REVERT_HEAD', 'rebase-merge', 'rebase-apply'):
            location = self.git('rev-parse', '--git-path', marker).stdout.strip()
            if (self.path / location).exists():
                raise Failure(f'{self.name}: finish the {marker} operation first')
        if self.git('diff', '--cached', '--quiet', check=False).returncode:
            raise Failure(f'{self.name}: the index already has staged changes; commit or unstage them first')
        configured = self.git('remote', 'get-url', '--push', self.remote, check=False).stdout.strip()
        if self.url and configured and self.url != configured:
            raise Failure(f'{self.name}: supplied URL differs from {self.remote}; choose a different remote name')
        self.destination = configured or self.url or self.fallback_url
        tracked = self.git('diff', '--name-only', '-z', 'HEAD').stdout.split('\0')
        untracked = self.git('ls-files', '--others', '--exclude-standard', '-z').stdout.split('\0')
        allowed = {'backends', 'docs', 'examples', 'experimental', 'include', 'src', 'lib', 'tools', 'tests'} if self.name == 'vexel' else {'gameplay', 'generated', 'src', 'tests', 'tools', 'docs'}
        selected = {p for p in tracked if p}
        selected.update(p for p in untracked if p and p.split('/')[0] in allowed)
        # Some development environments mount a directory over this tracked
        # historical symlink. Do not turn that into a vendor-tree replacement.
        excluded = []
        alias = self.path / 'sdcc_msx'
        if self.name == 'edelweiss' and alias.is_dir() and not alias.is_symlink():
            selected.discard('sdcc_msx')
            excluded.append('sdcc_msx (preserve the tracked symlink)')
        excluded.extend(p for p in untracked if p and p not in selected)
        self.paths = tuple(sorted(selected))
        destination = self.destination or f'MISSING (supply --{self.name}-url)'
        print(f'\n{self.name}: {self.path}\n  current branch: {self.branch}\n'
              f'  publication branch: {self.target_branch}\n  destination: {destination}\n'
              f'  commit: {self.message}\n  selected paths: {len(self.paths)}')
        for path in self.paths:
            print(f'    {path}')
        if excluded:
            print('  excluded:')
            for path in excluded:
                print(f'    {path}')

    def preflight_remote(self):
        if not self.destination:
            raise Failure(f'{self.name}: no remote configured; supply --{self.name}-url URL')
        # Fetch only the destination branch, without changing the working tree
        # or configuring a remote. An absent branch is a permitted first push.
        reference = f'refs/heads/{self.target_branch}'
        listed = self.git('ls-remote', '--heads', self.destination, reference).stdout.strip()
        if listed:
            self.git('fetch', '--no-tags', self.destination, reference)
            if self.git('merge-base', '--is-ancestor', 'FETCH_HEAD', 'HEAD', check=False).returncode:
                raise Failure(f'{self.name}: remote {self.target_branch} has changes missing locally; choose another branch or integrate them and rerun')

    def commit(self):
        if self.target_branch != self.branch:
            self.git('switch', '-c', self.target_branch)
        if not self.paths:
            print(f'{self.name}: no selected changes to commit')
            return
        self.git('add', '-A', '--pathspec-from-file=-', '--pathspec-file-nul',
                 data='\0'.join(self.paths) + '\0')
        if not self.git('diff', '--cached', '--quiet', check=False).returncode:
            return
        identity = []
        for key, fallback in (('user.name', 'Manuel Martinez'), ('user.email', 'salutte@gmail.com')):
            if not self.git('config', '--get', key, check=False).stdout.strip():
                identity += ['-c', f'{key}={fallback}']
        result = self.git(*identity, 'commit', '-m', self.message)
        print(result.stdout.strip())

    def push(self):
        if not self.git('remote', 'get-url', self.remote, check=False).returncode:
            destination = self.remote
        else:
            self.git('remote', 'add', self.remote, self.destination)
            destination = self.remote
        result = self.git('push', '--no-follow-tags', '--set-upstream', destination, f'HEAD:refs/heads/{self.target_branch}')
        print(result.stdout.strip() or result.stderr.strip())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--apply', action='store_true', help='perform the reviewed commits and pushes')
    parser.add_argument('--vexel-url', help='remote URL if Vexel has no configured remote')
    parser.add_argument('--edelweiss-url', help='remote URL if Edelweiss has no configured remote')
    parser.add_argument('--vexel-remote', default='origin')
    parser.add_argument('--edelweiss-remote', default='origin')
    parser.add_argument('--vexel-branch', default=VEXEL_BRANCH,
                        help=f'local and remote branch for Vexel (default: {VEXEL_BRANCH})')
    parser.add_argument('--vexel-message', default='Unify calculation scheduling and whole-program optimization')
    parser.add_argument('--edelweiss-message', default='Migrate gameplay to Vexel and refresh optimized generated code')
    args = parser.parse_args()
    root = args.root.resolve()
    repositories = [Repository(root, 'vexel', args.vexel_url, args.vexel_remote, args.vexel_message,
                               target_branch=args.vexel_branch, fallback_url=VEXEL_URL),
                    Repository(root/'ext/edelweiss', 'edelweiss', args.edelweiss_url, args.edelweiss_remote, args.edelweiss_message)]
    try:
        for repository in repositories:
            repository.inspect()
        if not args.apply:
            print('\nPreview only. Review the paths, then rerun with --apply and any missing remote URL.')
            return 0
        # Both preflights must pass before either index or history is changed.
        for repository in repositories:
            repository.preflight_remote()
        for repository in repositories:
            repository.commit()
        for repository in repositories:
            repository.push()
        print('\nBoth repositories pushed successfully.')
        return 0
    except (Failure, OSError, subprocess.SubprocessError) as error:
        print(f'\nStopped: {error}\nNo rollback or force push was attempted. Commits/pushes already completed remain valid; correct the reported issue and rerun.', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
