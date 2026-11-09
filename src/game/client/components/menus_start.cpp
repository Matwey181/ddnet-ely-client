/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus_start.h"

#include <base/time.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cmath>

namespace
{
float EaseOut(float Progress)
{
	const float Remaining = 1.0f - std::clamp(Progress, 0.0f, 1.0f);
	return 1.0f - Remaining * Remaining * Remaining;
}

bool HasTexture(IGraphics::CTextureHandle Texture)
{
	return Texture.IsValid() && !Texture.IsNullTexture();
}

void DrawPill(IGraphics *pGraphics, const CUIRect &Rect, ColorRGBA Top, ColorRGBA Bottom)
{
	const float Radius = Rect.h * 0.5f;
	const auto Edge = [&](float Phase) {
		const float Angle = (Phase - 0.5f) * pi;
		const float Inset = Radius * (1.0f - std::cos(Angle));
		const float Height = (1.0f + std::sin(Angle)) * 0.5f;
		const float Y = Rect.y + Rect.h * Height;
		return vec3(Rect.x + Inset, Rect.x + Rect.w - Inset, Y);
	};
	const auto Gradient = [&](float Y) {
		const float T = (Y - Rect.y) / Rect.h;
		return ColorRGBA(mix(Top.r, Bottom.r, T), mix(Top.g, Bottom.g, T), mix(Top.b, Bottom.b, T), Top.a);
	};

	pGraphics->TextureClear();
	pGraphics->QuadsBegin();
	constexpr int Segments = 24;
	vec3 From = Edge(0.0f);
	for(int i = 1; i <= Segments; ++i)
	{
		const vec3 To = Edge(i / (float)Segments);
		const ColorRGBA A = Gradient(From.z), B = Gradient(To.z);
		pGraphics->SetColor4(A, A, B, B);
		const IGraphics::CFreeformItem Quad(From.x, From.z, From.y, From.z, To.x, To.z, To.y, To.z);
		pGraphics->QuadsDrawFreeform(&Quad, 1);
		From = To;
	}
	pGraphics->QuadsEnd();
}

void DrawStroke(IGraphics *pGraphics, vec2 From, vec2 To, float Width)
{
	const vec2 Dir = normalize(To - From);
	const vec2 Normal(-Dir.y * Width * 0.5f, Dir.x * Width * 0.5f);
	const IGraphics::CFreeformItem Quad(From + Normal, To + Normal, From - Normal, To - Normal);
	pGraphics->QuadsDrawFreeform(&Quad, 1);
}

void DrawSettingsIcon(IGraphics *pGraphics, vec2 Center, float Scale, ColorRGBA Color)
{
	pGraphics->TextureClear();
	pGraphics->QuadsBegin();
	pGraphics->SetColor(Color);
	const auto GearPoint = [&](int Index) {
		const float Radius = Index % 4 == 1 || Index % 4 == 2 ? 14.0f : 11.4f;
		return Center + direction(2.0f * pi * Index / 32.0f) * (Radius * Scale);
	};
	for(int i = 0; i < 32; ++i)
		DrawStroke(pGraphics, GearPoint(i), GearPoint(i + 1), 2.15f * Scale);
	for(int i = 0; i < 20; ++i)
	{
		const vec2 From = Center + direction(2.0f * pi * i / 20.0f) * (4.3f * Scale);
		const vec2 To = Center + direction(2.0f * pi * (i + 1) / 20.0f) * (4.3f * Scale);
		DrawStroke(pGraphics, From, To, 1.9f * Scale);
	}
	pGraphics->QuadsEnd();
}
} // namespace

void CMenusStart::UnloadElyAssets()
{
	ClearElyLabels();
	Graphics()->UnloadTexture(&m_ElyLogoTexture);
	Graphics()->UnloadTexture(&m_ElyBackgroundTexture);
	m_ElyAssetsLoaded = false;
}

void CMenusStart::ClearElyLabels()
{
	for(SElyLabel &Label : m_aElyLabels)
		TextRender()->DeleteTextContainer(Label.m_Container);
}

void CMenusStart::EnsureElyAssets()
{
	if(m_ElyAssetsLoaded)
		return;
	m_ElyLogoTexture = Graphics()->LoadTexture("ely/logo.png", IStorage::TYPE_ALL);
	CImageInfo Background;
	if(Graphics()->LoadPng(Background, "ely/mascot-background.png", IStorage::TYPE_ALL))
	{
		m_ElyBackgroundAspect = Background.m_Width / (float)Background.m_Height;
		m_ElyBackgroundTexture = Graphics()->LoadTextureRawMove(Background, 0, "Ely mascot background");
	}
	m_ElyAssetsLoaded = true;
}

void CMenusStart::RenderElyBackground(CUIRect View, bool Settings)
{
	EnsureElyAssets();
	const ColorRGBA Back(0.035f, 0.031f, 0.043f, 1.0f);
	View.Draw4(Back, Back, Back, ColorRGBA(0.045f, 0.039f, 0.055f, 1.0f), IGraphics::CORNER_NONE, 0.0f);
	if(!HasTexture(m_ElyBackgroundTexture))
		return;

	const bool Animate = g_Config.m_BcMainMenuAnimation != 0;
	const float Scale = View.h / 941.0f;
	const float Overscan = 7.0f * Scale;
	const float Height = View.h * (Settings ? 0.77f : 1.0f) + Overscan * 2.0f;
	const float Width = Height * m_ElyBackgroundAspect;
	const float DriftX = Animate && !Settings ? m_ElyPointer.x * 5.0f * Scale : 0.0f;
	const float DriftY = Animate && !Settings ? m_ElyPointer.y * 4.0f * Scale : 0.0f;
	const CUIRect Image = {View.x + View.w - Width + Overscan + DriftX, View.y + View.h - Height + Overscan + DriftY, Width, Height};
	const float Alpha = Settings ? 0.72f : (Animate && !m_ElyOpening ? EaseOut(m_ElyIntro) : 1.0f);
	IGraphics *pGraphics = Graphics();
	pGraphics->BlendNormal();
	pGraphics->TextureSet(m_ElyBackgroundTexture);
	pGraphics->QuadsBegin();
	// The supplied portrait remains intact. Only its dark left edge fades into the wider canvas.
	constexpr float Seam = 0.085f;
	pGraphics->QuadsSetSubset(0.0f, 0.0f, Seam, 1.0f);
	pGraphics->SetColor4(ColorRGBA(1, 1, 1, 0), ColorRGBA(1, 1, 1, Alpha), ColorRGBA(1, 1, 1, 0), ColorRGBA(1, 1, 1, Alpha));
	IGraphics::CQuadItem Fade(Image.x, Image.y, Image.w * Seam, Image.h);
	pGraphics->QuadsDrawTL(&Fade, 1);
	pGraphics->QuadsSetSubset(Seam, 0.0f, 1.0f, 1.0f);
	pGraphics->SetColor(1, 1, 1, Alpha);
	IGraphics::CQuadItem Artwork(Image.x + Image.w * Seam, Image.y, Image.w * (1.0f - Seam), Image.h);
	pGraphics->QuadsDrawTL(&Artwork, 1);
	pGraphics->QuadsEnd();
}

void CMenusStart::RenderStartMenu(CUIRect MainView)
{
	auto *pMenus = &GameClient()->m_Menus;
	IGraphics *pGraphics = Graphics();
	ITextRender *pText = TextRender();
	#if defined(CONF_PLATFORM_IOS)
	const float Scale = std::min(MainView.h / 700.0f, MainView.w / 1190.0f);
#else
	const float Scale = std::min(MainView.h / 941.0f, MainView.w / 1190.0f);
#endif
	const float Right = MainView.x + MainView.w;
	const bool Animate = g_Config.m_BcMainMenuAnimation != 0;
	const float Dt = std::clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);
	const int64_t Now = time_get();
	if(!m_ElyLastRender || Now - m_ElyLastRender > time_freq() / 2)
	{
		m_ElyIntro = 0.0f;
		m_ElyPlayHover = 0.0f;
		m_ElyOpening = false;
		m_ElyPointer = vec2(0.0f, 0.0f);
	}
	m_ElyLastRender = Now;
	m_ElyIntro = Animate ? std::clamp(m_ElyIntro + Dt * (m_ElyOpening ? -7.0f : 1.85f), 0.0f, 1.0f) : 1.0f;

	const float HeaderAlpha = EaseOut(m_ElyIntro * 1.35f);
	const float ContentAlpha = EaseOut((m_ElyIntro - 0.10f) / 0.90f);
	const float HeaderOffset = (1.0f - HeaderAlpha) * -12.0f * Scale;
	const float ContentOffset = (1.0f - ContentAlpha) * 23.0f * Scale;
	const vec2 Mouse = Ui()->MousePos();
	const vec2 Pointer = vec2(
		std::clamp((Mouse.x - MainView.x) / MainView.w - 0.5f, -0.5f, 0.5f),
		std::clamp((Mouse.y - MainView.y) / MainView.h - 0.5f, -0.5f, 0.5f));
	m_ElyPointer = Animate ? mix(m_ElyPointer, Pointer, 1.0f - std::exp(-4.0f * Dt)) : vec2(0.0f, 0.0f);

	RenderElyBackground(MainView, false);
	const auto DrawLogo = [&](IGraphics::CTextureHandle Texture, const CUIRect &Rect, float Rotation, ColorRGBA Light, ColorRGBA Shade) {
		if(!HasTexture(Texture) || Light.a <= 0.0f)
			return;
		pGraphics->BlendAdditive();
		pGraphics->TextureSet(Texture);
		pGraphics->QuadsBegin();
		pGraphics->QuadsSetSubset(0.15f, 0.216f, 0.855f, 0.77f);
		pGraphics->QuadsSetRotation(Rotation);
		pGraphics->SetColor4(Light, Shade, Shade, Shade);
		IGraphics::CQuadItem Quad(Rect.x + Rect.w * 0.5f, Rect.y + Rect.h * 0.5f, Rect.w, Rect.h);
		pGraphics->QuadsDraw(&Quad, 1);
		pGraphics->QuadsEnd();
		pGraphics->QuadsSetRotation(0.0f);
		pGraphics->BlendNormal();
	};

	const ColorRGBA White(0.97f, 0.96f, 0.99f, HeaderAlpha);
	const ColorRGBA Muted(0.67f, 0.65f, 0.73f, HeaderAlpha);
	const ColorRGBA PreviousColor = pText->GetTextColor();
	bool FontChanged = false;
	enum class EFont { REGULAR, BRAND, ICON };
	const auto Label = [&](EElyLabel Id, const CUIRect &Rect, const char *pLabel, float Size, int Align, ColorRGBA Color, EFont Font = EFont::REGULAR, bool Ellipsis = false) {
		if(Color.a <= 0.0f)
			return;
		Size *= Scale;
		SElyLabel &Cache = m_aElyLabels[Id];
		if(!Cache.m_Container.Valid() || Cache.m_Text != pLabel || Cache.m_Size != vec2(Rect.w, Rect.h) || Cache.m_FontSize != Size)
		{
			pText->DeleteTextContainer(Cache.m_Container);
			Cache.m_Text = pLabel;
			Cache.m_Position = vec2(Rect.x, Rect.y);
			Cache.m_Size = vec2(Rect.w, Rect.h);
			Cache.m_FontSize = Size;
			pText->SetFontPreset(Font == EFont::ICON ? EFontPreset::ICON_FONT : EFontPreset::DEFAULT_FONT);
			pText->SetCustomFace(Font == EFont::BRAND ? BESTCLIENT_BETTER_FONT_FACE : "DejaVu Sans");
			pText->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			FontChanged = true;
			const int Flags = Ellipsis ? TEXTFLAG_STOP_AT_END | TEXTFLAG_ELLIPSIS_AT_END : 0;
			float Height = 0.0f, GlyphHeight = 0.0f;
			STextSizeProperties Props{};
			Props.m_pHeight = &Height;
			Props.m_pMaxCharacterHeightInLine = &GlyphHeight;
			const float Width = pText->TextWidth(Size, pLabel, -1, Rect.w, Flags, Props);
			CTextCursor Cursor;
			Cursor.SetPosition(CUi::CalcAlignedCursorPos(&Rect, vec2(Width, Height), Align, &GlyphHeight));
			Cursor.m_FontSize = Size;
			Cursor.m_LineWidth = Rect.w;
			Cursor.m_Flags |= Flags;
			if(!pText->CreateTextContainer(Cache.m_Container, &Cursor, pLabel))
				return;
		}
		const vec2 Offset = vec2(Rect.x, Rect.y) - Cache.m_Position;
		pText->RenderTextContainer(Cache.m_Container, Color, ColorRGBA(0, 0, 0, 0), Offset.x, Offset.y);
	};

	const float BrandX = MainView.x + MainView.w * 0.051f;
	const float HeaderY = MainView.y + MainView.h * 0.115f + HeaderOffset;
	const CUIRect BrandIcon = {BrandX, HeaderY - 26.0f * Scale, 65.0f * Scale, 51.0f * Scale};
	DrawLogo(m_ElyLogoTexture, BrandIcon, 0.0f, White, White);
	Label(LABEL_BRAND, {BrandX + 94.0f * Scale, HeaderY - 32.0f * Scale, 80.0f * Scale, 52.0f * Scale}, "ely.", 40.0f, TEXTALIGN_ML, White, EFont::BRAND);
	Label(LABEL_VERSION, {BrandX + 184.0f * Scale, HeaderY - 16.0f * Scale, 76.0f * Scale, 36.0f * Scale}, "v1.0.0", 18.0f, TEXTALIGN_ML, Muted);

	const auto Tab = [&](EElyLabel Id, float X, const char *pName, ColorRGBA Color) {
		Label(Id, {MainView.x + MainView.w * X - 53.0f * Scale, HeaderY - 25.0f * Scale, 106.0f * Scale, 40.0f * Scale}, pName, 27.0f, TEXTALIGN_MC, Color);
	};
	Tab(LABEL_PLAY_TAB, 0.398f, "play", White);
	Tab(LABEL_SOCIAL, 0.497f, "social", Muted);
	Tab(LABEL_STUDIO, 0.603f, "studio", Muted);
	const float UnderlineWidth = 57.0f * Scale * HeaderAlpha;
	const CUIRect Underline = {MainView.x + MainView.w * 0.398f - UnderlineWidth * 0.5f,
		HeaderY + 29.0f * Scale, UnderlineWidth, 3.4f * Scale};
	if(UnderlineWidth > 0.0f)
		Underline.Draw(White, IGraphics::CORNER_ALL, std::min(Underline.w, Underline.h) * 0.5f);

	const bool CanUse = !m_ElyOpening && !pMenus->HasOpenPopup() && !GameClient()->m_GameConsole.IsActive();
	const auto Click = [&](CButtonContainer &Id, const CUIRect &Rect) {
		return CanUse && Ui()->DoButtonLogic(&Id, 0, &Rect, BUTTONFLAG_LEFT);
	};
	const auto Hovered = [&](const CUIRect &Rect) {
		return CanUse && Ui()->MouseHovered(&Rect);
	};
	static CButtonContainer s_Settings;
	const vec2 SettingsCenter(Right - MainView.w * 0.062f, HeaderY + 2.0f * Scale);
	const CUIRect Settings = {SettingsCenter.x - 27.0f * Scale, SettingsCenter.y - 25.0f * Scale, 54.0f * Scale, 50.0f * Scale};
	if(Hovered(Settings))
		Settings.Draw(White.WithAlpha(0.08f * HeaderAlpha), IGraphics::CORNER_ALL, 12.0f * Scale);
	DrawSettingsIcon(pGraphics, SettingsCenter, Scale, White);
	const bool OpenSettings = Click(s_Settings, Settings) || (CanUse && CheckHotKey(KEY_S));

	const CUIRect PlayArea = {MainView.x + MainView.w * 0.0845f, MainView.y + MainView.h * 0.439f + ContentOffset, 414.0f * Scale, 98.0f * Scale};
	static CButtonContainer s_Play;
	const float HoverTarget = Hovered(PlayArea) ? 1.0f : 0.0f;
	m_ElyPlayHover = Animate ? mix(m_ElyPlayHover, HoverTarget, 1.0f - std::exp(-g_Config.m_BcMainMenuAnimationSpeed * Dt)) : HoverTarget;
	const bool ClickPlay = Click(s_Play, PlayArea);
	const bool Pressed = Animate && Ui()->CheckActiveItem(&s_Play) && Hovered(PlayArea);
	const float Lift = (3.0f * m_ElyPlayHover - (Pressed ? 3.0f : 0.0f)) * Scale;
	const CUIRect Play = {PlayArea.x, PlayArea.y - Lift, PlayArea.w + 5.0f * m_ElyPlayHover * Scale, PlayArea.h};
	for(int i = 3; i > 0; --i)
	{
		const float Spread = i * (3.4f + 1.0f * m_ElyPlayHover) * Scale;
		const CUIRect Glow = {Play.x - Spread, Play.y - Spread * 0.5f + 4.0f * Scale, Play.w + 2.0f * Spread, Play.h + Spread};
		const float Alpha = (0.035f + 0.019f * m_ElyPlayHover) * ContentAlpha;
		Glow.Draw(ColorRGBA(0.52f, 0.48f, 0.70f, Alpha), IGraphics::CORNER_ALL, Glow.h * 0.5f);
	}
	const ColorRGBA PlayTop(0.97f, 0.96f, 0.99f, ContentAlpha);
	const ColorRGBA PlayBottom(0.89f + 0.05f * m_ElyPlayHover, 0.88f + 0.05f * m_ElyPlayHover, 0.93f, ContentAlpha);
	DrawPill(pGraphics, Play, PlayTop, PlayBottom);
	const ColorRGBA Ink(0.055f, 0.048f, 0.070f, ContentAlpha);
	Label(LABEL_PLAY_ICON, {Play.x + 42.0f * Scale, Play.y, 64.0f * Scale, Play.h}, FontIcon::PLAY, 35.0f, TEXTALIGN_MC, Ink, EFont::ICON);
	Label(LABEL_PLAY, {Play.x + 131.0f * Scale, Play.y, 176.0f * Scale, Play.h}, "Play", 40.0f, TEXTALIGN_ML, Ink, EFont::BRAND);
	const float ArrowX = Play.x + Play.w - (55.0f - m_ElyPlayHover * 4.0f) * Scale;
	const float ArrowY = Play.y + Play.h * 0.5f;
	pGraphics->TextureClear();
	pGraphics->QuadsBegin();
	pGraphics->SetColor(Ink);
	DrawStroke(pGraphics, vec2(ArrowX - 6.5f * Scale, ArrowY - 12.0f * Scale), vec2(ArrowX + 5.0f * Scale, ArrowY), 3.2f * Scale);
	DrawStroke(pGraphics, vec2(ArrowX + 5.0f * Scale, ArrowY), vec2(ArrowX - 6.5f * Scale, ArrowY + 12.0f * Scale), 3.2f * Scale);
	pGraphics->QuadsEnd();

	const float StatusY = PlayArea.y + 122.0f * Scale;
	const CUIRect Dot = {PlayArea.x + 9.0f * Scale, StatusY + 11.0f * Scale, 16.0f * Scale, 16.0f * Scale};
	Dot.Draw(White.WithAlpha(ContentAlpha), IGraphics::CORNER_ALL, 8.0f * Scale);
	Label(LABEL_DDNET, {PlayArea.x + 46.0f * Scale, StatusY, 104.0f * Scale, 38.0f * Scale}, "DDNet", 24.0f, TEXTALIGN_ML, White.WithAlpha(ContentAlpha));
	const CUIRect Divider = {PlayArea.x + 154.0f * Scale, StatusY + 9.0f * Scale, 1.2f * Scale, 21.0f * Scale};
	Divider.Draw(Muted.WithAlpha(0.68f * ContentAlpha), IGraphics::CORNER_NONE, 0.0f);
	char aLastServer[160];
	str_format(aLastServer, sizeof(aLastServer), "last server: %s", g_Config.m_ClElyLastServer[0] ? g_Config.m_ClElyLastServer : "—");
	const float LastServerX = PlayArea.x + 189.0f * Scale;
	const float StatusWidth = std::min(420.0f * Scale, MainView.w * 0.59f - (LastServerX - MainView.x));
	Label(LABEL_LAST_SERVER, {LastServerX, StatusY, StatusWidth, 38.0f * Scale}, aLastServer, 24.0f, TEXTALIGN_ML, Muted.WithAlpha(ContentAlpha), EFont::REGULAR, true);

	bool Quit = false;
#if !defined(CONF_PLATFORM_ANDROID) && !defined(CONF_PLATFORM_IOS) && !defined(CONF_PLATFORM_EMSCRIPTEN)
	auto *pWindow = static_cast<IEngineGraphics *>(pGraphics);
	static CButtonContainer s_aWindowButtons[3];
	for(int i = 0; i < 3; ++i)
	{
		const CUIRect WindowButton = {Right - (184.0f - 60.0f * i) * Scale, MainView.y, 60.0f * Scale, 63.0f * Scale};
		if(Hovered(WindowButton))
		{
			const ColorRGBA Hover = i == 2 ? ColorRGBA(0.56f, 0.13f, 0.20f, 0.44f * HeaderAlpha) : White.WithAlpha(0.08f * HeaderAlpha);
			WindowButton.Draw(Hover, IGraphics::CORNER_NONE, 0.0f);
		}
		const vec2 Center(WindowButton.x + WindowButton.w * 0.5f, WindowButton.y + WindowButton.h * 0.5f + 1.0f * Scale);
		const float Radius = 7.4f * Scale;
		const float Weight = 2.2f * Scale;
		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		pGraphics->SetColor(White);
		if(i == 0)
		{
			DrawStroke(pGraphics, Center - vec2(Radius, 0.0f), Center + vec2(Radius, 0.0f), Weight);
		}
		else if(i == 1)
		{
			const vec2 aCorners[] = {Center + vec2(-Radius, -Radius), Center + vec2(Radius, -Radius),
				Center + vec2(Radius, Radius), Center + vec2(-Radius, Radius)};
			for(int Corner = 0; Corner < 4; ++Corner)
				DrawStroke(pGraphics, aCorners[Corner], aCorners[(Corner + 1) % 4], Weight);
		}
		else
		{
			DrawStroke(pGraphics, Center - vec2(Radius, Radius), Center + vec2(Radius, Radius), Weight);
			DrawStroke(pGraphics, Center + vec2(-Radius, Radius), Center + vec2(Radius, -Radius), Weight);
		}
		pGraphics->QuadsEnd();
		if(Click(s_aWindowButtons[i], WindowButton))
		{
			if(i == 0)
				pWindow->Minimize();
			else if(i == 1)
				pWindow->ToggleMaximize();
			else
				Quit = true;
		}
	}
#endif

	if(FontChanged)
	{
		pText->SetFontPreset(EFontPreset::DEFAULT_FONT);
		BestClientApplyMenuFont(pText);
		pText->TextColor(PreviousColor);
	}

	if(CanUse)
	{
		const bool Escape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE);
		if(Quit || Escape || CheckHotKey(KEY_Q))
		{
			if(g_Config.m_BcConfirmQuit || Escape || GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
				pMenus->ShowQuitPopup();
			else
				Client()->Quit();
			return;
		}

		int NewPage = -1;
		if(ClickPlay || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P))
			NewPage = g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;
		else if(OpenSettings)
			NewPage = CMenus::PAGE_SETTINGS;
		else if(CheckHotKey(KEY_D))
			NewPage = CMenus::PAGE_DEMOS;
		else if(CheckHotKey(KEY_E))
		{
			static_cast<IEngineGraphics *>(pGraphics)->SetElyStartScreen(false);
			g_Config.m_ClEditor = 1;
			Input()->MouseModeRelative();
		}
		else if(CheckHotKey(KEY_R))
		{
			if(GameClient()->m_LocalServer.IsServerRunning())
				GameClient()->m_LocalServer.KillServer();
			else
				GameClient()->m_LocalServer.RunServer({});
		}

		if(NewPage != -1)
		{
			m_ElyOpening = true;
			m_ElyNextPage = NewPage;
		}
	}
	if(m_ElyOpening && (!Animate || m_ElyIntro <= 0.0f))
	{
		m_ElyOpening = false;
		pMenus->SetShowStart(false);
		pMenus->SetMenuPage(m_ElyNextPage);
	}
}

bool CMenusStart::CheckHotKey(int Key) const
{
	return !Input()->ShiftIsPressed() && !Input()->ModifierIsPressed() && !Input()->AltIsPressed() &&
	       Input()->KeyPress(Key) &&
	       !GameClient()->m_GameConsole.IsActive();
}
