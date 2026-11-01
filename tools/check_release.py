#!/usr/bin/env python3
"""Local release checks for Advanced Dirt Removal (no CI service is used).

  python3 tools/check_release.py scan [--history] [--denylist FILE] [PATH ...]
      Fail when any file that would be published (tracked plus untracked,
      not ignored), or with --history every commit message and diff on the
      current branch, matches a denylist pattern. Binary files are scanned as
      bytes. The denylist is a local, untracked file (default
      .sanitize-denylist): one case-insensitive regular expression per line,
      '#' comments allowed. Extra PATHs (e.g. a built bundle) are scanned too.

  python3 tools/check_release.py build [--backends metal,opencl,cpu] [--build-dir DIR]
      Configure and build with CMake, then check the engine against
      tests/engine_hashes.json on each backend and run the OFX contract test.
"""
from __future__ import annotations

import argparse
import json
import os
import platform
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def git(*args: str) -> str:
    return subprocess.run(['git', '-C', str(ROOT), *args], check=True, capture_output=True).stdout.decode(
        'utf-8', 'replace')


def load_denylist(path: Path) -> list[re.Pattern]:
    if not path.is_file():
        sys.exit(f'denylist not found: {path} (create it locally; never commit it)')
    patterns = []
    for line in path.read_text(encoding='utf-8').splitlines():
        line = line.strip()
        if line and not line.startswith('#'):
            patterns.append(re.compile(line.encode('utf-8'), re.IGNORECASE))
    if not patterns:
        sys.exit('denylist is empty')
    return patterns


def is_utf8(data: bytes) -> bool:
    try:
        data.decode('utf-8')
        return True
    except UnicodeDecodeError:
        return False


def publishable_files() -> list[Path]:
    names = git('ls-files', '-z', '--cached', '--others', '--exclude-standard').split('\0')
    return [ROOT / n for n in names if n and (ROOT / n).is_file()]


def scan_bytes(label: str, data: bytes, patterns: list[re.Pattern], hits: list[str]):
    for pattern in patterns:
        for m in pattern.finditer(data):
            line = data.count(b'\n', 0, m.start()) + 1
            start = data.rfind(b'\n', 0, m.start()) + 1
            end = data.find(b'\n', m.end())
            context = data[start:end if end >= 0 else len(data)][:160].decode('utf-8', 'replace').strip()
            hits.append(f'{label}:{line}: [{pattern.pattern.decode()}] {context}')


def cmd_scan(a) -> int:
    patterns = load_denylist(a.denylist)
    hits: list[str] = []
    files = publishable_files()
    for extra in a.paths:
        p = Path(extra)
        files += [f for f in p.rglob('*') if f.is_file()] if p.is_dir() else [p]
    for f in files:
        if f.resolve() == a.denylist.resolve():
            continue
        try:
            label = str(f.relative_to(ROOT))
        except ValueError:
            label = str(f)
        data = f.read_bytes()
        if b'\0' in data[:8192] or not is_utf8(data):
            # Binary: scan printable runs of eight or more characters, like strings(1).
            data = b'\n'.join(re.findall(rb'[\x20-\x7e]{8,}', data))
        scan_bytes(label, label.encode() + b'\n' + data, patterns, hits)
    if a.history:
        log = subprocess.run(['git', '-C', str(ROOT), 'log', '-p', '--format=commit %H%n%an <%ae>%n%B'],
                             check=True, capture_output=True).stdout
        scan_bytes('history', log, patterns, hits)
    for h in hits:
        print(h)
    print(f'scan: {len(files)} files{" and history" if a.history else ""}, {len(hits)} findings')
    return 1 if hits else 0


def run(cmd: list[str], **kw):
    print('+', ' '.join(cmd), flush=True)
    return subprocess.run(cmd, check=True, **kw)


def cmd_build(a) -> int:
    build = Path(a.build_dir)
    options = ['-DCMAKE_BUILD_TYPE=Release']
    if 'opencl' in a.backends:
        options.append('-DADR_WITH_OPENCL=ON')
    if 'cuda' in a.backends:
        options.append('-DADR_WITH_CUDA=ON')
    run(['cmake', '-S', str(ROOT), '-B', str(build), *options])
    run(['cmake', '--build', str(build), '--config', 'Release', '--parallel'])
    exe = '.exe' if platform.system() == 'Windows' else ''
    bench = next(build.rglob(f'adr_bench{exe}'))
    contract = next(build.rglob(f'adr_ofx_contract{exe}'))
    expected = json.loads((ROOT / 'tests' / 'engine_hashes.json').read_text())
    failures = 0
    for backend in a.backends:
        for case in expected['cases']:
            seed, w, h, n = case['clip']
            cmd = [str(bench), '--synthetic', str(seed), '--width', str(w), '--height', str(h), '--frames', str(n),
                   '--backend', backend, '--expect', case['sha256']]
            for c in case['controls']:
                cmd += ['--control', c]
            out = json.loads(subprocess.run(cmd, check=True, capture_output=True, text=True).stdout)
            ok = out['runs'][0]['matches_expected'] is True
            failures += not ok
            print(f"{backend:7s} {case['name']:22s} {'ok' if ok else 'DIFFERENT'}", flush=True)
        work = build / 'contract'
        work.mkdir(exist_ok=True)
        source, target = work / 'source.f32', work / 'target.f32'
        base = ['--synthetic', '5', '--width', '512', '--height', '512', '--frames', '21']
        subprocess.run([str(bench), *base, '--backend', backend, '--write-input', str(source)], check=True,
                       capture_output=True)
        subprocess.run([str(bench), '--input', str(source), '--width', '512', '--height', '512', '--frames', '21',
                        '--backend', backend, '--output', str(target)], check=True, capture_output=True)
        r = subprocess.run([str(contract), backend, str(source), str(target), '512', '512', '21'],
                           capture_output=True, text=True)
        failures += r.returncode != 0
        print(f"{backend:7s} OFX contract           {'ok' if r.returncode == 0 else 'FAILED'}", flush=True)
        if r.returncode:
            print((r.stdout + r.stderr)[-2000:])
    print(f'build: {"passed" if not failures else f"{failures} failures"}')
    return 1 if failures else 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    s = sub.add_parser('scan')
    s.add_argument('--history', action='store_true')
    s.add_argument('--denylist', type=Path, default=ROOT / '.sanitize-denylist')
    s.add_argument('paths', nargs='*')
    b = sub.add_parser('build')
    default = 'metal,opencl,cpu' if platform.system() == 'Darwin' else 'cpu'
    b.add_argument('--backends', type=lambda v: v.split(','), default=default.split(','))
    b.add_argument('--build-dir', default=str(ROOT / 'build' / 'check'))
    a = p.parse_args()
    return cmd_scan(a) if a.command == 'scan' else cmd_build(a)


if __name__ == '__main__':
    sys.exit(main())
