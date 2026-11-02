"""Checks for release integrity and publication-boundary handling; no GPU required."""
import argparse
import contextlib
import hashlib
import importlib.util
import io
import json
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / f'{name}.py')
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


scan = module('check_release')
stage = module('stage_release')


class ReleaseTools(unittest.TestCase):
    def test_compressed_member_is_scanned_without_extracting_files(self):
        pattern = scan.re.compile(b'blocked_marker')
        payload = io.BytesIO()
        with zipfile.ZipFile(payload, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr('../../outside.bin', 'blocked_marker'.encode('utf-16-le'))
        hits = []
        scan.scan_payload('candidate.zip', payload.getvalue(), [pattern], hits)
        self.assertEqual(len(hits), 1)
        self.assertIn('candidate.zip!/../../outside.bin', hits[0])

    def test_unicode_metadata_and_new_history(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            def git(*args):
                return subprocess.run(['git', '-C', directory, *args], check=True, capture_output=True, text=True).stdout.strip()

            git('init', '-q')
            git('config', 'user.name', 'Release Test')
            git('config', 'user.email', 'test@example.invalid')
            (root / 'old.txt').write_text('blocked_marker\n')
            git('add', '.')
            git('commit', '-qm', 'Accepted baseline')
            base = git('rev-parse', 'HEAD')
            (root / 'old.txt').write_text('clean text\n')
            git('add', '.')
            git('commit', '-qm', 'Replace old description')
            denylist = root / '.deny'
            denylist.write_text('blocked_marker\n')
            args = argparse.Namespace(denylist=denylist, paths=[], history=False, history_since=base)
            with patch.object(scan, 'ROOT', root), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(scan.cmd_scan(args), 0)
                (root / 'metadata.bin').write_bytes(b'\0' + 'blocked_marker'.encode('utf-16-le'))
                self.assertEqual(scan.cmd_scan(args), 1)
                (root / 'metadata.bin').write_bytes(b'\0' + 'blocked_marker'.encode('utf-16-be'))
                self.assertEqual(scan.cmd_scan(args), 1)
                (root / 'metadata.bin').write_bytes(b'\0clean payload')
                git('add', 'metadata.bin')
                git('commit', '-qm', '-blocked_marker in a message')
                self.assertEqual(scan.cmd_scan(args), 1)

    def test_verified_archives_tamper_and_existing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'docs').mkdir()
            (root / 'docs/INSTALL.md').write_text('[Guide](USER_GUIDE.md#recovery) [Read](../README.md#licence)')
            (root / 'docs/RELEASE_NOTES.md').write_text('2.0.0 [Tests](TESTED.md)')
            (root / 'ofx/resources').mkdir(parents=True)
            (root / 'ofx/resources/recovery-model.bin').write_bytes(b'weights')
            (root / 'WEIGHTS_LICENSE').write_text('CC BY-NC 4.0')
            (root / 'LICENSE').write_text('MIT')
            inputs = root / 'inputs'
            inputs.mkdir()
            assets = []
            for platform, ext in [('macos', 'pkg'), ('windows', 'exe'), ('linux', 'tar.gz')]:
                data = (platform + ' installer').encode()
                name = f'installer.{ext}'
                (inputs / name).write_bytes(data)
                assets.append({'platform': platform, 'filename': name, 'sha256': hashlib.sha256(data).hexdigest()})
            manifest = root / 'manifest.json'
            manifest.write_text(json.dumps({'version': '2.0.0', 'assets': assets}))
            with patch.object(stage, 'ROOT', root):
                output = root / 'release'
                stage.stage(manifest, inputs, output)
                with zipfile.ZipFile(output / 'AdvancedDirtRemoval-Setup-2.0.0-win64.zip') as archive:
                    self.assertEqual(archive.namelist(), ['installer.exe'])
                    self.assertEqual(archive.read('installer.exe'), b'windows installer')
                self.assertEqual((output / 'installer.exe').read_bytes(), b'windows installer')
                self.assertEqual((output / 'recovery-model-2.0.0.bin').read_bytes(), b'weights')
                self.assertEqual((output / 'WEIGHTS_LICENSE.txt').read_text(), 'CC BY-NC 4.0')
                self.assertEqual((output / 'LICENSE').read_text(), 'MIT')
                self.assertIn('/blob/v2.0.0/docs/USER_GUIDE.md#recovery', (output / 'INSTALL.md').read_text())
                self.assertIn('](https://github.com/fabiocolor/Advanced-Dirt-Removal/blob/v2.0.0/README.md#licence)',
                              (output / 'INSTALL.md').read_text())
                self.assertIn('/blob/v2.0.0/docs/TESTED.md', (output / 'RELEASE_NOTES.md').read_text())
                for line in (output / 'SHA256SUMS').read_text().splitlines():
                    digest, name = line.split('  ')
                    self.assertEqual(stage.digest(output / name), digest)
                with self.assertRaises(ValueError):
                    stage.stage(manifest, inputs, output)
                (inputs / 'installer.exe').write_bytes(b'modified installer')
                with self.assertRaises(ValueError):
                    stage.stage(manifest, inputs, root / 'bad-release')
                self.assertFalse((root / 'bad-release').exists())


if __name__ == '__main__':
    unittest.main()
