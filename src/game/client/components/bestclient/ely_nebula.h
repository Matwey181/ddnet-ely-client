// Adapted from SoundVisualizer Nebula / SVBgUtil.rng, MIT.
// Copyright (c) 2026 Çağan Turgut (CaYatur) — CaYaDev, https://cayadev.com.
// https://github.com/CaYatur/SoundVisualizer/tree/c3e7fc6bc3fa826e323486256268fa764a317963
// Full license and original sources: data/ely/third_party/soundvisualizer/.
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_ELY_NEBULA_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_ELY_NEBULA_H
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace ElyNebula
{
struct SCloud { float m_X, m_Y, m_Radius, m_Speed, m_Phase, m_Tone; };
inline const std::array<SCloud, 12> &Clouds()
{
	static const auto Result = [] {
		std::array<SCloud, 12> Clouds{};
		uint32_t State = 0x4d21u;
		const auto Random = [&]() {
			State ^= State << 13;
			// JS >> is arithmetic; keep the upstream seeded layout reproducible.
			State ^= (State >> 17) | ((State & 0x80000000u) ? 0xffff8000u : 0u);
			State ^= State << 5;
			return (float)(State / 4294967296.0);
		};
		for(auto &Cloud : Clouds)
			Cloud = {Random(), Random(), .25f + Random() * .5f, .2f + Random() * .6f, Random() * 6.2831853f, Random()};
		return Clouds;
	}();
	return Result;
}
inline float X(const SCloud &Cloud, float Age)
{
	return std::clamp(Cloud.m_X + std::sin(Age * .45f * Cloud.m_Speed * .6f + Cloud.m_Phase) * .16f, .04f, .96f);
}
inline float Y(const SCloud &Cloud, float Age)
{
	return std::clamp(Cloud.m_Y + std::cos(Age * .45f * Cloud.m_Speed * .47f + Cloud.m_Phase * 1.7f) * .14f, .04f, .96f);
}
// SoundVisualizer's three radial color stops, widened for Ely's soft-light reference.
inline float Gradient(float Distance)
{
	constexpr float Middle = .624f; // .28 * softness(1.8) + .12
	if(Distance >= 1) return 0;
	return Distance <= Middle ? 1.0f - .64f * Distance / Middle : .36f * (1 - Distance) / (1 - Middle);
}
// Small Gaussian quadrature softens both the center and the outer gradient stop.
inline float SoftGradient(float X, float Y)
{
	constexpr float Offsets[] = {-.095f, 0, .095f};
	constexpr float Weights[] = {.25f, .5f, .25f};
	float Result = 0;
	for(int j=0;j<3;++j) for(int i=0;i<3;++i)
		Result += Weights[i] * Weights[j] * Gradient(std::hypot(X + Offsets[i], Y + Offsets[j]));
	return Result;
}
}
#endif
