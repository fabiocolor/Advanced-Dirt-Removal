#!/usr/bin/env python3
"""Local release checks for Advanced Dirt Removal (no CI service is used).

  python3 tools/check_release.py scan [--history | --history-since REV] [--denylist FILE] [PATH ...]
      Fail when any file that would be published (tracked plus untracked,
      not ignored), or with --history every commit message and diff on the
      current branch, matches a denylist pattern. --history-since REV scans
      only messages, added lines and paths after an explicitly accepted ancestor
      (deleted text from that ancestor is not new publication content). Binary files are
      scanned for ASCII and UTF-16 strings. The denylist is a local, untracked file (default
      .sanitize-denylist): one case-insensitive regular expression per line,
      '#' comments allowed. Extra PATHs (e.g. a built bundle) are scanned too.

  python3 tools/check_release.py build [--backends metal,opencl,cpu] [--build-dir DIR]
      Configure and build with CMake, then check the engine against
      tests/engine_hashes.json on each backend and run the OFX contract test.
"""
from __future__ import annotations

import argparse
import io
import json
import os
import platform
import re
import subprocess
import sys
import tarfile
import zipfile
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


def scan_payload(label: str, data: bytes, patterns: list[re.Pattern], hits: list[str], *, depth=0, budget=None):
    """Scan archive members, not random strings in their compressed envelopes."""
    if budget is None:
        budget = [512 * 1024 * 1024]
    scan_bytes(label, label.encode(), patterns, hits)
    if label.lower().endswith(('.zip', '.tar', '.tar.gz', '.tgz')):
        if depth >= 4:
            raise ValueError(f'archive nesting limit reached: {label}; unpack and scan separately')

        def member(name, size, read):
            budget[0] -= size
            if budget[0] < 0:
                raise ValueError(f'archive size limit reached: {label}; unpack and scan separately')
            scan_payload(f'{label}!/{name}', read(), patterns, hits, depth=depth + 1, budget=budget)

        if label.lower().endswith('.zip'):
            with zipfile.ZipFile(io.BytesIO(data)) as archive:
                for item in archive.infolist():
                    if not item.is_dir():
                        member(item.filename, item.file_size, lambda: archive.read(item))
        else:
            with tarfile.open(fileobj=io.BytesIO(data), mode='r:*') as archive:
                for item in archive:
                    if item.isfile():
                        member(item.name, item.size, lambda: archive.extractfile(item).read())
                    else:
                        scan_bytes(label, (item.name + '\n' + item.linkname).encode(), patterns, hits)
        return
    if b'\0' in data[:8192] or not is_utf8(data):
        # Include Windows resources and other UTF-16 metadata as well as ASCII.
        strings = re.findall(rb'[\x20-\x7e]{8,}', data)
        strings += [s.decode('utf-16-le').encode() for s in re.findall(rb'(?:[\x20-\x7e]\x00){4,}', data)]
        strings += [s.decode('utf-16-be').encode() for s in re.findall(rb'(?:\x00[\x20-\x7e]){4,}', data)]
        data = b'\n'.join(strings)
    scan_bytes(label, data, patterns, hits)


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
        scan_payload(label, f.read_bytes(), patterns, hits)
    history_range = []
    if a.history_since:
        revision = git('rev-parse', '--verify', f'{a.history_since}^{{commit}}').strip()
        subprocess.run(['git', '-C', str(ROOT), 'merge-base', '--is-ancestor', revision, 'HEAD'], check=True)
        history_range = [f'{revision}..HEAD']
    if a.history or a.history_since:
        log = subprocess.run(['git', '-C', str(ROOT), 'log', '-p', '--unified=0', '--format=commit %H%n%an <%ae>%n%B', *history_range],
                             check=True, capture_output=True).stdout
        if history_range:
            # Keep messages separate: a message beginning with '-' is still new content.
            messages = git('log', '--no-patch', '--format=commit %H%n%an <%ae>%n%B', *history_range).encode()
            patches = git('log', '-p', '--unified=0', '--format=', *history_range).encode()
            log = messages + b'\n' + b'\n'.join(line for line in patches.splitlines() if not line.startswith(b'-'))
        scan_bytes('history', log, patterns, hits)
    for h in hits:
        print(h)
    scope = f' and history {history_range[0]}' if history_range else (' and full history' if a.history else '')
    print(f'scan: {len(files)} files{scope}, {len(hits)} findings')
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
    history = s.add_mutually_exclusive_group()
    history.add_argument('--history', action='store_true')
    history.add_argument('--history-since', metavar='REV', help='scan commits after an explicitly accepted ancestor')
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
