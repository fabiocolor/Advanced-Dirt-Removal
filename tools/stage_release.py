#!/usr/bin/env python3
"""Assemble verified release downloads locally. Never uploads or changes Git refs."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def digest(path: Path) -> str:
    sha = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            sha.update(block)
    return sha.hexdigest()


def stage(manifest: Path, inputs: Path, output: Path) -> dict:
    spec = json.loads(manifest.read_text())
    version = spec['version']
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('expected a three-part release version')
    if output.exists():
        raise ValueError('output already exists; choose a new directory to preserve previous candidates')
    verified = []
    for asset in spec['assets']:
        name = asset['filename']
        if Path(name).name != name or name in ('', '.', '..'):
            raise ValueError('asset filename must be a plain basename')
        source = inputs / name
        if digest(source) != asset['sha256']:
            raise ValueError(f'hash mismatch: {name}; do not ship an unverified rebuild')
        verified.append((asset, source))
    if {a['platform'] for a, _ in verified} != {'macos', 'windows', 'linux'} or len(verified) != 3:
        raise ValueError('expected one verified installer for each platform')
    output.mkdir(parents=True)
    for asset, source in verified:
        platform = asset['platform']
        if platform == 'linux':
            shutil.copyfile(source, output / source.name)
        else:
            if platform == 'windows':
                # As in 1.x: the raw setup program is attached too, for winget.
                shutil.copyfile(source, output / source.name)
            name = (f'AdvancedDirtRemoval-{version}-macos.zip' if platform == 'macos'
                    else f'AdvancedDirtRemoval-Setup-{version}-win64.zip')
            with zipfile.ZipFile(output / name, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
                entry = zipfile.ZipInfo(source.name, date_time=(2026, 1, 1, 0, 0, 0))
                entry.compress_type = zipfile.ZIP_DEFLATED
                entry.external_attr = 0o100644 << 16
                archive.writestr(entry, source.read_bytes())
            with zipfile.ZipFile(output / name) as archive:
                if archive.namelist() != [source.name] or archive.testzip() is not None:
                    raise ValueError(f'archive verification failed: {name}')
                if hashlib.sha256(archive.read(source.name)).hexdigest() != asset['sha256']:
                    raise ValueError(f'archive payload changed: {name}')
    # Standalone release attachments need links that work outside the repository.
    for name in ('INSTALL.md', 'RELEASE_NOTES.md'):
        text = (ROOT / 'docs' / name).read_text()
        text = re.sub(r'\]\((USER_GUIDE\.md|TESTED\.md)([^)]*)\)',
                      rf'](https://github.com/fabiocolor/Advanced-Dirt-Removal/blob/v{version}/docs/\1\2)', text)
        (output / name).write_text(text)
    report = {'version': version, 'publication': 'held_for_owner_approval', 'inputs': spec['assets'],
              'downloads': {p.name: {'sha256': digest(p), 'bytes': p.stat().st_size}
                            for p in sorted(output.iterdir())}}
    (output / 'release-manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    files = sorted(output.iterdir())
    (output / 'SHA256SUMS').write_text(''.join(f'{digest(p)}  {p.name}\n' for p in files))
    for path in files:
        assert digest(path) == next(line.split()[0] for line in (output / 'SHA256SUMS').read_text().splitlines()
                                    if line.endswith('  ' + path.name))
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--input-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    report = stage(args.manifest, args.input_dir, args.output_dir)
    print(f"Verified {len(report['inputs'])} installers; staged downloads and SHA256SUMS in {args.output_dir}")


if __name__ == '__main__':
    main()
