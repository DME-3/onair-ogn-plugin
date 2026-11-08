#!/usr/bin/env python3
"""Check tracked or staged source for common accidental private-data inclusions.

This is a conservative file/credential/path guard, not a substitute for review.
"""
import argparse
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys

ROOT_FILES = {
    '.gitignore', '.gitattributes', 'AGENTS.md', 'CMakeLists.txt', 'manifest.json',
    'LICENSE', 'NOTICE.txt', 'README.txt', 'README.md', 'TEST-REPORT.txt',
    'CONTRIBUTING.md', 'CODE_OF_CONDUCT.md', 'SECURITY.md', 'CHANGELOG.md',
}
SOURCE_DIRS = {'src', 'sdk', 'vendor', 'tests', 'tools', 'licenses', 'docs', '.github'}
SUFFIXES = {'.cpp', '.hpp', '.h', '.inc', '.py', '.txt', '.md', '.yml', '.yaml'}
SENSITIVE_PATTERNS = {
    'absolute personal filesystem path': re.compile(r'/(?:Users|home|Volumes)/[^\s\"\'<>]+'),
    'private key material': re.compile(r'-----BEGIN (?:[A-Z]+ )?PRIVATE KEY-----'),
    'GitHub credential': re.compile(r'\b(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})\b'),
    'AWS access key': re.compile(r'\b(?:AKIA|ASIA)[A-Z0-9]{16}\b'),
}


def check_file(name, data):
    path = PurePosixPath(name)
    errors = []
    if path.is_absolute() or '..' in path.parts:
        return ['unsafe path']
    if len(path.parts) == 1:
        if name not in ROOT_FILES:
            errors.append('unexpected root file; review and update the source allowlist')
    elif path.parts[0] not in SOURCE_DIRS or path.suffix not in SUFFIXES:
        errors.append('file outside the public source allowlist')
    if any(part in {'__pycache__', 'captures', 'recordings', 'results'} or
           part.lower().startswith('private') for part in path.parts):
        errors.append('private/generated path')
    try:
        content = data.decode('utf-8')
    except UnicodeDecodeError:
        return errors + ['binary data in source tree']
    if '\0' in content:
        errors.append('binary content in source tree')
    for label, pattern in SENSITIVE_PATTERNS.items():
        if pattern.search(content):
            errors.append(label)
    return errors


def source_files(root):
    paths = [root / name for name in ROOT_FILES if (root / name).is_file()]
    for folder in SOURCE_DIRS:
        paths.extend(p for p in (root / folder).rglob('*')
                     if p.is_file() and '__pycache__' not in p.parts
                     and p.name != '.DS_Store')
    return sorted(paths)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--staged', action='store_true', help='inspect Git index blobs')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    names = subprocess.check_output(['git', 'ls-files', '-z'], cwd=root).decode().split('\0')
    failed, count = False, 0
    for name in filter(None, names):
        if args.staged:
            data = subprocess.check_output(['git', 'show', ':' + name], cwd=root)
        else:
            path = root / name
            if not path.is_file() or path.is_symlink():
                print(f'{name}: missing file or symlink', file=sys.stderr)
                failed = True
                continue
            data = path.read_bytes()
        count += 1
        for error in check_file(name, data):
            # Do not echo matching content; it may itself be private.
            print(f'{name}: {error}', file=sys.stderr)
            failed = True
    if not count:
        print('No tracked files to inspect', file=sys.stderr)
        return 1
    if not failed:
        print(f'Public tree guard passed: {count} source files; review still required.')
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
