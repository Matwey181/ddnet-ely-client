#include "hook_aim_activation.h"
#include "fx/fgf_glow.h"

#include <base/time.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/components/controls.h>
#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

namespace
{
float AngleDelta(float To, float From)
{
	return std::remainder(To - From, 2.0f * pi);
}
}

bool CControls::HookAimInputAvailable() const
{
	const auto *pGame = GameClient();
	return Client()->State() == IClient::STATE_ONLINE &&
	       !pGame->m_Menus.IsActive() && !pGame->m_GameConsole.IsActive() && !pGame->m_Chat.IsActive() &&
	       static_cast<IEngineGraphics *>(Graphics())->WindowActive();
}

bool CControls::HookAimReady() const
{
	return g_Config.m_BcHookAim && HookAimInputAvailable() &&
	       GameClient()->m_Snap.m_pLocalCharacter && !GameClient()->m_Snap.m_SpecInfo.m_Active;
}

bool CControls::OnInput(const IInput::CEvent &Event)
{
	if(Event.m_Key == KEY_UNKNOWN || Event.m_Key != g_Config.m_BcHookAimToggleKey ||
		(Event.m_Flags & (IInput::FLAG_PRESS | IInput::FLAG_REPEAT)) != IInput::FLAG_PRESS ||
		CBinds::GetModifierMask(Input()) != g_Config.m_BcHookAimToggleModifiers || !HookAimInputAvailable())
		return false;

	ToggleHookAim();
	return true;
}

void CControls::ToggleHookAim()
{
	g_Config.m_BcHookAim = !g_Config.m_BcHookAim;
	m_HookAimTarget = -1;
	m_HookAimTime = 0;
	GameClient()->Echo(g_Config.m_BcHookAim ? BcLocalize("Aimbot enabled") : BcLocalize("Aimbot disabled"));
}

float CControls::HookAimReach() const
{
	const float Hook = GameClient()->m_aTuning[g_Config.m_ClDummy].m_HookLength;
	return g_Config.m_BcHookAimRange > 0 ? std::min(Hook, (float)g_Config.m_BcHookAimRange) : Hook;
}

bool CControls::HookAimHeld() const
{
#if defined(CONF_PLATFORM_IOS)
	const bool Touch = g_Config.m_ClTouchControls != 0;
#else
	const bool Touch = false;
#endif
	const bool Keyboard = g_Config.m_BcHookAimKey != KEY_UNKNOWN && Input()->KeyIsPressed(g_Config.m_BcHookAimKey);
	return ElyMobile::HookAimActive(HookAimReady(), Touch, m_aInputData[g_Config.m_ClDummy].m_Hook != 0,
		m_aInputDebounce[g_Config.m_ClDummy].m_Hook, Keyboard,
		(CBinds::GetModifierMask(Input()) & g_Config.m_BcHookAimModifiers) == g_Config.m_BcHookAimModifiers);
}

int CControls::HookAimTarget() const
{
	if(m_HookAimTarget < 0 || m_HookAimConn != g_Config.m_ClDummy || !HookAimHeld() ||
		!GameClient()->m_Snap.m_aCharacters[m_HookAimTarget].m_Active)
		return -1;
	return m_HookAimTarget;
}

bool CControls::ApplyHookAimInput(CNetObj_PlayerInput &Input) const
{
	if(HookAimTarget() < 0)
		return false;

	const int Conn = g_Config.m_ClDummy;
	vec2 Aim = m_aMousePos[Conn];
	// The hook launches on the first press, before the visible cursor has finished turning.
	if(Input.m_Hook && !m_aLastData[Conn].m_Hook)
		Aim = m_HookAimDirection * length(Aim);
	if(g_Config.m_TcScaleMouseDistance)
	{
		const int MaxDistance = g_Config.m_ClDyncam ? g_Config.m_ClDyncamMaxDistance : g_Config.m_ClMouseMaxDistance;
		if(MaxDistance > 5 && MaxDistance < 1000)
			Aim *= 1000.0f / MaxDistance;
	}
	Input.m_TargetX = (int)std::round(Aim.x);
	Input.m_TargetY = (int)std::round(Aim.y);
	return true;
}

void CControls::UpdateHookAim()
{
	const int Conn = g_Config.m_ClDummy;
	if(!HookAimHeld())
	{
		m_HookAimTarget = -1;
		m_HookAimTime = 0;
		return;
	}
	if(m_HookAimConn != Conn)
	{
		m_HookAimConn = Conn;
		m_HookAimTarget = -1;
		m_HookAimTime = 0;
	}
	const int64_t Now = time_get();
	const float Dt = std::clamp(m_HookAimTime ? (Now - m_HookAimTime) / (float)time_freq() : Client()->RenderFrameTime(), 0.001f, 0.1f);
	m_HookAimTime = Now;
	const auto *pGame = GameClient();
	const int Local = pGame->m_Snap.m_LocalClientId;
	if(Local < 0 || Local >= MAX_CLIENTS || !pGame->m_Snap.m_aCharacters[Local].m_Active ||
		!pGame->m_aTuning[Conn].m_PlayerHooking || pGame->m_aClients[Local].m_HookHitDisabled)
	{
		m_HookAimTarget = -1;
		return;
	}
	const vec2 Origin = pGame->m_aClients[Local].m_RenderPos;
	const auto &LocalClient = pGame->m_aClients[Local];
	const float Reach = HookAimReach(), Half = g_Config.m_BcHookAimFov * pi / 360.0f;
	const float Current = angle(m_aMousePos[Conn]);
	constexpr float HookStart = CCharacterCore::PhysicalSize() * 1.5f;
	const auto CanReach = [&](vec2 Delta) {
		if(!g_Config.m_BcHookAimWalls)
			return true;
		int Tele = 0;
		const vec2 Start = Origin + normalize(Delta) * std::min(HookStart, length(Delta));
		return Collision()->IntersectLineTeleHook(Start, Origin + Delta, nullptr, nullptr, &Tele) == 0;
	};
	int Best = -1;
	float BestAngle = Half;
	for(int Id = 0; Id < MAX_CLIENTS; ++Id)
	{
		if(Id == Local || !pGame->m_Snap.m_aCharacters[Id].m_Active ||
			(g_Config.m_BcHookAimDummyOnly && (!Client()->DummyConnected() || Id != pGame->m_aLocalIds[!Conn])))
			continue;
		const auto &TargetClient = pGame->m_aClients[Id];
		if(!LocalClient.m_Super && !TargetClient.m_Super &&
			(LocalClient.m_Solo || TargetClient.m_Solo || !pGame->m_Teams.CanCollide(Id, Local)))
			continue;
		const vec2 Pos = TargetClient.m_RenderPos;
		const vec2 Delta = Pos - Origin;
		const float Distance = length(Delta), Error = std::abs(AngleDelta(angle(Delta), Current));
		if(Distance <= 1.0f || Distance > Reach || Error > Half || (Id != m_HookAimTarget && Best >= 0 && Error >= BestAngle))
			continue;
		if(!CanReach(Delta))
			continue;
		Best = Id;
		BestAngle = Error;
		if(Id == m_HookAimTarget)
			break;
	}
	m_HookAimTarget = Best;
	if(Best < 0)
		return;
	const auto &Target = pGame->m_aClients[Best];
	const vec2 Delta = Target.m_RenderPos - Origin;
	vec2 AimDelta = Delta;
	const float HookSpeed = pGame->m_aTuning[Conn].m_HookFireSpeed;
	if(g_Config.m_BcHookAimLead && HookSpeed > 0.0f)
	{
		const vec2 Velocity = vec2(Target.m_RenderCur.m_VelX, Target.m_RenderCur.m_VelY) / 256.0f;
		vec2 Lead = Delta;
		for(int Iteration = 0; Iteration < 3; ++Iteration)
		{
			const float Ticks = std::clamp((length(Lead) - HookStart) / HookSpeed, 0.0f, 6.0f);
			Lead = Delta + Velocity * Ticks;
		}
		if(length(Lead) > 1.0f && length(Lead) <= Reach &&
			std::abs(AngleDelta(angle(Lead), Current)) <= Half && CanReach(Lead))
			AimDelta = Lead;
	}
	const float Smooth = (g_Config.m_BcHookAimSmooth - 1) / 99.0f;
	const float Alpha = Smooth == 0.0f ? 1.0f : -std::expm1(-Dt / (0.005f + Smooth * Smooth * 0.6f));
	m_HookAimDirection = normalize(AimDelta);
	m_aMousePos[Conn] = direction(Current + AngleDelta(angle(AimDelta), Current) * Alpha) * std::min(Reach, std::max(80.0f, length(AimDelta)));
	m_aMouseInputType[Conn] = EMouseInputType::AUTOMATED;
	ClampMousePos();
}

void CControls::RenderHookAim()
{
	if(!g_Config.m_BcHookAimDraw || !HookAimReady())
		return;
	const auto *pGame = GameClient();
	const int Local = pGame->m_Snap.m_LocalClientId;
	const float Reach = HookAimReach();
	if(Local < 0 || Local >= MAX_CLIENTS || !pGame->m_Snap.m_aCharacters[Local].m_Active || Reach <= 0.0f)
		return;
	const vec2 Origin = pGame->m_aClients[Local].m_RenderPos;
	const float Current = angle(m_aMousePos[g_Config.m_ClDummy]), Half = g_Config.m_BcHookAimFov * pi / 360.0f;
	const auto &Camera = pGame->m_Camera;
	const auto Old = Graphics()->GetScreen();
	const float Aspect = Graphics()->ScreenAspect();
	Graphics()->MapScreen(Graphics()->MapScreenToWorld(Camera.m_Center.x, Camera.m_Center.y, 100, 100, 100, 0, 0, Aspect, Camera.m_Zoom));
	float Width, Height;
	Graphics()->CalcScreenParams(Aspect, Camera.m_Zoom, &Width, &Height);
	const float Pixel = Width / std::max(1, Graphics()->ScreenWidth());
	const bool Locked = HookAimTarget() >= 0;
	const ColorRGBA Tint = color_cast<ColorRGBA>(ColorHSLA(Locked ? g_Config.m_BcHookAimLockColor : g_Config.m_BcHookAimColor));
	const float Start = std::min(22.0f, Reach);
	const bool Decorated = g_Config.m_BcHookAimDecorations;
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	for(const float Offset : {-Half, Half})
	{
		const vec2 Dir = direction(Current + Offset);
		const vec2 aPoints[] = {Origin + Dir * Start, Origin + Dir * Reach};
		if(Decorated)
		{
			const float aScales[] = {1.0f, 0.05f};
			const float aWidths[] = {3.5f * Pixel, 1.7f * Pixel};
			const ColorRGBA aColors[] = {Tint.WithAlpha(0.07f), Tint.WithAlpha(0.18f)};
			FgfGlow::DrawStripScaled(Graphics(), aPoints, aScales, 2, aWidths, aColors, 2);
		}
		const ColorRGBA StartColor = Tint.WithAlpha(0.85f);
		const ColorRGBA EndColor = Tint.WithAlpha(Decorated ? 0.12f : 0.85f);
		Graphics()->SetColor4(StartColor, EndColor, EndColor, StartColor);
		const vec2 N = vec2(-Dir.y, Dir.x) * (Pixel * 0.65f);
		const IGraphics::CFreeformItem Line(aPoints[0] + N, aPoints[1] + N, aPoints[0] - N, aPoints[1] - N);
		Graphics()->QuadsDrawFreeform(&Line, 1);
	}
	Graphics()->QuadsEnd();
	Graphics()->MapScreen(Old);
}
