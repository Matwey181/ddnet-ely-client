from pathlib import Path
import hashlib, json, shutil, sys
root=Path(sys.argv[1]); here=Path(__file__).resolve().parent
for p in (here/'overlays').iterdir(): shutil.copyfile(p,root/'src/ios'/p.name)
p=root/'CMakeLists.txt'; s=p.read_text(); old='    src/ios/ios_performance.mm'; assert s.count(old)==1;p.write_text(s.replace(old,old+'\n    src/ios/ios_voice_audio.cpp',1))
p=root/'src/game/client/components/bestclient/voice/voice.cpp';s=p.read_text(); anchor='namespace\n{';assert s.count(anchor)>=1
s=s.replace(anchor,'''#if defined(CONF_PLATFORM_IOS)
#include <ios/ios_voice_audio.h>
// iOS queued-audio functions take the device mutex. Use a callback-fed SPSC
// bridge so a slow audio callback cannot stall every game frame.
#define SDL_OpenAudioDevice IosVoiceOpenAudio
#define SDL_CloseAudioDevice IosVoiceCloseAudio
#define SDL_GetQueuedAudioSize IosVoiceQueuedAudio
#define SDL_ClearQueuedAudio IosVoiceClearAudio
#define SDL_DequeueAudio IosVoiceDequeueAudio
#define SDL_QueueAudio IosVoiceQueueAudio
#endif

'''+anchor,1)
old='\twhile(SDL_GetQueuedAudioSize(m_PlaybackDevice) < TargetBytes)';assert s.count(old)==1
s=s.replace(old,'''#if defined(CONF_PLATFORM_IOS)
 // Audio may be consumed while this loop runs. Bound work even after a flush.
 for(int Frame = 0; Frame < 3 && SDL_GetQueuedAudioSize(m_PlaybackDevice) < TargetBytes; ++Frame)
#else
'''+old+'\n#endif',1);p.write_text(s)
# Keep stage timings in the same shareable log if another voice operation stalls.
p=root/'src/ios/ios_performance.mm';s=p.read_text();s=s.replace('std::array<int64_t, 4> m_Phases{};', 'std::array<int64_t, 8> m_Phases{};');s=s.replace('worst_update=%s:%.2f worst_render=%s:%.2f', 'worst_update=%s:%.2f worst_render=%s:%.2f voice_net=%.2f voice_capture=%.2f voice_playback=%.2f voice_devices=%.2f');old='S.m_Names[1].c_str(), S.m_Worst[1] / 1e6);';assert s.count(old)==1;s=s.replace(old,'S.m_Names[1].c_str(), S.m_Worst[1] / 1e6, S.m_Phases[4] / 1e6 / S.m_Frames, S.m_Phases[5] / 1e6 / S.m_Frames, S.m_Phases[6] / 1e6 / S.m_Frames, S.m_Phases[7] / 1e6 / S.m_Frames);');p.write_text(s)
p=root/'src/game/client/components/bestclient/voice/voice.cpp';s=p.read_text();old='#include <ios/ios_voice_audio.h>';s=s.replace(old,old+"\n#include <ios/ios_performance.h>\n#include <base/time.h>\nclass CIosVoiceStage\n{\n int m_Stage; int64_t m_Start;\npublic:\n explicit CIosVoiceStage(int Stage) : m_Stage(Stage), m_Start(time_get_nanoseconds().count()) {}\n ~CIosVoiceStage() { IosPerfPhase(m_Stage, time_get_nanoseconds().count() - m_Start); }\n};")
for Function,Stage in [('ProcessNetwork',4),('ProcessCapture',5),('ProcessPlayback',6),('OpenAudioDevices',7),('CloseAudioDevices',7)]:
 prefix=('bool' if Function=='OpenAudioDevices' else 'void')+' CVoiceChat::'+Function+'()\n{';assert s.count(prefix)==1;s=s.replace(prefix,prefix+'\n#if defined(CONF_PLATFORM_IOS)\n CIosVoiceStage Stage('+str(Stage)+');\n#endif',1)
p.write_text(s)
p=root/'ELY_SOURCE_LOCK.json';d=json.loads(p.read_text());d['voice_fix_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest();d['voice_overlays']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (here/'overlays').iterdir()};p.write_text(json.dumps(d,indent=2)+'\n')
print('Applied nonblocking iOS voice audio bridge and bounded playback work')
