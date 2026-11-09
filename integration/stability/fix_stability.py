from pathlib import Path
import hashlib, json, shutil, sys
root = Path(sys.argv[1]); here = Path(__file__).resolve().parent

def edit(path, old, new):
 p=root/path; s=p.read_text(); assert s.count(old)==1, (path,s.count(old),old[:80]); p.write_text(s.replace(old,new,1))
for p in (here/'overlays').iterdir(): shutil.copyfile(p,root/'src/ios'/p.name)
edit('CMakeLists.txt','    src/ios/ios_music.mm','    src/ios/ios_music.mm\n    src/ios/ios_performance.mm')
edit('CMakeLists.txt','    set_source_files_properties(src/ios/ios_media.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")','    set_source_files_properties(src/ios/ios_media.mm src/ios/ios_music.mm src/ios/ios_performance.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")')
edit('src/ios/ios_main.h','#endif // IOS_IOS_MAIN_H','''// Bound autoreleased Foundation/UIKit temporaries to a single game-loop iteration.
void *IosFramePoolCreate();
void IosFramePoolDrain(void *pPool);
class CIosFramePool
{
 void *m_pPool;
public:
 CIosFramePool() : m_pPool(IosFramePoolCreate()) {}
 ~CIosFramePool() { IosFramePoolDrain(m_pPool); }
 CIosFramePool(const CIosFramePool &) = delete;
 CIosFramePool &operator=(const CIosFramePool &) = delete;
};
#endif // IOS_IOS_MAIN_H''')
edit('src/ios/ios_main.mm','#include <cmath>','''#include <cmath>
#import <Foundation/Foundation.h>
void *IosFramePoolCreate() { return [[NSAutoreleasePool alloc] init]; }
void IosFramePoolDrain(void *pPool) { [(NSAutoreleasePool *)pPool drain]; }''')
edit('src/engine/client/client.cpp','#include <ios/ios_main.h>','#include <ios/ios_main.h>\n#include <ios/ios_performance.h>')
edit('src/engine/client/client.cpp','\tint64_t NextUpdateTime = time_get();','''#if defined(CONF_PLATFORM_IOS)
 const bool ProfileFrame = true;
#else
 const bool ProfileFrame = false;
#endif
\tint64_t NextUpdateTime = time_get();''')
edit('src/engine/client/client.cpp','\t\tset_new_tick();','''#if defined(CONF_PLATFORM_IOS)
  CIosFramePool FramePool;
#endif
\t\tset_new_tick();''')
edit('src/engine/client/client.cpp','\t\tif(Input()->Update())','''#if defined(CONF_PLATFORM_IOS)
  const auto EventStart = time_get_nanoseconds().count();
#endif
  const bool QuitFromInput = Input()->Update();
#if defined(CONF_PLATFORM_IOS)
  IosPerfPhase(3, time_get_nanoseconds().count() - EventStart);
#endif
\t\tif(QuitFromInput)''')
for name in ['TimeUp0','TimeRender0','TimeRender1']:
 edit('src/engine/client/client.cpp',f'{name} = g_Config.m_Debug ?',f'{name} = (g_Config.m_Debug || ProfileFrame) ?')
edit('src/engine/client/client.cpp','\t\t\tUpdate();\n\t\t\tif(g_Config.m_Debug)','''\t\t\tUpdate();
#if defined(CONF_PLATFORM_IOS)
   IosPerfPhase(0, time_get_nanoseconds().count() - TimeUp0);
#endif
\t\t\tif(g_Config.m_Debug)''')
edit('src/engine/client/client.cpp','\t\t\t\tm_pGraphics->Swap();','''\t\t\t\tm_pGraphics->Swap();
#if defined(CONF_PLATFORM_IOS)
    IosPerfPhase(1, TimeRender1 - TimeRender0);
    IosPerfPhase(2, time_get_nanoseconds().count() - TimeRender1);
    IosPerfFrame(m_RenderFrameTime);
#endif''')
old='''\t\t\t\twhile(WaitTime > 0ns && net_socket_read_wait(m_aNetClient[CONN_MAIN].m_Socket, WaitTime > 1000us ? WaitTime / 2 : 0ns) == 0)
\t\t\t\t{
\t\t\t\t\tWaitTime = Deadline - time_get_nanoseconds();
\t\t\t\t}'''
edit('src/engine/client/client.cpp',old,'''#if defined(CONF_PLATFORM_IOS)
    // Sleep until the deadline or a packet; avoid busy-polling the final millisecond on mobile.
    if(WaitTime > 0ns) net_socket_read_wait(m_aNetClient[CONN_MAIN].m_Socket, WaitTime);
#else
'''+old+'''
#endif''')
edit('src/game/client/gameclient.cpp','#include "mobile_runtime.h"','''#include "mobile_runtime.h"
#if defined(CONF_PLATFORM_IOS)
#include <ios/ios_performance.h>
#endif''')
for method,render in [('OnUpdate','false'),('OnRender','true')]:
 anchor=f'\t\tpComponent->{method}();'
 # Reuse existing debug timestamp; force it on only for iOS.
 p=root/'src/game/client/gameclient.cpp'; s=p.read_text(); pos=s.index(anchor); begin=s.rfind('\t\tint64_t TimeStart = ',0,pos); end=s.index(';',begin)
 s=s[:begin]+s[begin:end].replace('g_Config.m_Debug ?', '(g_Config.m_Debug || ProfileIos) ?')+s[end:]; p.write_text(s)
 edit('src/game/client/gameclient.cpp',anchor,anchor+f'''
#if defined(CONF_PLATFORM_IOS)
  const auto ComponentNs = time_get_nanoseconds().count() - TimeStart;
  if(ComponentNs > 1000000) IosPerfComponent(GetComponentName(pComponent), {render}, ComponentNs);
#endif''')
edit('src/game/client/gameclient.cpp','#include <ios/ios_performance.h>','#include <ios/ios_performance.h>\nstatic constexpr bool ProfileIos = true;\n#else\nstatic constexpr bool ProfileIos = false;')
edit('src/game/client/components/menus_settings_touch.cpp','#include <ios/ios_music.h>','#include <ios/ios_music.h>\n#include <ios/ios_performance.h>')
edit('src/game/client/components/menus_settings_touch.cpp','Localize("Allow Apple Music access")','Localize("Music source")')
edit('src/game/client/components/menus_settings_touch.cpp','\t\tIosMusicAuthorize();','''\t\tIosMusicAuthorize();
 MainView.HSplitTop(8.0f, nullptr, &MainView);
 MainView.HSplitTop(30.0f, &Button, &MainView);
 pScrollRegion->AddRect(Button);
 static CButtonContainer s_PerfShare;
 if(DoButton_Menu(&s_PerfShare, Localize("Share performance log"), 0, &Button)) IosPerfShare();''')
edit('src/game/client/components/bestclient/music_player.cpp','Out.m_ServiceId = "ios.apple-music";','Out.m_ServiceId = "ios.now-playing";')
edit('src/game/client/components/bestclient/music_player.cpp','m_Cv.wait_for(Lock, std::chrono::milliseconds(125),','m_Cv.wait_for(Lock, std::chrono::milliseconds(\n#if defined(CONF_PLATFORM_IOS)\n500\n#else\n125\n#endif\n),')
lock=root/'ELY_SOURCE_LOCK.json'; data=json.loads(lock.read_text()); data['stability_fix_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(); data['stability_overlays']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (here/'overlays').iterdir()}; lock.write_text(json.dumps(data,indent=2)+'\n')
print('Applied per-frame iOS autorelease pool, non-spinning wait, performance capture and experimental system music source')
