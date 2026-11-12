#ifndef GAME_CLIENT_COMPONENTS_ELY_VECTOR_MASCOT_H
#define GAME_CLIENT_COMPONENTS_ELY_VECTOR_MASCOT_H

#include <engine/graphics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>

namespace ElyVectorArt
{
struct CMesh
{
	std::array<vec2, 160> m_aPoints{};
	std::array<unsigned short, 480> m_aIndices{};
	int m_Count = 0, m_Indices = 0;
	void Point(vec2 P) { if(m_Count < 160) m_aPoints[m_Count++] = P; }
	void Curve(vec2 A, vec2 B, vec2 End)
	{
		const vec2 Start = m_aPoints[m_Count - 1];
		for(int I = 1; I <= 20; ++I)
		{
			const float T = I / 20.0f, U = 1 - T;
			Point(Start * (U * U * U) + A * (3 * U * U * T) + B * (3 * U * T * T) + End * (T * T * T));
		}
	}
	static float Cross(vec2 A, vec2 B) { return A.x * B.y - A.y * B.x; }
	void Triangulate()
	{
		if(m_Count < 3) return;
		std::array<int, 160> Order{};
		float Area = 0;
		for(int I = 0; I < m_Count; ++I) Area += Cross(m_aPoints[I], m_aPoints[(I + 1) % m_Count]);
		for(int I = 0; I < m_Count; ++I) Order[I] = Area > 0 ? I : m_Count - 1 - I;
		int Remaining = m_Count;
		while(Remaining > 2)
		{
			bool Found = false;
			for(int I = 0; I < Remaining; ++I)
			{
				const int IA = Order[(I + Remaining - 1) % Remaining], IB = Order[I], IC = Order[(I + 1) % Remaining];
				const vec2 A = m_aPoints[IA], B = m_aPoints[IB], C = m_aPoints[IC];
				if(Cross(B - A, C - B) <= 0.0000001f) continue;
				bool Inside = false;
				for(int J = 0; J < Remaining; ++J)
				{
					const int V = Order[J];
					if(V == IA || V == IB || V == IC) continue;
					const vec2 P = m_aPoints[V];
					if(Cross(B - A, P - A) >= -0.0000001f && Cross(C - B, P - B) >= -0.0000001f && Cross(A - C, P - C) >= -0.0000001f) { Inside = true; break; }
				}
				if(Inside) continue;
				m_aIndices[m_Indices++] = (unsigned short)IA;
				m_aIndices[m_Indices++] = (unsigned short)IB;
				m_aIndices[m_Indices++] = (unsigned short)IC;
				for(int J = I; J < Remaining - 1; ++J) Order[J] = Order[J + 1];
				--Remaining; Found = true; break;
			}
			if(!Found) break;
		}
	}
};
inline const CMesh &Body()
{
	static const CMesh Mesh = [] {
		CMesh M; M.Point(vec2(-.32f, -.12f));
		M.Curve(vec2(-.27f, -.28f), vec2(-.05f, -.34f), vec2(.16f, -.32f));
		M.Curve(vec2(.33f, -.30f), vec2(.37f, -.17f), vec2(.37f, .02f));
		M.Curve(vec2(.37f, .16f), vec2(.32f, .27f), vec2(.25f, .30f));
		M.Curve(vec2(.03f, .35f), vec2(-.16f, .29f), vec2(-.28f, .24f));
		M.Curve(vec2(-.34f, .20f), vec2(-.36f, .02f), vec2(-.32f, -.12f));
		--M.m_Count; M.Triangulate(); return M;
	}(); return Mesh;
}
inline const CMesh &Brow()
{
	static const CMesh Mesh = [] {
		CMesh M; M.Point(vec2(-.31f, -.10f));
		M.Curve(vec2(-.12f, -.29f), vec2(.14f, -.38f), vec2(.27f, -.28f));
		M.Curve(vec2(.32f, -.24f), vec2(.35f, -.18f), vec2(.37f, -.12f));
		M.Curve(vec2(.24f, -.23f), vec2(-.03f, -.17f), vec2(-.31f, -.10f));
		--M.m_Count; M.Triangulate(); return M;
	}(); return Mesh;
}
inline const CMesh &Crown()
{
	static const CMesh Mesh = [] {
		CMesh M; M.Point(vec2(-.42f, -.44f)); M.Point(vec2(-.12f, -.29f));
		M.Curve(vec2(.02f, -.37f), vec2(.15f, -.43f), vec2(.26f, -.36f));
		M.Curve(vec2(.32f, -.31f), vec2(.35f, -.22f), vec2(.37f, -.12f));
		M.Curve(vec2(.25f, -.35f), vec2(-.04f, -.23f), vec2(-.21f, -.09f));
		M.Curve(vec2(-.28f, -.03f), vec2(-.32f, .04f), vec2(-.35f, .10f));
		M.Point(vec2(-.49f, -.055f)); M.Point(vec2(-.29f, -.15f));
		M.Triangulate(); return M;
	}(); return Mesh;
}
}

// Pose angles are radians; Look is a normalized gaze direction. Every layer shares
// one pivot and projection. Including orbit and star, the complete artwork stays
// within +/-0.70 Size horizontally and +/-0.62 Size vertically for every pose.
inline void DrawElyVectorMascot(IGraphics *pGraphics, vec2 Center, float Size, float Yaw, float Tilt, float Wink, float Alpha, vec2 Look)
{
	if(!pGraphics || Size <= 0 || Alpha <= 0) return;
	using namespace ElyVectorArt;
	constexpr float Pi = 3.14159265358979323846f;
	const float Turn = std::clamp(Yaw, -1.15f, 1.15f), Cy = std::cos(Turn), Sy = std::sin(Turn);
	const float Ct = std::cos(Tilt), St = std::sin(Tilt);
	const float A = std::clamp(Alpha, 0.0f, 1.0f), Close = std::clamp(Wink, 0.0f, 1.0f);
	const auto Project = [&](vec2 P, float Depth) {
		const vec2 Q(P.x * Cy + Depth * Sy, P.y + P.x * Sy * .035f);
		return Center + vec2(Q.x * Ct - Q.y * St, Q.x * St + Q.y * Ct) * (Size * .92f);
	};
	const auto Gray = [&](float G, float Opacity = 1.0f) { G = std::clamp(G, 0.0f, 1.0f); return ColorRGBA(G, G, G, A * Opacity); };
	pGraphics->TextureClear(); pGraphics->QuadsBegin();
	const auto Triangle = [&](vec2 P, vec2 Q, vec2 R, float Z, float GP, float GQ, float GR, float Opacity = 1.0f) {
		pGraphics->SetColor4(Gray(GP, Opacity), Gray(GQ, Opacity), Gray(GR, Opacity), Gray(GR, Opacity));
		const IGraphics::CFreeformItem Quad(Project(P, Z), Project(Q, Z), Project(R, Z), Project(R, Z));
		pGraphics->QuadsDrawFreeform(&Quad, 1);
	};
	const auto DrawMesh = [&](const CMesh &M, float Z, float Top, float Bottom) {
		for(int I = 0; I < M.m_Indices; I += 3)
		{
			const vec2 P = M.m_aPoints[M.m_aIndices[I]], Q = M.m_aPoints[M.m_aIndices[I + 1]], R = M.m_aPoints[M.m_aIndices[I + 2]];
			const auto Shade = [&](vec2 V) { const float T = std::clamp((V.y + .40f) / .74f + V.x * .16f, 0.0f, 1.0f); return Top + (Bottom - Top) * T; };
			Triangle(P, Q, R, Z, Shade(P), Shade(Q), Shade(R));
		}
	};
	const auto OrbitPoint = [&](float T, float RadiusOffset) {
		const vec2 V((.585f + RadiusOffset) * std::cos(T), (.170f + RadiusOffset) * std::sin(T));
		constexpr float C = .9393727f, S = -.3428978f;
		return vec2(V.x * C - V.y * S, V.x * S + V.y * C);
	};
	const auto Ring = [&](float Start, float End, bool Front) {
		for(int I = 0; I < 64; ++I)
		{
			const float T0 = Start + (End - Start) * I / 64, T1 = Start + (End - Start) * (I + 1) / 64;
			const float W0 = .017f + .009f * (.5f + .5f * std::sin(T0)), W1 = .017f + .009f * (.5f + .5f * std::sin(T1));
			const vec2 P = OrbitPoint(T0, W0), Q = OrbitPoint(T1, W1), R = OrbitPoint(T0, -W0), S = OrbitPoint(T1, -W1);
			const float G0 = Front ? .83f + .15f * std::cos(T0 + .8f) : .40f + .29f * std::cos(T0 + .7f);
			const float G1 = Front ? .83f + .15f * std::cos(T1 + .8f) : .40f + .29f * std::cos(T1 + .7f);
			const float Z0 = (Front ? .19f : .16f) * std::sin(T0);
			const float Z1 = (Front ? .19f : .16f) * std::sin(T1);
			pGraphics->SetColor4(Gray(G0), Gray(G1), Gray(G1 * .70f), Gray(G0 * .70f));
			const IGraphics::CFreeformItem Quad(Project(P, Z0), Project(Q, Z1), Project(R, Z0), Project(S, Z1));
			pGraphics->QuadsDrawFreeform(&Quad, 1);
		}
	};
	Ring(Pi, 2 * Pi, false);
	DrawMesh(Body(), -.09f, .51f, .18f);
	DrawMesh(Body(), .095f, .97f, .39f);
	DrawMesh(Brow(), .108f, .028f, .010f);
	DrawMesh(Crown(), .14f, .99f, .69f);
	// The folded triangular underside makes the crown point part of the same solid.
	Triangle(vec2(-.42f, -.44f), vec2(-.29f, -.15f), vec2(-.18f, -.20f), .145f, .96f, .47f, .75f);
	const vec2 Gaze(std::clamp(Look.x, -1.0f, 1.0f) * .018f, std::clamp(Look.y, -1.0f, 1.0f) * .022f);
	const auto Eye = [&](vec2 At, float Rx, float Ry, float Squash) {
		At += Gaze;
		const float Height = Ry * (1 - Squash) + .004f * Squash;
		for(int I = 0; I < 40; ++I)
		{
			const float T0 = I * 2 * Pi / 40, T1 = (I + 1) * 2 * Pi / 40;
			const vec2 P = At + vec2(std::cos(T0) * Rx, std::sin(T0) * Height), Q = At + vec2(std::cos(T1) * Rx, std::sin(T1) * Height);
			Triangle(At, P, Q, .154f, .018f, .035f, .035f);
		}
	};
	Eye(vec2(-.075f, .012f), .045f, .104f, 0);
	Eye(vec2(.153f, -.008f + Close * .011f), .042f, .098f, Close);
	if(Close > .45f)
	{
		for(int I = 0; I < 16; ++I)
		{
			const float X0 = -.044f + .088f * I / 16, X1 = -.044f + .088f * (I + 1) / 16;
			const float Y0 = .008f + .006f * (1 - X0 * X0 / (.044f * .044f)), Y1 = .008f + .006f * (1 - X1 * X1 / (.044f * .044f));
			const vec2 P = vec2(.153f, -.008f) + Gaze + vec2(X0, Y0), Q = vec2(.153f, -.008f) + Gaze + vec2(X1, Y1);
			pGraphics->SetColor4(Gray(.025f, Close), Gray(.025f, Close), Gray(.025f, Close), Gray(.025f, Close));
			const IGraphics::CFreeformItem Quad(Project(P + vec2(0, -.003f), .156f), Project(Q + vec2(0, -.003f), .156f), Project(P + vec2(0, .003f), .156f), Project(Q + vec2(0, .003f), .156f));
			pGraphics->QuadsDrawFreeform(&Quad, 1);
		}
	}
	Ring(0, Pi, true);
	// A four-point orbital glint, made of four concave curved sides.
	const vec2 StarCenter(.39f, -.35f);
	CMesh Star; Star.Point(StarCenter + vec2(0, -.081f));
	Star.Curve(StarCenter + vec2(.010f, -.025f), StarCenter + vec2(.026f, -.009f), StarCenter + vec2(.077f, 0));
	Star.Curve(StarCenter + vec2(.026f, .010f), StarCenter + vec2(.010f, .027f), StarCenter + vec2(0, .083f));
	Star.Curve(StarCenter + vec2(-.009f, .027f), StarCenter + vec2(-.027f, .010f), StarCenter + vec2(-.077f, 0));
	Star.Curve(StarCenter + vec2(-.027f, -.009f), StarCenter + vec2(-.010f, -.025f), StarCenter + vec2(0, -.081f));
	--Star.m_Count;
	for(int I = 0; I < Star.m_Count; ++I) Triangle(StarCenter, Star.m_aPoints[I], Star.m_aPoints[(I + 1) % Star.m_Count], .10f, 1, .78f, .86f);
	pGraphics->QuadsEnd();
}

#endif
