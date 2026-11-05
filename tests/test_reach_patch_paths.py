"""Characterize nested names using CEF's unmodified patch tools."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


class ReachPatchPathsTest(unittest.TestCase):

    def test_nested_names_apply_update_and_reset(self):
        root = Path(__file__).resolve().parents[1]
        artifacts = root / '.artifacts'
        artifacts.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=artifacts) as temporary:
            src = Path(temporary)
            cef = src / 'cef'
            tools = cef / 'tools'
            tools.mkdir(parents=True)
            for name in ('patcher.py', 'patch_updater.py', 'git_util.py',
                         'exec_util.py', 'file_util.py'):
                shutil.copyfile(root / 'tools' / name, tools / name)
            entries = [
                {'name': 'reach/example'},
                {'name': 'reach/skia_example', 'path': 'third_party/skia'},
            ]
            (cef / 'patch').mkdir()
            (cef / 'patch' / 'patch.cfg').write_text('patches = ' +
                                                     repr(entries) + '\n',
                                                     encoding='utf-8')
            environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')

            def run(*command, cwd=src):
                result = subprocess.run(command,
                                        cwd=cwd,
                                        env=environment,
                                        capture_output=True,
                                        text=True)
                self.assertEqual(result.returncode, 0,
                                 result.stdout + result.stderr)
                return result.stdout

            for entry in entries:
                repo = src / entry.get('path', '')
                repo.mkdir(parents=True, exist_ok=True)
                run('git', 'init', '--quiet', cwd=repo)
                run('git', 'config', 'core.autocrlf', 'false', cwd=repo)
                file = repo / 'example.txt'
                file.write_bytes(b'original\n')
                run('git', 'add', 'example.txt', cwd=repo)
                run('git',
                    '-c',
                    'user.name=ReachTests',
                    '-c',
                    'user.email=reach-tests@example.invalid',
                    'commit',
                    '--quiet',
                    '-m',
                    'fixture',
                    cwd=repo)
                file.write_bytes(b'patched\n')
                patch = cef / 'patch' / 'patches' / (entry['name'] + '.patch')
                patch.parent.mkdir(parents=True, exist_ok=True)
                patch.write_bytes(
                    run('git', 'diff', '--no-prefix', '--full-index',
                        cwd=repo).encode())
                run('git', 'restore', 'example.txt', cwd=repo)
            # An unregistered snapshot in the same folder must not be applied.
            (cef / 'patch' / 'patches' / 'reach' / 'snapshot.patch').write_text(
                'not an applicable patch\n', encoding='utf-8')
            patcher = tools / 'patcher.py'
            self.assertIn('2 applied', run(sys.executable, str(patcher)))
            for entry in entries:
                file = src / entry.get('path', '') / 'example.txt'
                self.assertEqual(file.read_bytes(), b'patched\n')
            self.assertIn(
                'already applied',
                run(sys.executable, str(patcher), '--patch-file',
                    'reach/example'))
            updater = tools / 'patch_updater.py'
            (src / 'example.txt').write_bytes(b'updated\n')
            run(sys.executable, str(updater), '--resave', '--patch',
                'reach/example')
            self.assertIn(b'+updated\n', (cef / 'patch' / 'patches' / 'reach' /
                                          'example.patch').read_bytes())
            run(sys.executable, str(updater), '--revert', '--patch',
                'reach/example')
            self.assertEqual((src / 'example.txt').read_bytes(), b'original\n')
            self.assertEqual(
                (src / 'third_party' / 'skia' / 'example.txt').read_bytes(),
                b'patched\n')
            run(sys.executable, str(patcher), '--patch-file', 'reach/example')
            self.assertEqual((src / 'example.txt').read_bytes(), b'updated\n')
            reset = root / 'docker' / 'reset_patched_files.py'
            run(sys.executable, str(reset), 'reset', str(src))
            for entry in entries:
                file = src / entry.get('path', '') / 'example.txt'
                self.assertEqual(file.read_bytes(), b'original\n')
            run(sys.executable, str(patcher))
            run(sys.executable, str(reset), 'restore', str(src))
            for entry in entries:
                file = src / entry.get('path', '') / 'example.txt'
                expected = b'patched\n' if entry.get('path') else b'updated\n'
                self.assertEqual(file.read_bytes(), expected)


if __name__ == '__main__':
    unittest.main()
