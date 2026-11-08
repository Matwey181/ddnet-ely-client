#!/usr/bin/env python3
"""Verify pinned upstream and apply the reviewed iOS integration patch."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
args = parser.parse_args()
control = Path(__file__).resolve().parent
lock = json.loads((control / 'SOURCE_LOCK.json').read_text())
head = subprocess.check_output(['git', '-C', str(args.source), 'rev-parse', 'HEAD'], text=True).strip()
if head != lock['bestclient']['commit']:
    raise SystemExit(f'Wrong BestClient source: {head}')
packed = (control / 'ios-integration.patch.gz').read_bytes()
if hashlib.sha256(packed).hexdigest() != lock['patch_sha256']:
    raise SystemExit('Integration patch checksum mismatch')
patch = gzip.decompress(packed)
subprocess.run(['git', '-C', str(args.source), 'apply', '--check', '-'], input=patch, check=True)
subprocess.run(['git', '-C', str(args.source), 'apply', '-'], input=patch, check=True)
(args.source / 'ELY_SOURCE_LOCK.json').write_text(json.dumps(lock, indent=2) + '\n')
for directory in ['scripts/ios', 'scripts/compile_libs']:
    for script in (args.source / directory).glob('*.sh'):
        script.chmod(script.stat().st_mode | 0o111)
print('Applied iOS integration to pinned BestClient source')
