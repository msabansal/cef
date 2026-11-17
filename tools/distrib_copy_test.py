"""Tests for incremental runtime distribution copies."""

import errno
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock

import distrib_copy


class DistribCopyTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'source'
        self.previous = self.root / 'previous'
        self.destination = self.root / 'destination'
        self.source.mkdir()
        self.previous.mkdir()
        (self.source / 'runtime').write_bytes(b'old frame')
        shutil.copy2(self.source / 'runtime', self.previous / 'runtime')

    def test_identical_runtime_is_reused_and_changed_bytes_are_copied(self):
        self.destination.mkdir()
        target = self.destination / 'runtime'
        distrib_copy.copy_runtime_file(
            self.source / 'runtime', target, self.previous / 'runtime')
        self.assertTrue(os.path.samefile(target, self.previous / 'runtime'))

        original_time = (self.source / 'runtime').stat().st_mtime_ns
        (self.source / 'runtime').write_bytes(b'new frame')
        os.utime(self.source / 'runtime', ns=(original_time, original_time))
        distrib_copy.copy_runtime_file(
            self.source / 'runtime', target, self.previous / 'runtime')
        self.assertEqual(target.read_bytes(), b'new frame')
        self.assertEqual((self.previous / 'runtime').read_bytes(), b'old frame')
        self.assertFalse(os.path.samefile(target, self.previous / 'runtime'))

    def test_tree_does_not_reintroduce_removed_files(self):
        (self.previous / 'stale').write_bytes(b'removed')
        (self.source / 'nested').mkdir()
        (self.source / 'nested' / 'new').write_bytes(b'new')
        distrib_copy.copy_runtime_path(
            self.source, self.destination, self.previous)
        self.assertEqual(
            sorted(str(path.relative_to(self.destination))
                   for path in self.destination.rglob('*')),
            ['nested', os.path.join('nested', 'new'), 'runtime'])
        self.assertTrue(os.path.samefile(
            self.destination / 'runtime', self.previous / 'runtime'))

    def test_cross_device_reuse_falls_back_to_copy(self):
        self.destination.mkdir()
        with mock.patch.object(
                distrib_copy.os, 'link',
                side_effect=OSError(errno.EXDEV, 'device')):
            distrib_copy.copy_runtime_file(
                self.source / 'runtime',
                self.destination / 'runtime',
                self.previous / 'runtime')
        self.assertEqual(
            (self.destination / 'runtime').read_bytes(), b'old frame')
        self.assertFalse(os.path.samefile(
            self.destination / 'runtime', self.previous / 'runtime'))

    def test_unexpected_link_failures_are_reported(self):
        self.destination.mkdir()
        with mock.patch.object(
                distrib_copy.os, 'link',
                side_effect=PermissionError(errno.EACCES, 'denied')):
            with self.assertRaises(PermissionError):
                distrib_copy.copy_runtime_file(
                    self.source / 'runtime',
                    self.destination / 'runtime',
                    self.previous / 'runtime')


if __name__ == '__main__':
    unittest.main()
