"""Remove the voice backend and all callable UI integrations from the iOS port."""
from pathlib import Path
import hashlib, json, re, shutil, sys
root=Path(sys.argv[1])
def edit(path, old, new=''):
 p=root/path;s=p.read_text();assert s.count(old)==1,(path,s.count(old),old[:70]);p.write_text(s.replace(old,new,1))
def span(path,start,end):
 p=root/path;s=p.read_text();a=s.index(start);b=s.index(end,a);p.write_text(s[:a]+s[b:])
edit('src/game/client/gameclient.h','#include "components/bestclient/voice/voice.h" // bestclient\n')
edit('src/game/client/gameclient.h','\tCVoiceChat m_VoiceChat; // bestclient\n')
p=root/'src/game/client/gameclient.cpp';s=p.read_text();s='\n'.join(l for l in s.split('\n') if '&m_VoiceChat,' not in l);s=re.sub(r'\tif\(pComp == &m_VoiceChat\)\n\t\treturn "VoiceChat";\n','',s);p.write_text(s)
span('src/game/client/components/bestclient/menus_bestclient.cpp','\tconst bool VoiceExpanded =','\tconst bool ShowNamePlateSettings =')
span('src/game/client/components/bestclient/menus_bestclient.cpp','CUi::EPopupMenuFunctionResult CMenus::PopupVoiceModeration(','void CMenus::RenderSettingsBestClientInfo(')
p=root/'src/game/client/components/menus.h';s=p.read_text();s='\n'.join(l for l in s.split('\n') if 'static CUi::EPopupMenuFunctionResult PopupVoiceModeration(' not in l);p.write_text(s)
span('src/game/client/components/scoreboard.cpp','\tif(!pPopupContext->m_IsLocal && g_Config.m_BcVoiceChatEnable)','\tconst int LocalId = pScoreboard->GameClient()->m_aLocalIds[')
edit('src/game/client/components/bestclient/bestclient.cpp','\tif(!IsLocal && g_Config.m_BcVoiceChatEnable)\n\t\tButtonRows += 2;\n')
edit('src/game/client/components/chat.cpp','\tif(GameClient()->m_VoiceChat.TryHandleChatCommand(pLine))\n\t\treturn;\n')
edit('src/game/client/components/nameplates.cpp','\tData.m_ShowVoiceTalking = g_Config.m_BcVoiceChatNameplateIcon != 0 &&\n\t\t\t\t  GameClient()->m_VoiceChat.IsClientTalking(pPlayerInfo->m_ClientId);','\tData.m_ShowVoiceTalking = false;')
edit('src/game/client/components/nameplates.cpp','\tData.m_ShowVoiceTalking = g_Config.m_BcVoiceChatNameplateIcon != 0;', '\tData.m_ShowVoiceTalking = false;')
edit('src/game/client/components/menus.h','\tstruct SPopupVoiceModerationContext\n\t{\n\t\tCMenus *m_pMenus;\n\t};\n')
for path in ['src/game/client/components/hud.cpp','src/game/client/components/bestclient/hud_editor.cpp']:
 p=root/path;s=p.read_text();s='\n'.join(l for l in s.split('\n') if 'm_VoiceChat.RenderHud' not in l and 'AddModule(HudLayout::MODULE_VOICE_' not in l);p.write_text(s)
span('src/game/client/components/bestclient/hud_editor.cpp','\tcase HudLayout::MODULE_VOICE_TALKERS:','\tcase HudLayout::MODULE_MUSIC_PLAYER:')
p=root/'src/game/client/components/bestclient/settings_search_catalog.inl';s=p.read_text();p.write_text('\n'.join(l for l in s.split('\n') if '"Voice", SETTINGS_NAV_LEAF_BC_OTHERS' not in l))
# Remove saved voice options; old configuration files cannot reactivate the module.
p=root/'src/engine/shared/config_variables_bestclient.h';s=p.read_text();p.write_text('\n'.join(l for l in s.split('\n') if not (l.startswith('MACRO_CONFIG_') and ('(BcVoiceChat' in l or '(BcVoiceMod' in l))))
# Migrate just dedicated voice buttons, leaving the rest of each user's layout intact.
anchor='\t\tstd::optional<CTouchButton> ParsedButton = ParseButton(&TouchButtons[ButtonIndex]);'
edit('src/game/client/components/touch_controls.cpp',anchor,'''\t\tconst json_value &VoiceCommand = TouchButtons[ButtonIndex]["behavior"]["command"];
\t\tif(VoiceCommand.type == json_string &&
\t\t\t(str_comp(VoiceCommand.u.string.ptr, "+voicechat") == 0 ||
\t\t\t str_comp(VoiceCommand.u.string.ptr, "toggle_voice_mic_mute") == 0 ||
\t\t\t str_comp(VoiceCommand.u.string.ptr, "toggle_voice_headphones_mute") == 0))
\t\t\tcontinue;
'''+anchor)
p=root/'data/touch_controls.json';data=json.loads(p.read_text());data['touch-buttons']=[b for b in data['touch-buttons'] if b.get('behavior',{}).get('command') not in ['+voicechat','toggle_voice_mic_mute','toggle_voice_headphones_mute']];p.write_text(json.dumps(data,indent='\t')+'\n')
p=root/'CMakeLists.txt';s=p.read_text();s='\n'.join(l for l in s.split('\n') if 'components/bestclient/voice/' not in l and 'src/ios/ios_voice_audio.cpp' not in l);p.write_text(s)
shutil.rmtree(root/'src/game/client/components/bestclient/voice')
for name in ['ios_voice_audio.cpp','ios_voice_audio.h','ios_voice_ring.h']:
 (root/'src/ios'/name).unlink()
p=root/'scripts/ios/files/Info.plist.in';s=p.read_text();s,n=re.subn(r'\s*<key>NSMicrophoneUsageDescription</key>\s*<string>[^<]*</string>','',s);assert n==1;p.write_text(s)
# Build-time absence checks, including all callers instead of only the update loop.
for p in (root/'src').rglob('*'):
 if p.suffix in ['.h','.cpp']:
  assert 'm_VoiceChat' not in p.read_text(errors='replace'),p
assert 'components/bestclient/voice/' not in (root/'CMakeLists.txt').read_text()
assert not (root/'src/game/client/components/bestclient/voice').exists()
assert '+voicechat' not in (root/'data/touch_controls.json').read_text()
p=root/'ELY_SOURCE_LOCK.json';d=json.loads(p.read_text());d['voice_chat_removed']=True;d['voice_removal_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest();p.write_text(json.dumps(d,indent=2)+'\n')
print('Removed voice backend, component, UI, HUD, dedicated touch binds and microphone permission')
