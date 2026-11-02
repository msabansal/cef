"""Git metadata mount contracts; no Docker daemon or native build is used."""

import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import unittest
import uuid
from unittest import mock


class ComposeTest(unittest.TestCase):

    def setUp(self):
        self.root = Path(__file__).resolve().parents[1]
        self.fixture = self.root / '.artifacts' / ('compose-test-' +
                                                   uuid.uuid4().hex)
        self.fixture.mkdir(parents=True)

        def remove_readonly(function, path, error):
            os.chmod(path, stat.S_IWRITE)
            function(path)

        self.addCleanup(shutil.rmtree, self.fixture, onexc=remove_readonly)
        self.native = self.fixture / 'cef source'
        self.git('init', '--quiet', str(self.native))
        self.git('-C', str(self.native), '-c', 'user.name=CEF Tests', '-c',
                 'user.email=cef-tests@example.invalid', 'commit', '--quiet',
                 '--allow-empty', '-m', 'fixture')
        spec = importlib.util.spec_from_file_location(
            'compose', self.root / 'docker' / 'compose.py')
        self.compose = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.compose)

    def git(self, *args):
        return subprocess.run(['git', *args],
                              check=True,
                              capture_output=True,
                              text=True).stdout.strip()

    def check_mounts(self, source, external):
        original_run = subprocess.run
        observed = []

        def run(command, **kwargs):
            if command[0] != 'docker':
                return original_run(command, **kwargs)
            configs = [
                Path(command[i + 1])
                for i, arg in enumerate(command)
                if arg == '-f'
            ]
            observed.append(
                (command, kwargs['env'],
                             json.loads(configs[-1].read_text())))
            return subprocess.CompletedProcess(command, 0)

        before = self.git('-C', str(source), 'status', '--porcelain')
        with mock.patch.object(self.compose.subprocess, 'run', side_effect=run):
            self.assertEqual(0, self.compose.main(['config', '--quiet'],
                                                  source))
        command, env, config = observed[0]
        self.assertEqual(['config', '--quiet'], command[-2:])
        self.assertEqual(str(source.resolve()), env['CEF_SOURCE_DIRECTORY'])
        mounts = config['services']['builder']['volumes']
        self.assertEqual(mounts,
                         config['services']['builder-official']['volumes'])
        self.assertEqual({str(path.resolve()) for path in external},
                         {mount['source'] for mount in mounts})
        for mount in mounts:
            self.assertEqual(mount['source'], mount['target'])
            self.assertTrue(mount['read_only'])
            self.assertFalse(mount['bind']['create_host_path'])
        self.assertEqual(before,
                         self.git('-C', str(source), 'status', '--porcelain'))
        self.assertFalse(list(source.glob('.artifacts/compose-metadata-*')))
        self.assertTrue(
            (source / 'artifacts').is_dir(),
            'Artifacts bind must be created by the host user, not root Docker')
        self.assertFalse((source / 'artifacts' / 'official').exists(),
                         'Release invocations must not create official outputs')

    def test_standalone_clone_needs_no_external_metadata(self):
        self.check_mounts(self.native, [])

    def test_linked_worktree_mounts_gitdir_and_common_directory(self):
        linked = self.fixture / 'linked cef'
        self.git('-C', str(self.native), 'worktree', 'add', '--quiet', '-b',
                 'fixture-linked', str(linked))
        gitdir = Path(
            self.git('-C', str(linked), 'rev-parse', '--absolute-git-dir'))
        self.check_mounts(linked, [gitdir, self.native / '.git'])

    def test_reach_submodule_mounts_its_resolved_metadata(self):
        reach = self.fixture / 'reach'
        self.git('init', '--quiet', str(reach))
        self.git('-C', str(reach), '-c',
                 'protocol.file.allow=always', 'submodule', 'add', '--quiet',
                 str(self.native), 'external/cef')
        source = reach / 'external' / 'cef'
        gitdir = Path(
            self.git('-C', str(source), 'rev-parse', '--absolute-git-dir'))
        self.check_mounts(source, [gitdir])

    def test_official_service_creates_its_artifacts_directory(self):
        original_run = subprocess.run

        def run(command, **kwargs):
            if command[0] == 'docker':
                return subprocess.CompletedProcess(command, 0)
            return original_run(command, **kwargs)

        with mock.patch.object(self.compose.subprocess, 'run', side_effect=run):
            self.assertEqual(
                0,
                self.compose.main(['run', '--rm', 'builder-official', 'all',
                                   '--profile', 'official'], self.native))
        self.assertTrue((self.native / 'artifacts' / 'official').is_dir())

    def service_block(self, name):
        text = (self.root / 'docker' / 'compose.yaml').read_text()
        start = text.index('\n  ' + name + ':\n')
        end = len(text)
        for marker in ('\n  builder', '\nvolumes:'):
            found = text.find(marker, start + 1)
            if found != -1:
                end = min(end, found)
        return text[start:end]

    def test_release_service_is_unchanged(self):
        block = self.service_block('builder')
        self.assertIn(
            'GN_DEFINES: ${GN_DEFINES:-use_sysroot=true is_component_build=false '
            'symbol_level=1 is_cfi=false use_thin_lto=false use_remoteexec=false '
            'enable_remoting=false proprietary_codecs=true '
            'ffmpeg_branding="Chrome"}', block)
        self.assertIn('image: cef-builder:ubuntu-26.10', block)
        self.assertNotIn('target:', block.split('environment:')[0])
        self.assertNotIn('CEF_BUILD_SERVICE_PROFILE', block)
        self.assertNotIn('cef-official-out', block)
        self.assertIn('source: ../artifacts\n', block)

    def test_official_service_isolates_outputs(self):
        block = self.service_block('builder-official')
        self.assertIn('target: official', block)
        self.assertIn('CEF_BUILD_SERVICE_PROFILE: official', block)
        self.assertIn('GN_DEFINES: ${OFFICIAL_GN_DEFINES:-', block)
        self.assertIn('is_official_build=true', block)
        self.assertNotIn('use_thin_lto=false', block)
        self.assertNotIn('chrome_pgo_phase', block)
        self.assertIn('source: cef-work\n        target: /work\n', block)
        self.assertIn(
            'source: cef-official-out\n        target: /work/chromium/src/out\n',
            block)
        self.assertIn('source: ../artifacts/official\n        target: /artifacts',
                      block)

    def test_missing_metadata_fails_without_docker_or_host_directory_creation(
            self):
        missing = self.fixture / 'missing'
        missing.mkdir()
        with mock.patch.object(self.compose.subprocess, 'run') as run:
            run.side_effect = subprocess.CalledProcessError(128, ['git'])
            with self.assertRaises(subprocess.CalledProcessError):
                self.compose.main(['config'], missing)
        self.assertFalse((missing / '.git').exists())
        self.assertTrue(
            all(call.args[0][0] == 'git' for call in run.call_args_list))


if __name__ == '__main__':
    unittest.main()
