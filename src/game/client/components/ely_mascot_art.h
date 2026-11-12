#ifndef GAME_CLIENT_COMPONENTS_ELY_MASCOT_ART_H
#define GAME_CLIENT_COMPONENTS_ELY_MASCOT_ART_H

#include <engine/image.h>

#include <algorithm>
#include <array>
#include <cmath>

// Build the wink from the original texture in memory. The eye interior is filled
// from both adjacent cheeks, at the same local height, preserving the face's
// existing light falloff and hue. No flat patch or additional raster is used.
inline void ElyMascotCloseEye(CImageInfo &Image)
{
	if(!Image.m_pData || Image.m_Width < 64 || Image.m_Height < 64 ||
		(Image.m_Format != CImageInfo::FORMAT_RGB && Image.m_Format != CImageInfo::FORMAT_RGBA))
		return;
	const int Channels = Image.m_Format == CImageInfo::FORMAT_RGBA ? 4 : 3;
	auto *pPixels = static_cast<unsigned char *>(Image.m_pData);
	const float ScaleX = Image.m_Width / 1224.0f;
	const float ScaleY = Image.m_Height / 1285.0f;
	constexpr float CenterX = 1038.0f, CenterY = 1014.0f;
	constexpr float RadiusX = 48.0f, RadiusY = 113.0f;
	const float Cos = std::cos(0.28f), Sin = std::sin(0.28f);
	const auto Sample = [&](float U, float V) {
		const float X = (CenterX + U * Cos - V * Sin) * ScaleX;
		const float Y = (CenterY + U * Sin + V * Cos) * ScaleY;
		std::array<float, 3> Color{};
		// The small local average suppresses texture noise along the healed edge.
		for(int Dy = -1; Dy <= 1; ++Dy)
			for(int Dx = -1; Dx <= 1; ++Dx)
			{
				const int Px = std::clamp((int)std::round(X) + Dx, 0, (int)Image.m_Width - 1);
				const int Py = std::clamp((int)std::round(Y) + Dy, 0, (int)Image.m_Height - 1);
				const auto *pPixel = pPixels + ((size_t)Py * Image.m_Width + Px) * Channels;
				for(int C = 0; C < 3; ++C) Color[C] += pPixel[C] / 9.0f;
			}
		return Color;
	};
	// Capture the real eye's dark tone before repairing its area.
	const std::array<float, 3> Ink = Sample(0, 0);
	const float ExtentX = RadiusX * Cos + RadiusY * Sin + 13.0f;
	const float ExtentY = RadiusX * Sin + RadiusY * Cos + 13.0f;
	const int Left = std::max(0, (int)std::floor((CenterX - ExtentX) * ScaleX));
	const int Right = std::min((int)Image.m_Width, (int)std::ceil((CenterX + ExtentX) * ScaleX));
	const int Top = std::max(0, (int)std::floor((CenterY - ExtentY) * ScaleY));
	const int Bottom = std::min((int)Image.m_Height, (int)std::ceil((CenterY + ExtentY) * ScaleY));
	for(int Y = Top; Y < Bottom; ++Y)
		for(int X = Left; X < Right; ++X)
		{
			const float Dx = (X + 0.5f) / ScaleX - CenterX;
			const float Dy = (Y + 0.5f) / ScaleY - CenterY;
			const float U = Dx * Cos + Dy * Sin, V = -Dx * Sin + Dy * Cos;
			const float R = std::sqrt(U * U / (RadiusX * RadiusX) + V * V / (RadiusY * RadiusY));
			const float Fill = std::clamp((1.065f - R) / 0.065f, 0.0f, 1.0f);
			if(Fill <= 0) continue;
			// Samples always lie outside the feathered repair ellipse, so this
			// in-place pass never samples pixels written by an earlier iteration.
			const float Row = std::clamp(V / (RadiusY + 10.0f), -1.0f, 1.0f);
			const float Side = (RadiusX + 10.0f) * std::sqrt(std::max(0.0f, 1.0f - Row * Row)) + 10.0f;
			const auto A = Sample(-Side, V);
			const auto B = Sample(Side, V);
			const float T = std::clamp((U + Side) / (2.0f * Side), 0.0f, 1.0f);
			auto *pPixel = pPixels + ((size_t)Y * Image.m_Width + X) * Channels;
			for(int C = 0; C < 3; ++C)
			{
				const float Face = A[C] + (B[C] - A[C]) * T;
				pPixel[C] = (unsigned char)std::clamp((int)std::round(pPixel[C] + (Face - pPixel[C]) * Fill), 0, 255);
			}
		}
	// A tapered, anti-aliased curved lid closes the eye rather than covering it.
	for(int Y = Top; Y < Bottom; ++Y)
		for(int X = Left; X < Right; ++X)
		{
			const float Dx = (X + 0.5f) / ScaleX - CenterX;
			const float Dy = (Y + 0.5f) / ScaleY - CenterY;
			const float U = Dx * Cos + Dy * Sin, V = -Dx * Sin + Dy * Cos;
			const float P = U / 43.0f;
			if(std::abs(P) >= 1.0f) continue;
			const float Taper = std::sqrt(std::max(0.0f, 1.0f - P * P));
			const float LidY = 4.0f + 5.0f * (1.0f - P * P);
			const float Slope = -10.0f * U / (43.0f * 43.0f);
			const float Distance = std::abs(V - LidY) / std::sqrt(1.0f + Slope * Slope);
			const float Coverage = std::clamp((1.3f + 2.4f * Taper - Distance) / 0.9f, 0.0f, 1.0f) * Taper;
			if(Coverage <= 0) continue;
			auto *pPixel = pPixels + ((size_t)Y * Image.m_Width + X) * Channels;
			for(int C = 0; C < 3; ++C)
				pPixel[C] = (unsigned char)std::clamp((int)std::round(pPixel[C] + (Ink[C] - pPixel[C]) * Coverage), 0, 255);
		}
	// Alpha is intentionally unchanged for both the repaired face and the lid.
}

#endif
