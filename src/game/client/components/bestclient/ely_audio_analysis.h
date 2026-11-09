#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_ELY_AUDIO_ANALYSIS_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_ELY_AUDIO_ANALYSIS_H

#include "ely_audio.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace ElyAudio
{
// Onset detector ported from CaYatur/SoundVisualizer, src/shared/onset.js.
// Copyright (c) 2026 Çağan Turgut (CaYatur) — CaYaDev. SPDX-License-Identifier: MIT.
// Pinned source c3e7fc6bc3fa826e323486256268fa764a317963.
// Full license and original source are shipped in data/ely/third_party/soundvisualizer.
class COnsetDetector
{
	static constexpr double REFRACTORY = 0.11;
	static constexpr double TAU = 0.55;
	static constexpr double RATIO = 1.35;
	static constexpr double FLOOR = 0.35;
	static constexpr double GATE = 0.02;
	std::array<float, NUM_BANDS> m_aPrevious{};
	bool m_HasSpectrum = false;
	double m_Base = 0.0;
	double m_LevelBase = 0.0;
	double m_Since = REFRACTORY;
	double m_Energy = 0.0;
	double m_Last = 0.0;
	uint64_t m_Count = 0;

	static double Step(double Dt)
	{
		return std::clamp(!std::isfinite(Dt) || Dt == 0.0 ? 0.016 : Dt, 0.001, 0.25);
	}

public:
	double PushSpectrum(const std::array<float, NUM_BANDS> &Bands, double Dt)
	{
		const double Delta = Step(Dt);
		if(!m_HasSpectrum)
		{
			m_HasSpectrum = true;
			m_aPrevious = Bands;
			m_Since += Delta;
			return 0.0;
		}
		double Flux = 0.0, Sum = 0.0;
		for(int i = 0; i < NUM_BANDS; ++i)
		{
			const double Difference = (double)Bands[i] - m_aPrevious[i];
			if(Difference > 0.0)
				Flux += Difference;
			m_aPrevious[i] = Bands[i];
			Sum += Bands[i];
		}
		return PushFlux(Flux / Delta, Delta, Sum / NUM_BANDS);
	}

	double PushFlux(double Flux, double Dt, double Level)
	{
		const double Delta = Step(Dt);
		m_Since += Delta;
		m_Energy = std::max(0.0, m_Energy - Delta * 2.2);
		m_Base += (Flux - m_Base) * (1.0 - std::exp(-Delta / TAU));
		m_LevelBase += (Level - m_LevelBase) * (1.0 - std::exp(-Delta / 1.6));
		if(Level < GATE)
			return 0.0;
		const double Threshold = m_Base * RATIO + FLOOR;
		if(Flux <= Threshold || m_Since < REFRACTORY)
			return 0.0;
		m_Since = 0.0;
		++m_Count;
		const double Amplitude = std::clamp(Level - m_LevelBase, 0.0, 1.0);
		const double Overrun = std::clamp((Flux - Threshold) / (Threshold * 4.0 + 0.000001), 0.0, 1.0);
		m_Last = std::clamp(0.12 + 0.76 * Amplitude + 0.12 * Overrun, 0.0, 1.0);
		m_Energy = std::max(m_Energy, m_Last);
		return m_Last;
	}

	double Energy() const { return m_Energy; }
	uint64_t Count() const { return m_Count; }
};

// Measured from the detected audio onset: fast attack, continuous exponential release.
inline float FlashEnvelope(float Age, float Duration)
{
	if(Age < 0 || Age >= Duration) return 0;
	constexpr float Attack = .008f;
	const float Rise = std::clamp(Age / Attack, 0.0f, 1.0f);
	const float T = std::clamp((Age - Attack) / std::max(.001f, Duration - Attack), 0.0f, 1.0f);
	const float Tail = std::clamp((T - .8f) / .2f, 0.0f, 1.0f);
	return Rise * std::exp(-2.6f * T) * (1.0f - Tail * Tail * (3.0f - 2.0f * Tail));
}

// A new accent starts from the visible light level instead of adding another light.
inline float ReattackEnvelope(float Age, float Duration, float Base)
{
	if(Age < 0 || Age >= Duration) return 0;
	const float Rise = std::clamp(Age / .008f, 0.0f, 1.0f);
	return FlashEnvelope(Age, Duration) + std::clamp(Base, 0.0f, 1.0f) * (1.0f - Rise);
}
// Audio windows quantize detection. Schedule a slightly early candidate instead of dropping a beat.
inline double ScheduleFlashOnset(double Now, double Previous, int IntervalMs)
{
	const double Scheduled = std::max(Now, Previous + std::max(80, IntervalMs) / 1000.0);
	return Scheduled - Now <= .03 ? Scheduled : -1.0;
}
// Full-band volume accents, independent of rhythm and spectral changes.
class CLoudnessDetector
{
	float m_Average = 0.0f;
	float m_Peak = 0.0f;
	float m_Valley = 0.0f;
	float m_LocalPeak = 0.0f;
	bool m_Armed = true;
public:
	float Push(float Rms, float Dt)
	{
		Rms = std::isfinite(Rms) ? std::clamp(Rms, 0.0f, 1.0f) : 0.0f;
		Dt = std::clamp(Dt, .001f, .25f);
		// Rearm on a local dip, even when a compressed track stays loud overall.
		if(Rms < m_LocalPeak * .94f)
		{
			if(!m_Armed) m_Valley = Rms;
			m_Armed = true;
			m_LocalPeak = Rms;
		}
		if(m_Armed) m_Valley = std::min(m_Valley, Rms);
		const float Floor = std::max(.0015f, m_Peak * .12f);
		const bool Loud = m_Armed && Rms >= Floor && Rms >= m_Valley * 1.10f + .00035f;
		m_Average += (Rms - m_Average) * (1.0f - std::exp(-Dt / .65f));
		m_Peak = std::max(Rms, m_Peak * std::exp(-Dt / 1.2f));
		m_LocalPeak = std::max(m_LocalPeak, Rms);
		if(!Loud) return 0.0f;
		m_Armed = false;
		return std::clamp(.30f + .70f * Rms / std::max(.006f, m_Peak), 0.0f, 1.0f);
	}
};
// Pure streaming DSP, shared by the loopback worker and deterministic signal tests.
class CSignalAnalyzer
{
	static constexpr int FFT_SIZE = 2048;
	static constexpr int HOP_SIZE = 1024;
	static constexpr float PI = 3.14159265358979323846f;
	std::array<float, FFT_SIZE> m_aSamples{};
	std::array<float, FFT_SIZE> m_aWindow{};
	std::array<std::complex<float>, FFT_SIZE> m_aFft{};
	CLoudnessDetector m_Loudness;
	int m_SampleRate;
	int m_Write = 0;
	int m_Filled = 0;
	int m_SinceAnalysis = 0;

public:
	struct SFrame
	{
		float m_Rms = 0.0f;
		std::array<float, NUM_BANDS> m_aBands{};
		bool m_Onset = false;
		float m_Strength = 0.0f;
	};

	explicit CSignalAnalyzer(int SampleRate) :
		m_SampleRate(std::clamp(SampleRate, 8000, 384000))
	{
		for(int i = 0; i < FFT_SIZE; ++i)
			m_aWindow[i] = 0.5f - 0.5f * std::cos(2.0f * PI * i / (FFT_SIZE - 1));
	}

	bool Push(float Sample)
	{
		m_aSamples[m_Write] = std::isfinite(Sample) ? std::clamp(Sample, -1.0f, 1.0f) : 0.0f;
		m_Write = (m_Write + 1) % FFT_SIZE;
		m_Filled = std::min(FFT_SIZE, m_Filled + 1);
		++m_SinceAnalysis;
		if(m_Filled < FFT_SIZE || m_SinceAnalysis < HOP_SIZE)
			return false;
		m_SinceAnalysis = 0;
		return true;
	}

	SFrame Analyze(const SSettings &Settings)
	{
		SFrame Frame;
		float Squares = 0.0f;
		for(int i = 0; i < FFT_SIZE; ++i)
		{
			const float Value = m_aSamples[(m_Write + i) % FFT_SIZE];
			Squares += Value * Value;
			m_aFft[i] = {Value * m_aWindow[i], 0.0f};
		}
		Frame.m_Rms = std::sqrt(Squares / FFT_SIZE);
		for(int i = 1, j = 0; i < FFT_SIZE; ++i)
		{
			int Bit = FFT_SIZE / 2;
			for(; j & Bit; Bit >>= 1)
				j ^= Bit;
			j ^= Bit;
			if(i < j)
				std::swap(m_aFft[i], m_aFft[j]);
		}
		for(int Width = 2; Width <= FFT_SIZE; Width *= 2)
		{
			const float Angle = -2.0f * PI / Width;
			const std::complex<float> Step(std::cos(Angle), std::sin(Angle));
			for(int Start = 0; Start < FFT_SIZE; Start += Width)
			{
				std::complex<float> Phase(1.0f, 0.0f);
				for(int j = 0; j < Width / 2; ++j)
				{
					const auto Even = m_aFft[Start + j];
					const auto Odd = m_aFft[Start + j + Width / 2] * Phase;
					m_aFft[Start + j] = Even + Odd;
					m_aFft[Start + j + Width / 2] = Even - Odd;
					Phase *= Step;
				}
			}
		}

		constexpr float aEdges[] = {20, 80, 220, 520, 1200, 2800, 7000, 17000};
		std::array<float, NUM_BANDS> aWeightedBands{};
		for(int Band = 0; Band < NUM_BANDS; ++Band)
		{
			const int First = std::clamp((int)std::ceil(aEdges[Band] * FFT_SIZE / m_SampleRate), 1, FFT_SIZE / 2);
			const int Last = std::clamp((int)std::ceil(aEdges[Band + 1] * FFT_SIZE / m_SampleRate), First, FFT_SIZE / 2);
			float Power = 0.0f;
			for(int Bin = First; Bin < Last; ++Bin)
				Power += std::norm(m_aFft[Bin]);
			// Hann-window compensation and a fixed logarithmic display scale.
			const float Energy = std::sqrt(Power) * (4.0f / FFT_SIZE);
			Frame.m_aBands[Band] = std::clamp(std::log1p(Energy * 24.0f) / std::log(25.0f), 0.0f, 1.0f);
			const float Weight = std::clamp(Band < 2 ? Settings.m_Bass : Band < 5 ? Settings.m_Mid : Settings.m_High, 0.0f, 2.0f);
			aWeightedBands[Band] = Frame.m_aBands[Band] * Weight;
		}
		const float Gain = std::clamp((Settings.m_Bass + Settings.m_Mid + Settings.m_High) / 2.0f, 0.0f, 2.0f);
		const float Strength = m_Loudness.Push(Frame.m_Rms * Gain, HOP_SIZE / (float)m_SampleRate);
		Frame.m_Onset = Strength > 0.0 && Frame.m_Rms > 0.00025f;
		Frame.m_Strength = Frame.m_Onset ? (float)Strength : 0.0f;
		return Frame;
	}
};
} // namespace ElyAudio

#endif
