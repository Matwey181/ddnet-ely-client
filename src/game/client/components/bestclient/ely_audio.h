#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_ELY_AUDIO_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_ELY_AUDIO_H

#include <array>
#include <cstdint>

class IGraphics;

namespace ElyAudio
{
constexpr int NUM_BANDS = 7;

struct SSettings
{
	float m_Bass = 1.0f;
	float m_Mid = 0.65f;
	float m_High = 0.35f;
	int m_IntervalMs = 600;
	int m_Quality = 1;
	bool m_Debug = false;
};

struct SSnapshot
{
	bool m_Available = false;
	bool m_Audible = false;
	float m_Rms = 0.0f;
	std::array<float, NUM_BANDS> m_aBands{};
	uint64_t m_OnsetSerial = 0;
	double m_OnsetTime = 0.0;
	float m_OnsetStrength = 0.0f;
};

// Call from the client thread. Capture, device discovery and DSP run on a worker.
void Update(bool Enabled, bool PlaybackActive, const SSettings &Settings = {});
SSettings ConfiguredSettings();
SSnapshot Snapshot();
double MonotonicSeconds();
void Shutdown();

// Render only in the game-map background pass, before the Tee and HUD components.
void RenderFlashes(IGraphics *pGraphics);
void ResetFlashes();
} // namespace ElyAudio

#endif
