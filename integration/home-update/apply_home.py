"""Import the supplied November 12 home screen onto the fixed iOS client."""
from pathlib import Path
import gzip, hashlib, json, subprocess, sys
root = Path(sys.argv[1])
here = Path(__file__).resolve().parent
lock = json.loads((here / 'IMPORT_LOCK.json').read_text())
state = json.loads((root / 'ELY_SOURCE_LOCK.json').read_text())
assert state.get('voice_chat_removed') is True
for name, digest in lock['base_file_sha256'].items():
    assert hashlib.sha256((root / name).read_bytes()).hexdigest() == digest, name
packed = b''.join(p.read_bytes() for p in sorted((here / 'patch').glob('*.bin')))
assert hashlib.sha256(packed).hexdigest() == lock['patch_sha256']
patch = gzip.decompress(packed)
for check in [True, False]:
    args = ['git', '-C', str(root), 'apply', '--binary', '--whitespace=nowarn']
    if check:
        args.append('--check')
    subprocess.run(args + ['-'], input=patch, check=True)
assert not (root / 'src/game/client/components/bestclient/voice').exists()
assert 'DispatchTouchState' in (root / 'src/game/client/gameclient.cpp').read_text()
assert '*pLeft = 0;' in (root / 'src/ios/ios_main.mm').read_text()
state['home_update'] = {key: lock[key] for key in ['input_zip_sha256', 'patch_sha256', 'files']}
(root / 'ELY_SOURCE_LOCK.json').write_text(json.dumps(state, indent=2) + '\n')
print('Imported new Ely home and loading screens with iOS touch targets')
