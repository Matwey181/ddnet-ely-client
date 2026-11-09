#!/usr/bin/env python3
"""Apply the tested touch-release fix and migrate the iOS frame-rate profile."""
from pathlib import Path
import hashlib
import json
import shutil
import sys

root = Path(sys.argv[1])
here = Path(__file__).resolve().parent
shutil.copyfile(here / 'overlays/mobile_runtime.h', root / 'src/game/client/mobile_runtime.h')

def replace_once(path, old, new):
    p = root / path
    text = p.read_text()
    assert text.count(old) == 1, f'Unexpected source context: {path}'
    p.write_text(text.replace(old, new, 1))

replace_once('src/game/client/gameclient.cpp', '#include "gameclient.h"',
             '#include "gameclient.h"\n#include "mobile_runtime.h"')
replace_once('src/game/client/gameclient.cpp', '''\tif(!vTouchFingerStates.empty())
\t{
\t\tfor(auto &pComponent : m_vpInput)
\t\t{
\t\t\tif(pComponent->OnTouchState(vTouchFingerStates))
\t\t\t{
\t\t\t\tInput()->ClearTouchDeltas();
\t\t\t}
\t\t}
\t}''', '''\tElyMobile::DispatchTouchState(m_vpInput, vTouchFingerStates, [this]() {
\t\tInput()->ClearTouchDeltas();
\t});''')
replace_once('src/game/client/gameclient.cpp', 'void CGameClient::OnInit()\n{', '''void CGameClient::OnInit()
{
#if defined(CONF_PLATFORM_IOS)
\tif(g_Config.m_ClIosRuntimeProfile < 1)
\t{
\t\tElyMobile::ApplyFrameProfile(g_Config.m_GfxRefreshRate, g_Config.m_ClRefreshRate, g_Config.m_ClRefreshRateInactive);
\t\tg_Config.m_ClIosRuntimeProfile = 1;
\t}
#endif''')
replace_once('src/engine/shared/config_variables.h',
             '// iOS has neither a back button nor a keyboard to press the escape-key with.',
             '''MACRO_CONFIG_INT(ClIosRuntimeProfile, cl_ios_runtime_profile, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Applied iOS frame-rate profile version")
// iOS has neither a back button nor a keyboard to press the escape-key with.''')
lock_path = root / 'ELY_SOURCE_LOCK.json'
lock = json.loads(lock_path.read_text())
lock['runtime_fix_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
lock['runtime_header_sha256'] = hashlib.sha256((here / 'overlays/mobile_runtime.h').read_bytes()).hexdigest()
lock_path.write_text(json.dumps(lock, indent=2) + '\n')
print('Applied touch release dispatch, overlay capture and one-time iOS frame profile')
