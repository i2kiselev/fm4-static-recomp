# Copyright (c) 2026, i2kiselev
# BSD 3-Clause License: see LICENSE in the repository root.
"""Functional SDK preparation tests: local repositories only, never the network."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mac_sdk import prepare_sdk


def git(directory, *args):
    result = subprocess.run(['git', '-C', str(directory), *args], text=True,
                            capture_output=True, check=True)
    return result.stdout.strip()


class MacSdkTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='fm4-mac-sdk-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'payload with spaces'
        self.origin = Path(self.temp.name) / 'origin'
        self.root.mkdir()
        self.origin.mkdir()
        git(self.origin, 'init')
        (self.origin / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.25)\n')
        (self.origin / 'compiler.cpp').write_text('before\n')
        git(self.origin, 'add', '.')
        git(self.origin, '-c', 'user.name=SDK Test', '-c', 'user.email=test@example.invalid', 'commit', '-m', 'fixture')
        self.sdk = dict(repository=self.origin.as_uri(), revision=git(self.origin, 'rev-parse', 'HEAD'))
        self.config = dict(minimum_macos='14.0', sdk=dict(self.sdk))
        (self.root / 'config').mkdir()
        self.write_config()
        (self.root / 'config/release-toolchain.json').write_text(json.dumps(dict(sdk=self.sdk)))

    def write_config(self):
        (self.root / 'config/macos-toolchain.json').write_text(json.dumps(self.config))

    def test_managed_checkout_is_pinned_clean_and_resumable(self):
        source, receipt = prepare_sdk(self.root)
        self.assertEqual(git(source, 'rev-parse', 'HEAD'), self.sdk['revision'])
        self.assertEqual(git(source, 'status', '--porcelain'), '')
        self.assertFalse(receipt['external'])
        again, repeated = prepare_sdk(self.root)
        self.assertEqual(source, again)
        self.assertEqual(receipt, repeated)
        self.assertEqual((source / 'compiler.cpp').read_text(), 'before\n')

    def test_release_pin_is_used_without_the_temporary_mac_override(self):
        self.config.pop('sdk')
        self.write_config()
        _, receipt = prepare_sdk(self.root)
        self.assertEqual(receipt['revision'], self.sdk['revision'])
        self.assertEqual(receipt['repository'], self.sdk['repository'])

    def test_temporary_mac_candidate_takes_precedence_during_review(self):
        (self.root / 'config/release-toolchain.json').write_text(json.dumps(dict(sdk=dict(self.sdk, revision='a' * 40))))
        _, receipt = prepare_sdk(self.root)
        self.assertEqual(receipt['revision'], self.sdk['revision'])

    def test_external_revision_mismatch_is_rejected_without_changes(self):
        self.config['sdk']['revision'] = 'a' * 40
        self.write_config()
        with self.assertRaisesRegex(ValueError, 'revision mismatch'):
            prepare_sdk(self.root, self.origin)
        self.assertEqual(git(self.origin, 'status', '--porcelain'), '')

    def test_invalid_pin_is_rejected_before_checkout(self):
        self.config['sdk']['revision'] = 'main'
        self.write_config()
        with self.assertRaisesRegex(ValueError, 'full Git commit'):
            prepare_sdk(self.root)
        self.assertFalse((self.root / '.local').exists())

    def test_development_changes_are_preserved_and_recorded(self):
        _, clean = prepare_sdk(self.root, self.origin)
        (self.origin / 'compiler.cpp').write_text('my own edit\n')
        _, changed = prepare_sdk(self.root, self.origin)
        self.assertTrue(changed['external'])
        self.assertNotEqual(clean['working_diff_sha256'], changed['working_diff_sha256'])
        self.assertEqual((self.origin / 'compiler.cpp').read_text(), 'my own edit\n')

    def test_non_repository_managed_directory_is_not_adopted(self):
        managed = self.root / '.local/shiftglue-sdk-macos' / self.sdk['revision']
        managed.mkdir(parents=True)
        (managed / 'mine.txt').write_text('keep')
        with self.assertRaisesRegex(ValueError, 'not empty'):
            prepare_sdk(self.root)
        self.assertEqual((managed / 'mine.txt').read_text(), 'keep')

    def test_managed_local_edit_is_rejected_without_resetting_it(self):
        source, _ = prepare_sdk(self.root)
        (source / 'CMakeLists.txt').write_text('my own build change\n')
        with self.assertRaisesRegex(ValueError, 'local changes'):
            prepare_sdk(self.root)
        self.assertEqual((source / 'CMakeLists.txt').read_text(), 'my own build change\n')


class MacPayloadTest(unittest.TestCase):
    def package_module(self):
        path = Path(__file__).resolve().parents[1] / 'package-launcher-macos.py'
        spec = importlib.util.spec_from_file_location('package_mac', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_payload_has_upstream_release_metadata_and_no_legacy_tools(self):
        module = self.package_module()
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'source.zip'
            module.source_payload(archive)
            with zipfile.ZipFile(archive) as source:
                for name in ('tools/mac_sdk.py', 'tools/setup-macos.py', 'guide/macos.md', 'src/ui/guest_frame_stats.h'):
                    self.assertIn(name, source.namelist())
                self.assertEqual(json.loads(source.read('config/release.json')),
                                 json.loads((module.ROOT / 'config/release.json').read_text()))
                self.assertFalse(any(n.startswith(('game/', 'user/', 'out/', '.local/', 'thirdparty/', 'patches/', 'docs/', 'tools/metal-interop/')) for n in source.namelist()))
                self.assertNotIn('tools/sdk_patches.py', source.namelist())
                self.assertNotIn('config/macos-release.json', source.namelist())

    def test_missing_required_payload_source_fails_loudly(self):
        module = self.package_module()
        with tempfile.TemporaryDirectory() as directory:
            module.ROOT = Path(directory)
            with self.assertRaises(FileNotFoundError):
                module.source_payload(Path(directory) / 'source.zip')


if __name__ == '__main__':
    unittest.main()
