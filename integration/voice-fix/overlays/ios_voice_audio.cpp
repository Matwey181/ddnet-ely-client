#include "ios_voice_audio.h"
#include "ios_voice_ring.h"
#include <SDL_error.h>
#include <memory>
#include <unordered_map>

namespace
{
struct SAudio
{
 bool m_Capture;
 CIosVoiceRing<65536> m_Bytes;
 std::atomic<bool> m_Clear{false};
 explicit SAudio(bool Capture) : m_Capture(Capture) {}
};
// This map is owned by the game thread. Callbacks use their stable userdata
// directly and never access the map. SDL_CloseAudioDevice joins the callback
// before its userdata is destroyed.
std::unordered_map<SDL_AudioDeviceID, std::unique_ptr<SAudio>> gs_Devices;
SAudio *Find(SDL_AudioDeviceID Device)
{
 const auto It = gs_Devices.find(Device);
 return It == gs_Devices.end() ? nullptr : It->second.get();
}
void SDLCALL AudioCallback(void *pUser, Uint8 *pStream, int Length)
{
 auto *pAudio = static_cast<SAudio *>(pUser);
 if(Length <= 0) return;
 if(pAudio->m_Capture)
  pAudio->m_Bytes.Push(pStream, static_cast<size_t>(Length));
 else
 {
  if(pAudio->m_Clear.exchange(false, std::memory_order_acq_rel)) pAudio->m_Bytes.Discard();
  std::memset(pStream, 0, Length);
  pAudio->m_Bytes.Pop(pStream, static_cast<size_t>(Length));
 }
}
}
SDL_AudioDeviceID IosVoiceOpenAudio(const char *pDevice, int Capture, const SDL_AudioSpec *pDesired, SDL_AudioSpec *pObtained, int Changes)
{
 auto pAudio = std::make_unique<SAudio>(Capture != 0);
 SDL_AudioSpec Desired = *pDesired;
 Desired.callback = AudioCallback;
 Desired.userdata = pAudio.get();
 const auto Device = SDL_OpenAudioDevice(pDevice, Capture, &Desired, pObtained, Changes);
 if(Device) gs_Devices.emplace(Device, std::move(pAudio));
 return Device;
}
void IosVoiceCloseAudio(SDL_AudioDeviceID Device)
{
 SDL_CloseAudioDevice(Device);
 gs_Devices.erase(Device);
}
Uint32 IosVoiceQueuedAudio(SDL_AudioDeviceID Device)
{
 auto *pAudio = Find(Device);
 return pAudio ? static_cast<Uint32>(pAudio->m_Bytes.Size()) : 0;
}
void IosVoiceClearAudio(SDL_AudioDeviceID Device)
{
 if(auto *pAudio = Find(Device))
 {
  if(pAudio->m_Capture) pAudio->m_Bytes.Discard();
  else pAudio->m_Clear.store(true, std::memory_order_release);
 }
}
Uint32 IosVoiceDequeueAudio(SDL_AudioDeviceID Device, void *pData, Uint32 Length)
{
 auto *pAudio = Find(Device);
 return pAudio ? static_cast<Uint32>(pAudio->m_Bytes.Pop(pData, Length)) : 0;
}
int IosVoiceQueueAudio(SDL_AudioDeviceID Device, const void *pData, Uint32 Length)
{
 auto *pAudio = Find(Device);
 if(!pAudio) return SDL_SetError("Unknown iOS voice audio device");
 // A requested flush is handled by the consumer callback, never by the producer.
 if(pAudio->m_Clear.load(std::memory_order_acquire)) return 0;
 if(Length > 65536 - pAudio->m_Bytes.Size()) return SDL_SetError("iOS voice audio buffer full");
 return pAudio->m_Bytes.Push(pData, Length) == Length ? 0 : SDL_SetError("iOS voice audio buffer full");
}
