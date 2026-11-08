#!/usr/bin/env python3
"""Package a real iPhone app; reject simulator binaries and incomplete bundles."""
from pathlib import Path
import os
import plistlib
import stat
import subprocess
import sys
import zipfile

build, output = Path(sys.argv[1]), Path(sys.argv[2])
apps = list(build.glob('**/Release-iphoneos/*.app'))
if len(apps) != 1:
    raise SystemExit(f'Expected one device app, found {len(apps)}')
app = apps[0]
with (app / 'Info.plist').open('rb') as stream:
    info = plistlib.load(stream)
executable = app / info['CFBundleExecutable']
if not executable.is_file() or not (app / 'data/touch_controls.json').is_file():
    raise SystemExit('Incomplete app bundle')
if 'arm64' not in subprocess.check_output(['lipo', '-archs', str(executable)], text=True):
    raise SystemExit('App executable is not arm64')
if info.get('CFBundleSupportedPlatforms') != ['iPhoneOS']:
    raise SystemExit('App is not built for iPhoneOS')
if not info.get('NSMicrophoneUsageDescription'):
    raise SystemExit('Missing microphone permission description')
output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    for path in sorted(app.rglob('*')):
        name = 'Payload/' + app.name + '/' + path.relative_to(app).as_posix()
        if path.is_symlink():
            entry = zipfile.ZipInfo(name)
            entry.create_system = 3
            entry.external_attr = (stat.S_IFLNK | 0o777) << 16
            archive.writestr(entry, os.readlink(path))
        elif path.is_file():
            archive.write(path, name)
with zipfile.ZipFile(output) as archive:
    if archive.testzip() is not None:
        raise SystemExit('IPA ZIP integrity check failed')
print(f'Created device IPA: {output} ({output.stat().st_size} bytes). Requires signing before installation.')
