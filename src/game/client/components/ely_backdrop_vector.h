#ifndef GAME_CLIENT_COMPONENTS_ELY_BACKDROP_VECTOR_H
#define GAME_CLIENT_COMPONENTS_ELY_BACKDROP_VECTOR_H

#include "ely_vector_mascot.h"

namespace ElyBackdropArt
{
using CMesh = ElyVectorArt::CMesh;
inline const CMesh &Ribbon()
{
	static const CMesh M = [] {
		CMesh P; P.Point(vec2(64, 1285));
		P.Curve(vec2(194, 1180), vec2(356, 1110), vec2(497, 1045));
		P.Point(vec2(629, 1134)); P.Point(vec2(760, 1600)); P.Point(vec2(-300, 1600));
		P.Triangulate(); return P;
	}(); return M;
}
inline const CMesh &Crown()
{
	static const CMesh M = [] {
		CMesh P; P.Point(vec2(456, 1010)); P.Point(vec2(615, 931));
		P.Point(vec2(562, 657)); P.Point(vec2(740, 774));
		P.Curve(vec2(923, 656), vec2(1092, 615), vec2(1224, 650));
		P.Curve(vec2(1385, 682), vec2(1480, 836), vec2(1530, 1050));
		P.Point(vec2(1540, 1630)); P.Point(vec2(625, 1630));
		P.Curve(vec2(615, 1380), vec2(607, 1210), vec2(629, 1134));
		P.Point(vec2(456, 1010)); --P.m_Count; P.Triangulate(); return P;
	}(); return M;
}
inline const CMesh &Face()
{
	static const CMesh M = [] {
		CMesh P; P.Point(vec2(629, 1134));
		P.Curve(vec2(616, 1160), vec2(611, 1230), vec2(620, 1285));
		P.Point(vec2(625, 1630)); P.Point(vec2(1540, 1630)); P.Point(vec2(1530, 1090));
		P.Curve(vec2(1486, 882), vec2(1368, 788), vec2(1224, 802));
		P.Curve(vec2(1033, 785), vec2(861, 865), vec2(730, 994));
		P.Point(vec2(629, 1134)); --P.m_Count; P.Triangulate(); return P;
	}(); return M;
}
inline const CMesh &Brow()
{
	static const CMesh M = [] {
		CMesh P; P.Point(vec2(730, 994));
		P.Curve(vec2(850, 827), vec2(1050, 715), vec2(1224, 733));
		P.Curve(vec2(1356, 747), vec2(1420, 790), vec2(1472, 857));
		P.Curve(vec2(1289, 750), vec2(1060, 782), vec2(871, 895));
		P.Curve(vec2(808, 935), vec2(765, 972), vec2(730, 994));
		--P.m_Count; P.Triangulate(); return P;
	}(); return M;
}
}

// Original portrait coordinates, with the face and ribbon continuing beyond the
// portrait edges. No image, background rectangle, orbit, glint, or frame is drawn.
inline void DrawElyBackdropVector(IGraphics *pGraphics, vec2 Origin, float Scale, float Alpha)
{
	if(!pGraphics || Scale <= 0 || Alpha <= 0) return;
	const float A = std::clamp(Alpha, 0.0f, 1.0f);
	const float Fringe = std::abs(pGraphics->GetScreen().Height()) / std::max(1, pGraphics->ScreenHeight());
	const auto Position = [&](vec2 P) { return Origin + P * Scale; };
	const auto Gray = [&](float G) { G = std::clamp(G, .015f, .40f); return ColorRGBA(G, G, G, A); };
	pGraphics->TextureClear(); pGraphics->QuadsBegin();
	const auto Triangle = [&](vec2 P, vec2 Q, vec2 R, float GP, float GQ, float GR) {
		pGraphics->SetColor4(Gray(GP), Gray(GQ), Gray(GR), Gray(GR));
		const IGraphics::CFreeformItem Quad(Position(P), Position(Q), Position(R), Position(R));
		pGraphics->QuadsDrawFreeform(&Quad, 1);
	};
	const auto Draw = [&](const ElyBackdropArt::CMesh &Mesh, auto Shade) {
		for(int I = 0; I < Mesh.m_Indices; I += 3)
		{
			const vec2 P = Mesh.m_aPoints[Mesh.m_aIndices[I]], Q = Mesh.m_aPoints[Mesh.m_aIndices[I + 1]], R = Mesh.m_aPoints[Mesh.m_aIndices[I + 2]];
			Triangle(P, Q, R, Shade(P), Shade(Q), Shade(R));
		}
		// One physical pixel of analytic coverage, independent of MSAA and UI scale.
		float Area = 0;
		for(int I = 0; I < Mesh.m_Count; ++I) Area += ElyVectorArt::CMesh::Cross(Mesh.m_aPoints[I], Mesh.m_aPoints[(I + 1) % Mesh.m_Count]);
		const float Sign = Area >= 0 ? 1.0f : -1.0f;
		const auto Outside = [&](int I) {
			const vec2 Before = Mesh.m_aPoints[(I + Mesh.m_Count - 1) % Mesh.m_Count], At = Mesh.m_aPoints[I], Next = Mesh.m_aPoints[(I + 1) % Mesh.m_Count];
			const vec2 D0 = At - Before, D1 = Next - At;
			const float L0 = std::max(.00001f, std::sqrt(D0.x * D0.x + D0.y * D0.y)), L1 = std::max(.00001f, std::sqrt(D1.x * D1.x + D1.y * D1.y));
			const vec2 N0 = vec2(D0.y, -D0.x) * (Sign / L0), N1 = vec2(D1.y, -D1.x) * (Sign / L1);
			const vec2 Sum = N0 + N1;
			const float Length = std::max(.00001f, std::sqrt(Sum.x * Sum.x + Sum.y * Sum.y));
			const vec2 Normal = Sum / Length;
			const float Miter = std::max(.5f, Normal.x * N1.x + Normal.y * N1.y);
			return Position(At) + Normal * (Fringe / Miter);
		};
		for(int I = 0; I < Mesh.m_Count; ++I)
		{
			const int J = (I + 1) % Mesh.m_Count;
			const vec2 P = Mesh.m_aPoints[I], Q = Mesh.m_aPoints[J];
			ColorRGBA GP = Gray(Shade(P)), GQ = Gray(Shade(Q)), OP = GP, OQ = GQ; OP.a = OQ.a = 0;
			pGraphics->SetColor4(GP, GQ, OQ, OP);
			const IGraphics::CFreeformItem Quad(Position(P), Position(Q), Outside(I), Outside(J));
			pGraphics->QuadsDrawFreeform(&Quad, 1);
		}
	};
	Draw(ElyBackdropArt::Ribbon(), [](vec2 P) { return .075f + (1250 - P.y) * .00011f + P.x * .000026f; });
	Draw(ElyBackdropArt::Crown(), [](vec2 P) { return .225f + (900 - P.x) * .00013f + (900 - P.y) * .00022f; });
	Draw(ElyBackdropArt::Face(), [](vec2 P) { return .205f + (950 - P.x) * .00010f + (950 - P.y) * .00033f; });
	Draw(ElyBackdropArt::Brow(), [](vec2 P) { return .024f + (1000 - P.y) * .000015f; });
	const auto Eye = [&](vec2 Center, float Rx, float Ry, float Rotation) {
		const float C = std::cos(Rotation), S = std::sin(Rotation);
		const auto Point = [&](float T) {
			const vec2 V(std::cos(T) * Rx, std::sin(T) * Ry);
			return Center + vec2(V.x * C - V.y * S, V.x * S + V.y * C);
		};
		for(int I = 0; I < 72; ++I)
		{
			const vec2 P = Point(I * 6.28318530718f / 72), Q = Point((I + 1) * 6.28318530718f / 72);
			Triangle(Center, P, Q, .025f, .027f + (P.y - Center.y) * .000025f, .027f + (Q.y - Center.y) * .000025f);
			const auto Outer = [&](float T, vec2 At) {
				vec2 N(std::cos(T) / Rx, std::sin(T) / Ry);
				const float L = std::sqrt(N.x * N.x + N.y * N.y); N = N / L;
				return Position(At) + vec2(N.x * C - N.y * S, N.x * S + N.y * C) * Fringe;
			};
			ColorRGBA GP = Gray(.027f + (P.y - Center.y) * .000025f), GQ = Gray(.027f + (Q.y - Center.y) * .000025f), OP = GP, OQ = GQ; OP.a = OQ.a = 0;
			pGraphics->SetColor4(GP, GQ, OQ, OP);
			const IGraphics::CFreeformItem Quad(Position(P), Position(Q), Outer(I * 6.28318530718f / 72, P), Outer((I + 1) * 6.28318530718f / 72, Q));
			pGraphics->QuadsDrawFreeform(&Quad, 1);
		}
	};
	Eye(vec2(866, 1065), 49, 110, .24f);
	Eye(vec2(1038, 1014), 46, 108, .28f);
	pGraphics->QuadsEnd();
}

#endif
