"""Import only the delta from the supplied Ely archive onto the fixed iOS port."""
from pathlib import Path
import gzip, hashlib, json, subprocess, sys
root=Path(sys.argv[1]);here=Path(__file__).resolve().parent
lock=json.loads((here/'IMPORT_LOCK.json').read_text())
source_lock=root/'ELY_SOURCE_LOCK.json'; state=json.loads(source_lock.read_text())
assert state.get('voice_chat_removed') is True, 'Expected the current voice-free iOS baseline'
for name,digest in lock['base_file_sha256'].items():
 assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest, f'Wrong base file: {name}'
packed=b''.join(p.read_bytes() for p in sorted((here/'patch').glob('*.bin')))
assert len(packed)==lock['patch_bytes'] and hashlib.sha256(packed).hexdigest()==lock['patch_sha256']
patch=gzip.decompress(packed)
for check in [True,False]:
 args=['git','-C',str(root),'apply','-p2','--binary','--whitespace=nowarn']
 if check:args.append('--check')
 subprocess.run(args+['-'],input=patch,check=True)
for p in (root/'src').rglob('*.cpp'):
 assert 'm_VoiceChat' not in p.read_text(errors='replace'),p
assert not (root/'src/game/client/components/bestclient/voice').exists()
assert 'DispatchTouchState' in (root/'src/game/client/gameclient.cpp').read_text()
assert 'SETTINGS_TOUCH' in (root/'src/game/client/components/menus_settings.cpp').read_text()
state['ely_update']={'input_zip_sha256':lock['input_zip_sha256'],'patch_sha256':lock['patch_sha256'],'changed_files':len(lock['changed_files']),'new_files':len(lock['new_files']),'limitations':lock['limitations']}
source_lock.write_text(json.dumps(state,indent=2)+'\n')
print('Imported new Ely UI, hook aim, HUD/chat, island, lyrics and effects settings; iOS controls and voice removal verified')
