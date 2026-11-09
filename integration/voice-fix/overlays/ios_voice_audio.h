#ifndef IOS_VOICE_AUDIO_H
#define IOS_VOICE_AUDIO_H
#include <SDL_audio.h>
SDL_AudioDeviceID IosVoiceOpenAudio(const char *pDevice, int Capture, const SDL_AudioSpec *pDesired, SDL_AudioSpec *pObtained, int Changes);
void IosVoiceCloseAudio(SDL_AudioDeviceID Device);
Uint32 IosVoiceQueuedAudio(SDL_AudioDeviceID Device);
void IosVoiceClearAudio(SDL_AudioDeviceID Device);
Uint32 IosVoiceDequeueAudio(SDL_AudioDeviceID Device, void *pData, Uint32 Length);
int IosVoiceQueueAudio(SDL_AudioDeviceID Device, const void *pData, Uint32 Length);
#endif
