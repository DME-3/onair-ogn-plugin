#!/usr/bin/env python3
"""Create a local OnAir package, including complete source and license notices."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import zipfile
from check_public_tree import check_file, source_files

parser = argparse.ArgumentParser()
parser.add_argument('--build', default='build')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
build = Path(args.build)
if not build.is_absolute(): build = root / build
manifest = json.loads((root / 'manifest.json').read_text())
identity, version = manifest['id'], manifest['version']
assert identity == 'ogn-mband' and manifest['permissions'] == []
dist = root / 'dist'
stage = dist / identity
stage.mkdir(parents=True, exist_ok=True)
for name in ['manifest.json', 'LICENSE', 'NOTICE.txt', 'README.txt', 'TEST-REPORT.txt']:
    shutil.copyfile(root / name, stage / name)
library = stage / 'ogn-mband.dylib'
shutil.copyfile(build / 'plugins/ogn-mband/ogn-mband.dylib', library)
subprocess.run(['codesign', '--force', '--sign', '-', '--timestamp=none', '--identifier', 'local.onair.ogn-mband', str(library)], check=True)
subprocess.run(['codesign', '--verify', '--all-architectures', '--strict', str(library)], check=True)

# Keep complete source and public project guidance, using an explicit allowlist.
# Tar owner/path metadata is normalised and source text is checked before export.
sources = source_files(root)
source = io.BytesIO()
with tarfile.open(fileobj=source, mode='w') as tar:
    for path in sorted(sources):
        if path.is_symlink(): raise RuntimeError('Symlinks are not packaged')
        data = path.read_bytes()
        errors = check_file(path.relative_to(root).as_posix(), data)
        if errors: raise RuntimeError(f'Public source check failed for {path.name}: {errors}')
        info = tarfile.TarInfo(str(Path('ogn-mband-source') / path.relative_to(root)))
        info.size = len(data); info.mode = 0o644; info.uid = info.gid = 0
        info.uname = info.gname = ''; info.mtime = 0
        tar.addfile(info, io.BytesIO(data))
(stage / 'source.tar.gz').write_bytes(gzip.compress(source.getvalue(), mtime=0))
architectures = subprocess.check_output(['lipo', '-archs', str(library)], text=True).split()
if set(architectures) != {'arm64', 'x86_64'}:
    raise RuntimeError('Universal package requires both arm64 and x86_64')
package = dist / f'{identity}-{version}-macos-universal.onairplugin'
with zipfile.ZipFile(package,'w',zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(stage.iterdir()):
        if path.is_file(): archive.write(path, Path(identity) / path.name)
with zipfile.ZipFile(package) as archive:
    assert archive.testzip() is None and len(archive.namelist()) <= 64
digest = hashlib.sha256(package.read_bytes()).hexdigest()
(dist / 'SHA256SUMS').write_text(f'{digest}  {package.name}\n')
print(f'Prepared {package.name}: {package.stat().st_size} bytes')
