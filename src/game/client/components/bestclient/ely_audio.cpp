#include "ely_audio.h"
#include "ely_audio_analysis.h"
#include "ely_nebula.h"
#include "fx/fgf_glow.h"

#include <base/color.h>
#include <base/detect.h>
#include <base/log.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/shared/config.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#if defined(CONF_FAMILY_WINDOWS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOBITMAP
#define ELY_AUDIO_DEFINED_NOBITMAP
#define NOBITMAP
#endif
#define IStorage ElyAudioWindowsIStorage
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <wrl/client.h>
#undef IStorage
#if defined(ELY_AUDIO_DEFINED_NOBITMAP)
#undef ELY_AUDIO_DEFINED_NOBITMAP
#undef NOBITMAP
#endif
#endif

namespace ElyAudio
{
double MonotonicSeconds()
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

namespace
{
struct SWorkerState
{
	std::mutex m_Mutex;
	std::condition_variable m_Wake;
	std::thread m_Thread;
	bool m_Stop = false;
	bool m_Enabled = false;
	bool m_Playing = false;
	SSettings m_Settings;
	SSnapshot m_Snapshot;
	double m_FrameTime = 0.0;
	double m_LastOnset = -10.0;
	uint64_t m_OnsetSerial = 0;

	~SWorkerState()
	{
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_Stop = true;
		}
		m_Wake.notify_all();
		if(m_Thread.joinable())
			m_Thread.join();
	}
};

SWorkerState &WorkerState()
{
	static SWorkerState State;
	return State;
}

#if defined(CONF_FAMILY_WINDOWS)
using Microsoft::WRL::ComPtr;

class CLoopbackCapture
{
	ComPtr<IMMDeviceEnumerator> m_pEnumerator;
	ComPtr<IAudioClient> m_pClient;
	ComPtr<IAudioCaptureClient> m_pCapture;
	std::wstring m_DeviceId;
	int m_Channels = 0;
	int m_BytesPerSample = 0;
	int m_BlockAlign = 0;
	bool m_Floating = false;

	ComPtr<IMMDevice> DefaultDevice()
	{
		ComPtr<IMMDevice> Device;
		if(m_pEnumerator)
		{
			if(FAILED(m_pEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, Device.GetAddressOf())))
				m_pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, Device.ReleaseAndGetAddressOf());
		}
		return Device;
	}

	static std::wstring DeviceId(IMMDevice *pDevice)
	{
		LPWSTR pId = nullptr;
		if(!pDevice || FAILED(pDevice->GetId(&pId)))
			return {};
		const std::wstring Id(pId);
		CoTaskMemFree(pId);
		return Id;
	}

	float Decode(const BYTE *pSample) const
	{
		if(m_Floating)
		{
			if(m_BytesPerSample == 4)
			{
				float Value;
				std::memcpy(&Value, pSample, sizeof(Value));
				return std::isfinite(Value) ? std::clamp(Value, -1.0f, 1.0f) : 0.0f;
			}
			double Value;
			std::memcpy(&Value, pSample, sizeof(Value));
			return std::isfinite(Value) ? (float)std::clamp(Value, -1.0, 1.0) : 0.0f;
		}
		if(m_BytesPerSample == 1)
			return (pSample[0] - 128) / 128.0f;
		if(m_BytesPerSample == 2)
		{
			int16_t Value;
			std::memcpy(&Value, pSample, sizeof(Value));
			return Value / 32768.0f;
		}
		if(m_BytesPerSample == 3)
		{
			int Value = pSample[0] | (pSample[1] << 8) | (pSample[2] << 16);
			if(Value & 0x800000)
				Value -= 0x1000000;
			return Value / 8388608.0f;
		}
		int32_t Value;
		std::memcpy(&Value, pSample, sizeof(Value));
		return Value / 2147483648.0f;
	}

public:
	int m_SampleRate = 0;

	~CLoopbackCapture() { Close(); }

	void Close()
	{
		if(m_pClient)
			m_pClient->Stop();
		m_pCapture.Reset();
		m_pClient.Reset();
		m_DeviceId.clear();
	}

	bool Open()
	{
		Close();
		if(!m_pEnumerator && FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(m_pEnumerator.GetAddressOf()))))
			return false;
		auto Device = DefaultDevice();
		if(!Device || FAILED(Device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(m_pClient.GetAddressOf()))))
			return false;
		m_DeviceId = DeviceId(Device.Get());
		WAVEFORMATEX *pFormat = nullptr;
		if(FAILED(m_pClient->GetMixFormat(&pFormat)))
			return false;
		const auto FreeFormat = [](WAVEFORMATEX *pValue) { CoTaskMemFree(pValue); };
		const std::unique_ptr<WAVEFORMATEX, decltype(FreeFormat)> Format(pFormat, FreeFormat);
		WORD Tag = pFormat->wFormatTag;
		if(Tag == WAVE_FORMAT_EXTENSIBLE && pFormat->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
		{
			const GUID Pcm = {1, 0, 0x0010, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
			GUID SubFormat = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(pFormat)->SubFormat;
			const DWORD FormatTag = SubFormat.Data1;
			SubFormat.Data1 = 1;
			Tag = IsEqualGUID(SubFormat, Pcm) && (FormatTag == WAVE_FORMAT_PCM || FormatTag == WAVE_FORMAT_IEEE_FLOAT) ? (WORD)FormatTag : 0;
		}
		m_Floating = Tag == WAVE_FORMAT_IEEE_FLOAT;
		m_Channels = pFormat->nChannels;
		m_BytesPerSample = pFormat->wBitsPerSample / 8;
		m_BlockAlign = pFormat->nBlockAlign;
		m_SampleRate = pFormat->nSamplesPerSec;
		const bool ValidPcm = Tag == WAVE_FORMAT_PCM && (m_BytesPerSample >= 1 && m_BytesPerSample <= 4);
		const bool ValidFloat = m_Floating && (m_BytesPerSample == 4 || m_BytesPerSample == 8);
		if((!ValidPcm && !ValidFloat) || pFormat->wBitsPerSample % 8 != 0 || m_Channels < 1 || m_Channels > 32 ||
			m_BlockAlign < m_Channels * m_BytesPerSample || m_SampleRate < 8000 || m_SampleRate > 384000)
			return false;
		if(FAILED(m_pClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 1000000, 0, pFormat, nullptr)) ||
			FAILED(m_pClient->GetService(IID_PPV_ARGS(m_pCapture.GetAddressOf()))) || FAILED(m_pClient->Start()))
			return false;
		return true;
	}

	bool DeviceChanged()
	{
		return DeviceId(DefaultDevice().Get()) != m_DeviceId;
	}

	template<typename F>
	bool Drain(CSignalAnalyzer &Analyzer, const SSettings &Settings, F &&Publish)
	{
		UINT32 Pending = 0;
		if(FAILED(m_pCapture->GetNextPacketSize(&Pending)))
			return false;
		while(Pending != 0)
		{
			BYTE *pData = nullptr;
			UINT32 Frames = 0;
			DWORD Flags = 0;
			if(FAILED(m_pCapture->GetBuffer(&pData, &Frames, &Flags, nullptr, nullptr)))
				return false;
			for(UINT32 Frame = 0; Frame < Frames; ++Frame)
			{
				float Mono = 0.0f;
				if(pData && !(Flags & AUDCLNT_BUFFERFLAGS_SILENT))
				{
					for(int Channel = 0; Channel < m_Channels; ++Channel)
						Mono += Decode(pData + Frame * m_BlockAlign + Channel * m_BytesPerSample);
					Mono /= m_Channels;
				}
				if(Analyzer.Push(Mono))
					Publish(Analyzer.Analyze(Settings));
			}
			if(FAILED(m_pCapture->ReleaseBuffer(Frames)) || FAILED(m_pCapture->GetNextPacketSize(&Pending)))
				return false;
		}
		return true;
	}
};
#endif

void RunWorker(SWorkerState &State)
{
#if defined(CONF_FAMILY_WINDOWS)
	const HRESULT Init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if(FAILED(Init))
		return;
	{
		CLoopbackCapture Capture;
		std::unique_ptr<CSignalAnalyzer> Analyzer;
		double NextDeviceCheck = 0.0;
		double NextAttempt = 0.0;
		double NextLog = 0.0;
		for(;;)
		{
			SSettings Settings;
			bool Active;
			{
				std::lock_guard<std::mutex> Lock(State.m_Mutex);
				if(State.m_Stop)
					break;
				Active = State.m_Enabled && State.m_Playing;
				Settings = State.m_Settings;
			}
			const double Now = MonotonicSeconds();
			if(!Active)
			{
				Capture.Close();
				Analyzer.reset();
				std::unique_lock<std::mutex> Lock(State.m_Mutex);
				State.m_Snapshot = {};
				State.m_Wake.wait(Lock, [&] { return State.m_Stop || (State.m_Enabled && State.m_Playing); });
				continue;
			}
			if(Analyzer && Now >= NextDeviceCheck)
			{
				NextDeviceCheck = Now + 1.0;
				if(Capture.DeviceChanged())
				{
					Capture.Close();
					Analyzer.reset();
					NextAttempt = Now;
				}
			}
			if(!Analyzer && Now >= NextAttempt)
			{
				if(Capture.Open())
				{
					Analyzer = std::make_unique<CSignalAnalyzer>(Capture.m_SampleRate);
					NextDeviceCheck = Now + 1.0;
				}
				NextAttempt = Now + 1.0;
				std::lock_guard<std::mutex> Lock(State.m_Mutex);
				State.m_Snapshot = {};
				State.m_Snapshot.m_Available = Analyzer != nullptr;
			}
			if(Analyzer && !Capture.Drain(*Analyzer, Settings, [&](const CSignalAnalyzer::SFrame &Frame) {
				const double FrameTime = MonotonicSeconds();
				std::lock_guard<std::mutex> Lock(State.m_Mutex);
				if(!State.m_Enabled || !State.m_Playing) return;
				auto &Snapshot = State.m_Snapshot;
				Snapshot.m_Available = true;
				Snapshot.m_Audible = Frame.m_Rms > 0.0005f;
				Snapshot.m_Rms = Frame.m_Rms;
				Snapshot.m_aBands = Frame.m_aBands;
				const double ScheduledOnset = Frame.m_Onset ? ScheduleFlashOnset(FrameTime, State.m_LastOnset, Settings.m_IntervalMs) : -1;
				if(ScheduledOnset >= 0)
				{
					State.m_LastOnset = ScheduledOnset;
					Snapshot.m_OnsetSerial = ++State.m_OnsetSerial;
					Snapshot.m_OnsetTime = ScheduledOnset;
					Snapshot.m_OnsetStrength = Frame.m_Strength;
				}
				State.m_FrameTime = FrameTime;
			}))
			{
				Capture.Close();
				Analyzer.reset();
				std::lock_guard<std::mutex> Lock(State.m_Mutex);
				State.m_Snapshot = {};
				NextAttempt = Now + 1.0;
			}
			if(Settings.m_Debug && Now >= NextLog)
			{
				NextLog = Now + 2.0;
				SSnapshot Audio;
				{
					std::lock_guard<std::mutex> Lock(State.m_Mutex);
					Audio = State.m_Snapshot;
				}
				log_info("ely/audio", "loopback available=%d audible=%d rms=%.5f bands=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f onsets=%llu strength=%.3f", (int)Audio.m_Available, (int)Audio.m_Audible, Audio.m_Rms,
					Audio.m_aBands[0], Audio.m_aBands[1], Audio.m_aBands[2], Audio.m_aBands[3], Audio.m_aBands[4], Audio.m_aBands[5], Audio.m_aBands[6],
					(unsigned long long)Audio.m_OnsetSerial, Audio.m_OnsetStrength);
			}
			std::unique_lock<std::mutex> Lock(State.m_Mutex);
			State.m_Wake.wait_for(Lock, std::chrono::milliseconds(Settings.m_Quality == 0 ? 16 : 8), [&] { return State.m_Stop || !State.m_Enabled || !State.m_Playing; });
		}
	}
	CoUninitialize();
#else
	(void)State;
#endif
}

IGraphics::CTextureHandle NebulaTexture(IGraphics *pGraphics)
{
	// Broad, smooth light fields from the reference, without a visible circular boundary.
	static IGraphics *s_pTextureOwner = nullptr;
	static IGraphics::CTextureHandle s_CloudTexture;
	if(s_pTextureOwner != pGraphics || !s_CloudTexture.IsValid())
	{
		s_pTextureOwner = pGraphics;
		CImageInfo Image;
		Image.m_Width = Image.m_Height = 512;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.Allocate();
		for(int y = 0; y < 512; ++y)
			for(int x = 0; x < 512; ++x)
			{
				const float X = ((x + .5f) / 256.0f - 1.0f) * 1.6f, Y = ((y + .5f) / 256.0f - 1.0f) * 1.6f;
				uint8_t *pPixel = Image.m_pData + (y * 512 + x) * 4;
				pPixel[0] = pPixel[1] = pPixel[2] = 255;
				// Analytic blurred light: two overlapping Gaussian lobes, no radial stops/rim.
				const float Glow = .76f * std::exp(-4.2f * (X * X + Y * Y)) + .24f * std::exp(-3.5f * ((X - .24f) * (X - .24f) + (Y + .18f) * (Y + .18f)));
				const unsigned Hash = (x * 73856093u) ^ (y * 19349663u);
				const float Dither = (Hash & 255) / 255.0f;
				pPixel[3] = (uint8_t)std::clamp((int)(255.0f * Glow + Dither), 0, 255);
			}
		s_CloudTexture = pGraphics->LoadTextureRawMove(Image, 0, "ely_reference_light_cloud");
	}
	return s_CloudTexture;
}

struct SPulse
{
	double m_Start = -10.0;
	double m_Birth = -10.0;
	float m_Strength = 0.0f;
	float m_AttackBase = 0.0f;
	int m_Cloud = 0;
};
std::array<SPulse, 5> s_aPulses;
uint64_t s_LastOnsetSerial = 0;
double s_LastRenderOnset = -10.0;
} // namespace

void Update(bool Enabled, bool PlaybackActive, const SSettings &Settings)
{
	auto &State = WorkerState();
	{
		std::lock_guard<std::mutex> Lock(State.m_Mutex);
		State.m_Enabled = Enabled;
		State.m_Playing = PlaybackActive;
		State.m_Settings = Settings;
		if(!Enabled || !PlaybackActive)
			State.m_Snapshot = {};
#if !defined(CONF_PLATFORM_IOS)
		if(Enabled && !State.m_Thread.joinable())
		{
			State.m_Stop = false;
			State.m_Thread = std::thread([&State] { RunWorker(State); });
		}
#endif
	}
	State.m_Wake.notify_all();
}

SSettings ConfiguredSettings()
{
	if(g_Config.m_ElyAudioTimingVersion < 2)
	{
		if(g_Config.m_ElyFlashInterval == 500) g_Config.m_ElyFlashInterval = 120;
		if(g_Config.m_ElyFlashColor == 0x205578U) g_Config.m_ElyFlashColor = 0x2b998cU;
		if(g_Config.m_ElyFlashDecay == 1100) g_Config.m_ElyFlashDecay = 650;
		g_Config.m_ElyAudioTimingVersion = 2;
	}
	if(g_Config.m_ElyAudioTimingVersion < 3)
	{
		if(g_Config.m_ElyFlashInterval <= 120) g_Config.m_ElyFlashInterval = 320;
		if(g_Config.m_ElyFlashDecay == 650) g_Config.m_ElyFlashDecay = 480;
		g_Config.m_ElyFlashRegions = 1;
		g_Config.m_ElyAudioTimingVersion = 3;
	}
	SSettings Settings;
	Settings.m_Bass = g_Config.m_ElyFlashBass / 100.0f;
	Settings.m_Mid = g_Config.m_ElyFlashMid / 100.0f;
	Settings.m_High = g_Config.m_ElyFlashHigh / 100.0f;
	Settings.m_IntervalMs = std::max(180, g_Config.m_ElyFlashInterval);
	Settings.m_Quality = g_Config.m_ElyMusicQuality;
	Settings.m_Debug = g_Config.m_Debug != 0;
	return Settings;
}

SSnapshot Snapshot()
{
	auto &State = WorkerState();
	std::lock_guard<std::mutex> Lock(State.m_Mutex);
	auto Result = State.m_Snapshot;
	if(!State.m_Enabled || !State.m_Playing || MonotonicSeconds() - State.m_FrameTime > 0.25)
	{
		Result.m_Audible = false;
		Result.m_Rms = 0.0f;
		Result.m_aBands.fill(0.0f);
	}
	return Result;
}

void Shutdown()
{
	auto &State = WorkerState();
	{
		std::lock_guard<std::mutex> Lock(State.m_Mutex);
		State.m_Stop = true;
		State.m_Enabled = false;
		State.m_Snapshot = {};
	}
	State.m_Wake.notify_all();
	if(State.m_Thread.joinable())
		State.m_Thread.join();
	ResetFlashes();
}

void ResetFlashes()
{
	for(auto &Pulse : s_aPulses)
		Pulse = {};
	s_LastOnsetSerial = Snapshot().m_OnsetSerial;
}

void RenderFlashes(IGraphics *pGraphics)
{
	const auto Audio = Snapshot();
	if(!g_Config.m_ElyFlashEnabled || g_Config.m_ElyIslandReducedMotion || !Audio.m_Available)
	{
		ResetFlashes();
		return;
	}
	const auto CloudTexture = NebulaTexture(pGraphics); // prewarm before the first audio hit
	const double Now = MonotonicSeconds();
	const int Regions = 1; // One light envelope; accents never accumulate screen layers.
	const float Duration = std::clamp(g_Config.m_ElyFlashDecay, 200, 2000) / 1000.0f;
	if(Audio.m_Available && Audio.m_OnsetSerial != s_LastOnsetSerial)
	{
		s_LastOnsetSerial = Audio.m_OnsetSerial;
		if(Now - Audio.m_OnsetTime < 0.25)
		{
			const int Region = (Audio.m_OnsetSerial - 1) % Regions;
			auto &Pulse = s_aPulses[Region];
			const float OldAge = (float)(Audio.m_OnsetTime - Pulse.m_Start);
			const bool Active = OldAge >= 0 && OldAge < Duration;
			Pulse.m_AttackBase = Active ? ReattackEnvelope(OldAge, Duration, Pulse.m_AttackBase) : 0.0f;
			Pulse.m_Start = Audio.m_OnsetTime;
			const float OldGain = .35f + .65f * std::sqrt(Pulse.m_Strength);
			Pulse.m_Strength = Active ? std::max(Pulse.m_Strength, Audio.m_OnsetStrength) : Audio.m_OnsetStrength;
			const float NewGain = .35f + .65f * std::sqrt(Pulse.m_Strength);
			Pulse.m_AttackBase *= OldGain / NewGain;
			if(!Active) Pulse.m_Birth = Audio.m_OnsetTime;
			if(!Active) Pulse.m_Cloud = (Audio.m_OnsetSerial - 1) % ElyNebula::Clouds().size();
			s_LastRenderOnset = Audio.m_OnsetTime;
			if(g_Config.m_Debug) log_info("ely/flash", "render pulse region=%d strength=%.3f", Region, Audio.m_OnsetStrength);
		}
	}
	const float Limit = std::min(g_Config.m_ElyFlashBrightness / 100.0f, g_Config.m_ElyFlashContrast / 100.0f);
	const float Brightness = std::min(Limit, g_Config.m_ElyFlashSafeMode ? 0.18f : 0.25f) * (g_Config.m_ElyFlashIntensity / 100.0f);
	std::array<float, 5> aAlphas{};
	float TotalAlpha = 0.0f;
	for(int i = 0; i < Regions; ++i)
	{
		const float Age = Now - s_aPulses[i].m_Start;
		if(Age < 0.0f || Age >= Duration)
			continue;
		aAlphas[i] = Brightness * (0.35f + 0.65f * std::sqrt(s_aPulses[i].m_Strength)) * ReattackEnvelope(Age, Duration, s_aPulses[i].m_AttackBase);
		TotalAlpha += aAlphas[i];
	}
	if(TotalAlpha <= 0.00001f)
		return;
	// Preserve the chosen intensity at short intervals while bounding overlapping lights.
	const float AlphaScale = .85f; // No shared normalization: new lights never dim existing tails.
	const auto OldScreen = pGraphics->GetScreen();
	const float Height = 300.0f, Width = Height * pGraphics->ScreenAspect();
	pGraphics->MapScreen({0, 0, Width, Height});
	pGraphics->ClipDisable();
	pGraphics->BlendAdditive();
	pGraphics->TextureSet(CloudTexture);
	pGraphics->QuadsBegin();
	pGraphics->QuadsSetRotation(0.0f);
	pGraphics->QuadsSetSubset(0, 0, 1, 1);

	for(int i = 0; i < Regions; ++i)
	{
		if(aAlphas[i] <= 0.00001f)
			continue;
		const auto &Pulse = s_aPulses[i];
		// Screen-space placement measured from the supplied clip; motion is expansion only.
		constexpr vec2 aCenters[] = {{.28f,.26f},{.53f,.72f},{.74f,.31f},{.24f,.68f},{.65f,.48f}};
		const int Index = Pulse.m_Cloud % 5;
		const float Age = (float)(Now - Pulse.m_Birth);
		const float Expansion = 1.0f + .13f * (1.0f - std::exp(-Age * 5.0f));
		const float Spread = g_Config.m_ElyFlashRadius / 55.0f * Expansion;
		for(int Layer = 0; Layer < 1; ++Layer)
		{
			const vec2 Center = aCenters[(Index + Layer * 2) % 5];
			const ColorRGBA Palette = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ElyFlashMulticolor && (Index % 2) ? g_Config.m_ElyFlashSecondaryColor : g_Config.m_ElyFlashColor));
			const float Peak = std::max({Palette.r, Palette.g, Palette.b, .01f});
			pGraphics->SetColor(ColorRGBA(Palette.r / Peak, Palette.g / Peak, Palette.b / Peak, aAlphas[i] * AlphaScale * (Layer ? .70f : 1.0f)));
			IGraphics::CQuadItem Quad(Center.x * Width, Center.y * Height, Width * 1.65f * Spread, Height * 2.15f * Spread);
			pGraphics->QuadsDraw(&Quad, 1);
		}
	}
	pGraphics->QuadsEnd();
	pGraphics->TextureClear();
	pGraphics->BlendNormal();
	pGraphics->SetColor(1, 1, 1, 1);
	pGraphics->MapScreen(OldScreen);
}
} // namespace ElyAudio
