#include <ios/ios_voice_audio.h>
#include <ios/ios_voice_ring.h>
#include <SDL_error.h>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <unordered_map>

static SDL_AudioDeviceID Next = 1;
static std::unordered_map<SDL_AudioDeviceID, SDL_AudioSpec> Specs;
extern "C" SDL_AudioDeviceID SDL_OpenAudioDevice(const char *, int, const SDL_AudioSpec *Desired, SDL_AudioSpec *Obtained, int)
{
 assert(Desired->callback && Desired->userdata);
 *Obtained = *Desired;
 Specs[Next] = *Desired;
 return Next++;
}
extern "C" void SDL_CloseAudioDevice(SDL_AudioDeviceID Id) { assert(Specs.erase(Id) == 1); }
extern "C" int SDL_SetError(const char *, ...) { return -1; }
// Any call to SDL's mutex-taking queued API is a regression in this bridge.
extern "C" Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID) { assert(false); return 0; }
extern "C" void SDL_ClearQueuedAudio(SDL_AudioDeviceID) { assert(false); }
extern "C" Uint32 SDL_DequeueAudio(SDL_AudioDeviceID, void *, Uint32) { assert(false); return 0; }
extern "C" int SDL_QueueAudio(SDL_AudioDeviceID, const void *, Uint32) { assert(false); return -1; }

int main()
{
 SDL_AudioSpec Desired{}, Obtained{};
 auto Capture = IosVoiceOpenAudio(nullptr, 1, &Desired, &Obtained, 0);
 Uint8 In[32], Out[64];
 for(int i=0;i<32;++i) In[i] = i;
 Specs[Capture].callback(Specs[Capture].userdata, In, 32);
 assert(IosVoiceQueuedAudio(Capture) == 32);
 assert(IosVoiceDequeueAudio(Capture, Out, 11) == 11);
 for(int i=0;i<11;++i) assert(Out[i] == In[i]);
 IosVoiceClearAudio(Capture);
 assert(IosVoiceQueuedAudio(Capture) == 0);
 IosVoiceCloseAudio(Capture);
 auto Playback = IosVoiceOpenAudio(nullptr, 0, &Desired, &Obtained, 0);
 assert(IosVoiceQueueAudio(Playback, In, 32) == 0);
 Specs[Playback].callback(Specs[Playback].userdata, Out, 64);
 for(int i=0;i<64;++i) assert(Out[i] == (i<32 ? In[i] : 0));
 assert(IosVoiceQueuedAudio(Playback) == 0);
 assert(IosVoiceQueueAudio(Playback, In, 32) == 0);
 IosVoiceClearAudio(Playback);
 Specs[Playback].callback(Specs[Playback].userdata, Out, 64);
 for(auto Byte:Out) assert(Byte == 0);
 // A stopped audio callback must not make game-thread queue calls wait.
 const auto Start = std::chrono::steady_clock::now();
 for(int i=0;i<100000;++i) { IosVoiceQueueAudio(Playback, In, 32); IosVoiceQueuedAudio(Playback); }
 assert(std::chrono::steady_clock::now()-Start < std::chrono::seconds(2));
 IosVoiceCloseAudio(Playback);
 assert(Specs.empty());
 CIosVoiceRing<1024> Ring;
 std::thread Producer([&](){ for(uint32_t i=0;i<1000000;++i) { while(Ring.Size()>1020) std::this_thread::yield(); assert(Ring.Push(&i,4)==4); } });
 for(uint32_t i=0;i<1000000;++i) { uint32_t Value=0; while(Ring.Size()<4) std::this_thread::yield(); assert(Ring.Pop(&Value,4)==4); assert(Value==i); }
 Producer.join(); assert(Ring.Size()==0);
 // Wrap, overflow and consumer flush keep bounded storage and sample order.
 std::vector<uint8_t> Big(2048, 42), Copy(1024);
 assert(Ring.Push(Big.data(),Big.size())==1024);
 assert(Ring.Push(Big.data(),4)==0);
 assert(Ring.Pop(Copy.data(),700)==700);
 assert(Ring.Push(Big.data(),700)==700);
 Ring.Discard(); assert(Ring.Size()==0);
 std::cout << "Passed capture/playback, silence, flush, stalled callback, bounded buffers and one million concurrent samples\n";
}
