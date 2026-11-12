/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus_start.h"

#include <base/time.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/sound.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cmath>

namespace
{
CUIRect ElyLoadingCard(CUIRect View)
{
	const float W=std::min(View.w*.79f,View.h*.425f*(1556.f/340.f));
	return {View.x+(View.w-W)*.5f,View.y+View.h*.42f,W,W*(340.f/1556.f)};
}

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
	if(m_ElyWinkSound >= 0) { Sound()->UnloadSample(m_ElyWinkSound); m_ElyWinkSound = -1; }
	Graphics()->UnloadTexture(&m_ElyLogoTexture);
	Graphics()->UnloadTexture(&m_ElyBackgroundTexture);
	for(auto *p : {&m_ElyHomeBackgroundTexture,&m_ElyHomeMascotTexture,&m_ElyPlayTexture,&m_ElyQuickTexture,&m_ElyCardTexture,&m_ElyNavTexture,&m_ElyBrandTexture}) Graphics()->UnloadTexture(p);
	Graphics()->UnloadTexture(&m_ElyLoadingReferenceTexture);
	m_ElyAssetsLoaded = false;
}

void CMenusStart::ClearElyLabels()
{
	for(SElyLabel &Label : m_aElyLabels)
		TextRender()->DeleteTextContainer(Label.m_Container);
}

void CMenusStart::EnsureElyAssets()
{
	if(m_ElyAssetsLoaded)return;
	m_ElyLogoTexture=Graphics()->LoadTexture("ely/logo.png",IStorage::TYPE_ALL);
	CImageInfo Hero;
	if(Graphics()->LoadPng(Hero,"ely/supplied-mascot.png",IStorage::TYPE_ALL)) {
		const int Channels=Hero.m_Format==CImageInfo::FORMAT_RGBA?4:3;
		for(size_t I=0;I<(size_t)Hero.m_Width*Hero.m_Height;++I) {
			auto *P=Hero.m_pData+I*Channels;const uint8_t Gray=(uint8_t)(.2126f*P[0]+.7152f*P[1]+.0722f*P[2]);P[0]=P[1]=P[2]=Gray;
		}
		m_ElyBackgroundTexture=Graphics()->LoadTextureRawMove(Hero,0,"Supplied Ely mascot");
	}
	m_ElyLoadingReferenceTexture=Graphics()->LoadTexture("ely/supplied-loading-card.png",IStorage::TYPE_ALL);
	m_ElyHomeBackgroundTexture=Graphics()->LoadTexture("ely/home-background.png",IStorage::TYPE_ALL);
	m_ElyHomeMascotTexture=Graphics()->LoadTexture("ely/home-mascot.png",IStorage::TYPE_ALL);
	m_ElyPlayTexture=Graphics()->LoadTexture("ely/home-play.png",IStorage::TYPE_ALL);
	m_ElyQuickTexture=Graphics()->LoadTexture("ely/home-quick.png",IStorage::TYPE_ALL);
	m_ElyCardTexture=Graphics()->LoadTexture("ely/home-card.png",IStorage::TYPE_ALL);
	m_ElyNavTexture=Graphics()->LoadTexture("ely/home-nav.png",IStorage::TYPE_ALL);
	m_ElyBrandTexture=Graphics()->LoadTexture("ely/home-brand.png",IStorage::TYPE_ALL);
	m_ElyAssetsLoaded = true;
}

void CMenusStart::RenderElyBackground(CUIRect View, bool Settings)
{
	EnsureElyAssets();
	View.Draw4(ColorRGBA(.025f,.025f,.025f,1),ColorRGBA(.025f,.025f,.025f,1),ColorRGBA(.035f,.035f,.035f,1),ColorRGBA(.035f,.035f,.035f,1),IGraphics::CORNER_NONE,0);
	const float Startup=m_ElyStartupTick?(time_get()-m_ElyStartupTick)/(float)time_freq():0.f;
	const float LoadingAlpha=m_ElyDrawingLoading?1.f:(m_ElySawLoading&&g_Config.m_BcMainMenuAnimation&&!Settings?1.f-EaseOut((Startup-.50f)/.28f):0.f);
	auto *G=Graphics();G->BlendNormal();
	const auto DrawHero=[&](CUIRect R,float A) {
		if(A<=0||!HasTexture(m_ElyBackgroundTexture))return;
		G->TextureSet(m_ElyBackgroundTexture);G->QuadsBegin();G->SetColor(1,1,1,A);
		G->QuadsSetSubset(50.f/1254,322.f/1254,1184.f/1254,1077.f/1254);
		IGraphics::CQuadItem Q(R.x,R.y,R.w,R.h);G->QuadsDrawTL(&Q,1);G->QuadsEnd();
	};
	if(!m_ElyDrawingLoading && LoadingAlpha<1) {
		const float A=1-LoadingAlpha;
		if(HasTexture(m_ElyHomeBackgroundTexture)) {
			const float Fit=std::max(View.w/1672.f,View.h/941.f);
			const float W=1672*Fit,H=941*Fit;
			G->TextureSet(m_ElyHomeBackgroundTexture);G->QuadsBegin();G->QuadsSetSubset(0,0,1,1);G->SetColor(1,1,1,A);
			IGraphics::CQuadItem Q(View.x+(View.w-W)*.5f,View.y+(View.h-H)*.5f,W,H);G->QuadsDrawTL(&Q,1);G->QuadsEnd();
		}
		if(HasTexture(m_ElyHomeMascotTexture)) {
			const float W=std::min(View.w*.43f,View.h*.765f)*(Settings?.80f:1.f),H=W*(906.f/1162.f);
			G->TextureSet(m_ElyHomeMascotTexture);G->QuadsBegin();
			const float X=View.x+View.w*.61f,Y=View.y+View.h*.26f,Alpha=A*(Settings?.72f:1.f);
			for(int Row=0;Row<2;++Row) {
				const float T0=Row?.72f:0.f,T1=Row?1.f:.72f;
				G->QuadsSetSubset(89.f/1254,(338+906*T0)/1254,1251.f/1254,(338+906*T1)/1254);
				G->SetColor4(ColorRGBA(1,1,1,Alpha),ColorRGBA(1,1,1,Alpha),ColorRGBA(1,1,1,Row?0.f:Alpha),ColorRGBA(1,1,1,Row?0.f:Alpha));
				IGraphics::CQuadItem Q(X,Y+T0*H,W,(T1-T0)*H);G->QuadsDrawTL(&Q,1);
			}
			G->QuadsEnd();
		}
	}
	if(LoadingAlpha>0) {
		const float HeroW=std::min(View.w*.62f,View.h*.97f),HeroH=HeroW*(755.f/1134.f);
		DrawHero({View.x+View.w*.53f-HeroW*.5f,View.y+View.h*.04f,HeroW,HeroH},LoadingAlpha);
	}

}

void CMenusStart::DrawElyLoadingCard(CUIRect View, const char *pCaption, const char *pContent, float Progress, float Alpha)
{
	CUIRect Card=ElyLoadingCard(View);
	const float S=Card.w/1458.f;
	if(HasTexture(m_ElyLoadingReferenceTexture)) {
		Graphics()->BlendNormal();Graphics()->TextureSet(m_ElyLoadingReferenceTexture);Graphics()->QuadsBegin();Graphics()->SetColor(1,1,1,Alpha);
		Graphics()->QuadsSetSubset(58.f/1672,309.f/941,1614.f/1672,649.f/941);
		IGraphics::CQuadItem Q(Card.x,Card.y,Card.w,Card.h);Graphics()->QuadsDrawTL(&Q,1);Graphics()->QuadsEnd();
	} else Card.Draw(ColorRGBA(.05f,.05f,.05f,Alpha),IGraphics::CORNER_ALL,36*S);

	TextRender()->TextColor(1,1,1,Alpha);
	CUIRect Title = {Card.x, Card.y+Card.h*.36f, Card.w, 40*S};
	Ui()->DoLabel(&Title,pCaption,38*S,TEXTALIGN_MC);
	Title.y += Card.h*.17f; TextRender()->TextColor(.66f,.66f,.66f,Alpha);
	Ui()->DoLabel(&Title,pContent,24*S,TEXTALIGN_MC);
	CUIRect Track={Card.x+Card.w*.09f,Card.y+Card.h*.76f,Card.w*.756f,28*S};
	Track.Draw(ColorRGBA(.24f,.24f,.24f,Alpha),IGraphics::CORNER_ALL,12*S);
	CUIRect Fill=Track; Fill.w*=std::clamp(Progress,0.f,1.f);
	if(Fill.w>1*S) Fill.Draw(ColorRGBA(.98f,.98f,.98f,Alpha),IGraphics::CORNER_ALL,std::min(Fill.w/2,12*S));
	char aPercent[16];str_format(aPercent,sizeof(aPercent),"%d%%",(int)(std::clamp(Progress,0.f,1.f)*100));
	CUIRect Percent={Track.x+Track.w+22*S,Track.y-3*S,70*S,30*S};
	Ui()->DoLabel(&Percent,aPercent,26*S,TEXTALIGN_ML);
	TextRender()->TextColor(1,1,1,1);
}

void CMenusStart::RenderElyLoading(CUIRect View, const char *pCaption, const char *pContent, float Progress)
{
	m_ElySawLoading = true;
	m_ElyDrawingLoading=true;
	RenderElyBackground(View,false);
	m_ElyDrawingLoading=false;
	DrawElyLoadingCard(View,pCaption,pContent,Progress,1);
	const CUIRect Card=ElyLoadingCard(View);
	const float S=Card.w/1458.f;
	const CUIRect Logo={View.x+View.w*.5f-105*S,Card.y+Card.h*.085f,94*S,74*S};
	Graphics()->BlendAdditive();Graphics()->TextureSet(m_ElyLogoTexture);Graphics()->QuadsBegin();
	Graphics()->QuadsSetSubset(.15f,.216f,.855f,.77f);Graphics()->SetColor(1,1,1,1);
	IGraphics::CQuadItem Q(Logo.x,Logo.y,Logo.w,Logo.h);Graphics()->QuadsDrawTL(&Q,1);Graphics()->QuadsEnd();Graphics()->BlendNormal();
	CUIRect Brand={Logo.x+128*S,Logo.y,130*S,74*S};Ui()->DoLabel(&Brand,"ely.",54*S,TEXTALIGN_ML);
}

void CMenusStart::RenderStartMenu(CUIRect MainView)
{
	auto *pMenus = &GameClient()->m_Menus;
	auto *pGraphics = Graphics();
	auto *pText = TextRender();
	const float Scale = std::min(MainView.w / 1672.0f, MainView.h / 941.0f);
	const float Dt = std::clamp(Client()->RenderFrameTime(), 0.0f, .1f);
	const bool Animate = g_Config.m_BcAnimations && g_Config.m_BcMainMenuAnimation;
	const int64_t Now = time_get();
	if(!m_ElyStartupTick) m_ElyStartupTick = Now;
	const float Startup = (Now - m_ElyStartupTick) / (float)time_freq();
	const bool Boot = m_ElySawLoading && Startup < 1.65f && Animate;
	if(!m_ElyLastRender || Now - m_ElyLastRender > time_freq() / 2)
	{
		m_ElyIntro = 0; m_ElyOpening = false; m_ElyPlayHover = 0; m_ElyPointer = vec2(0, 0);
	}
	m_ElyLastRender = Now;
	m_ElyIntro = Animate ? std::clamp(m_ElyIntro + Dt * (m_ElyOpening ? -7.0f : 3.0f), 0.0f, 1.0f) : 1;
	const float Reveal = Boot ? EaseOut((Startup - .78f) / .35f) : EaseOut(m_ElyIntro);
	const bool CanUse = (!Boot || Startup > 1.1f) && !m_ElyOpening && !pMenus->HasOpenPopup() && !GameClient()->m_GameConsole.IsActive();
	const vec2 Mouse = Ui()->MousePos();
	const vec2 Pointer(std::clamp((Mouse.x - MainView.x) / MainView.w - .5f, -.5f, .5f), std::clamp((Mouse.y - MainView.y) / MainView.h - .5f, -.5f, .5f));
	m_ElyPointer = Animate ? mix(m_ElyPointer, Pointer, 1 - std::exp(-4 * Dt)) : vec2(0, 0);
	RenderElyBackground(MainView, false);
	if(Boot) DrawElyLoadingCard(MainView, "Loading DDNet Client", "Ready", 1, 1 - EaseOut((Startup - .50f) / .28f));
	const ColorRGBA PreviousColor = pText->GetTextColor();
	const auto R = [&](float X, float Y, float W, float H) { return CUIRect{MainView.x + MainView.w * X, MainView.y + MainView.h * Y, MainView.w * W, MainView.h * H}; };
	const auto Sprite = [&](IGraphics::CTextureHandle Texture, CUIRect Rect, float Alpha, float U0 = 0, float V0 = 0, float U1 = 1, float V1 = 1) {
		if(!HasTexture(Texture) || Alpha <= 0) return;
		pGraphics->TextureSet(Texture); pGraphics->QuadsBegin(); pGraphics->SetColor(1, 1, 1, Alpha);
		pGraphics->QuadsSetSubset(U0, V0, U1, V1);
		const IGraphics::CQuadItem Quad(Rect.x, Rect.y, Rect.w, Rect.h); pGraphics->QuadsDrawTL(&Quad, 1);
		pGraphics->QuadsEnd(); pGraphics->QuadsSetSubset(0, 0, 1, 1);
	};
	const auto Label = [&](int Id, CUIRect Rect, const char *Text, float FontSize, ColorRGBA Color, int Align = TEXTALIGN_ML, int Font = 0) {
		if(Color.a <= .001f || !Text[0]) return;
		FontSize *= Scale;
		SElyLabel &Cache = m_aElyLabels[Id];
		if(!Cache.m_Container.Valid() || Cache.m_Text != Text || Cache.m_Size != vec2(Rect.w, Rect.h) || Cache.m_FontSize != FontSize)
		{
			pText->DeleteTextContainer(Cache.m_Container); Cache.m_Text = Text; Cache.m_Position = vec2(Rect.x, Rect.y); Cache.m_Size = vec2(Rect.w, Rect.h); Cache.m_FontSize = FontSize;
			pText->SetFontPreset(Font == 2 ? EFontPreset::ICON_FONT : EFontPreset::DEFAULT_FONT);
			pText->SetCustomFace(Font == 1 ? BESTCLIENT_BETTER_FONT_FACE : "Google Sans");
			pText->TextColor(1, 1, 1, 1);
			float Height = 0, Glyph = 0; STextSizeProperties Props{}; Props.m_pHeight = &Height; Props.m_pMaxCharacterHeightInLine = &Glyph;
			const int Flags = TEXTFLAG_STOP_AT_END | TEXTFLAG_ELLIPSIS_AT_END;
			const float Width = pText->TextWidth(FontSize, Text, -1, Rect.w, Flags, Props);
			CTextCursor Cursor; Cursor.SetPosition(CUi::CalcAlignedCursorPos(&Rect, vec2(Width, Height), Align, &Glyph)); Cursor.m_FontSize = FontSize; Cursor.m_LineWidth = Rect.w; Cursor.m_MaxLines = 1; Cursor.m_Flags |= Flags;
			pText->CreateTextContainer(Cache.m_Container, &Cursor, Text);
		}
		const vec2 Offset = vec2(Rect.x, Rect.y) - Cache.m_Position;
		pText->RenderTextContainer(Cache.m_Container, Color, ColorRGBA(0, 0, 0, 0), Offset.x, Offset.y);
	};
	static CButtonContainer s_aButtons[18]; static float s_aHover[18]{};
	const auto Interact = [&](int Id, CUIRect Rect) {
#if defined(CONF_PLATFORM_IOS)
		// Keep the supplied artwork, but give small controls a finger-sized target.
		const float Minimum = MainView.h * (44.0f / 390.0f);
		if(Rect.w < Minimum) { Rect.x -= (Minimum - Rect.w) * .5f; Rect.w = Minimum; }
		if(Rect.h < Minimum) { Rect.y -= (Minimum - Rect.h) * .5f; Rect.h = Minimum; }
#endif
		const bool Hover = CanUse && Ui()->MouseHovered(&Rect);
		s_aHover[Id] = Animate ? mix(s_aHover[Id], Hover ? 1.0f : 0.0f, 1 - std::exp(-20 * Dt)) : (Hover ? 1.0f : 0.0f);
		return CanUse && Ui()->DoButtonLogic(&s_aButtons[Id], 0, &Rect, BUTTONFLAG_LEFT);
	};
	const auto Enter = [&](int Page, int Section = -1) {
		if(Section >= 0) g_Config.m_UiSettingsPage = Section;
		if(Section == CMenus::SETTINGS_BESTCLIENT) { pMenus->CloseBestClientFun(); pMenus->SetBestClientSettingsTab(0); }
		m_ElyOpening = true; m_ElyNextPage = Page;
	};
	const auto OpenServers = [&] { Enter(g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : CMenus::PAGE_INTERNET); };
	const ColorRGBA White(.98f, .97f, 1, Reveal), Muted(.70f, .69f, .77f, Reveal);
	// Original small brand; large decorative art remains in the background renderer.
	pGraphics->BlendAdditive();
	Sprite(m_ElyLogoTexture, R(.050f, .024f, .036f, .050f), Reveal, .15f, .216f, .855f, .77f);
	pGraphics->BlendNormal();
	Label(0, R(.093f, .020f, .055f, .052f), "ely.", 39, White, TEXTALIGN_ML, 1);
	const CUIRect BrandDivider = R(.147f, .035f, .0008f, .030f); BrandDivider.Draw(ColorRGBA(.6f, .58f, .68f, .45f * Reveal), IGraphics::CORNER_NONE, 0);
	Label(1, R(.162f, .026f, .065f, .043f), "v1.0.0", 19, Muted);
	const char *Tabs[] = {"Play", "Mods", "Settings", "Account"};
	const float NavX[] = {.329f, .409f, .489f, .592f}, NavW[] = {.070f, .070f, .091f, .089f};
	for(int I = 0; I < 4; ++I)
	{
		CUIRect Tab = R(NavX[I], .024f, NavW[I], .058f);
		const bool Click = Interact(I, Tab);
		if(I == 0 && HasTexture(m_ElyNavTexture)) Sprite(m_ElyNavTexture, Tab, Reveal,179.f/2172,273.f/724,593.f/2172,452.f/724);
		else if(I == 0 || s_aHover[I] > .001f) Tab.Draw(ColorRGBA(.21f, .20f, .26f, Reveal * (I == 0 ? .72f : .12f + .25f * s_aHover[I])), IGraphics::CORNER_ALL, 23 * Scale);
		if(I != 0 || !HasTexture(m_ElyNavTexture)) Label(2 + I, Tab, Tabs[I], 25, (I == 0 ? White : Muted), TEXTALIGN_MC);
		if(Click) { if(I == 0) OpenServers(); else if(I == 1) Enter(CMenus::PAGE_SETTINGS, CMenus::SETTINGS_BESTCLIENT); else if(I == 2) Enter(CMenus::PAGE_SETTINGS, CMenus::SETTINGS_GENERAL); else Enter(CMenus::PAGE_SETTINGS, CMenus::SETTINGS_PROFILES); }
	}
	const float Content = Animate ? EaseOut(std::clamp((Reveal - .10f) / .90f, 0.0f, 1.0f)) : 1;
	CUIRect Pill = R(.0795f, .235f, .0965f, .043f); Pill.Draw(ColorRGBA(.14f, .13f, .18f, .95f * Content), IGraphics::CORNER_ALL, 21 * Scale);
	CUIRect Dot = {Pill.x + 15 * Scale, Pill.y + Pill.h * .5f - 5 * Scale, 10 * Scale, 10 * Scale}; Dot.Draw(ColorRGBA(1, 1, 1, Content), IGraphics::CORNER_ALL, 5 * Scale);
	Label(6, {Pill.x + 39 * Scale, Pill.y, Pill.w - 46 * Scale, Pill.h}, "DDNet Client", 18, White.WithAlpha(Content));
	Label(7, R(.078f, .289f, .190f, .110f), "ely.", 110, White.WithAlpha(Content), TEXTALIGN_ML, 1);
	Label(8, R(.0795f, .415f, .30f, .043f), "A modern DDNet experience", 27, Muted.WithAlpha(Content));
	CUIRect Play = R(.0735f, .485f, .236f, .089f); const bool ClickPlay = Interact(4, Play); Play.y -= 2.5f * Scale * s_aHover[4];
	if(HasTexture(m_ElyPlayTexture)) Sprite(m_ElyPlayTexture, Play, Content,110.f/2172,134.f/724,2058.f/2172,583.f/724);
	else { DrawPill(pGraphics, Play, ColorRGBA(.98f, .98f, 1, Content), ColorRGBA(.86f, .84f, .93f, Content)); Label(9, {Play.x + 37 * Scale, Play.y, 57 * Scale, Play.h}, FontIcon::PLAY, 28, ColorRGBA(.03f, .03f, .05f, Content), TEXTALIGN_MC, 2); Label(10, {Play.x + 112 * Scale, Play.y, Play.w - 150 * Scale, Play.h}, "Play", 34, ColorRGBA(.03f, .03f, .05f, Content), TEXTALIGN_ML, 1); }
	if(s_aHover[4] > .001f) Play.Draw(ColorRGBA(1, 1, 1, .055f * s_aHover[4] * Content), IGraphics::CORNER_ALL, Play.h * .5f);
	if(ClickPlay) OpenServers();
	CUIRect Status = R(.077f, .590f, .37f, .037f);
	CUIRect Online = {Status.x, Status.y + Status.h * .5f - 6 * Scale, 13 * Scale, 13 * Scale}; Online.Draw(ColorRGBA(.99f, .99f, 1, Content), IGraphics::CORNER_ALL, 6.5f * Scale);
	Label(11, {Status.x + 32 * Scale, Status.y, 90 * Scale, Status.h}, "DDNet", 22, White.WithAlpha(Content));
	CUIRect Divider = {Status.x + 122 * Scale, Status.y + 9 * Scale, Scale, Status.h - 18 * Scale}; Divider.Draw(ColorRGBA(.7f, .68f, .78f, Content * .45f), IGraphics::CORNER_NONE, 0);
	char LastServer[192]; str_format(LastServer, sizeof(LastServer), "Last server: %s", g_Config.m_ClElyLastServer[0] ? g_Config.m_ClElyLastServer : "-");
	Label(12, {Status.x + 151 * Scale, Status.y, Status.w - 151 * Scale, Status.h}, LastServer, 22, Muted.WithAlpha(Content));
	const char *QuickNames[] = {"Servers", "Mods", "Folder"}; const char *QuickIcons[] = {"\uF11B", "\uF12E", FontIcon::FOLDER};
	for(int I = 0; I < 3; ++I)
	{
		CUIRect Button = R(.075f + I * .0725f, .655f, .054f, .088f); const bool Click = Interact(5 + I, Button); Button.y -= 2 * Scale * s_aHover[5 + I];
		if(HasTexture(m_ElyQuickTexture)) { const float X0[]={216,831,1446},X1[]={727,1343,1956}; Sprite(m_ElyQuickTexture, Button, Content, X0[I]/2172,110.f/724,X1[I]/2172,613.f/724); }
		else { Button.Draw(ColorRGBA(.12f, .115f, .15f, Content), IGraphics::CORNER_ALL, 22 * Scale); Label(13 + I * 2, {Button.x, Button.y + 9 * Scale, Button.w, Button.h * .48f}, QuickIcons[I], 29, White.WithAlpha(Content), TEXTALIGN_MC, 2); Label(14 + I * 2, {Button.x, Button.y + Button.h * .59f, Button.w, Button.h * .30f}, QuickNames[I], 18, Muted.WithAlpha(Content), TEXTALIGN_MC); }
		if(s_aHover[5 + I] > .001f) Button.Draw(ColorRGBA(.8f, .75f, 1, .08f * Content * s_aHover[5 + I]), IGraphics::CORNER_ALL, 22 * Scale);
		if(Click) { if(I == 0) OpenServers(); else if(I == 1) Enter(CMenus::PAGE_SETTINGS, CMenus::SETTINGS_BESTCLIENT); else { char Path[IO_MAX_PATH_LENGTH]; Storage()->GetCompletePath(IStorage::TYPE_SAVE, "", Path, sizeof(Path)); Client()->ViewFile(Path); } }
	}
	const char *CardNames[] = {"Mods", "Settings", "Account"}; const char *CardDescriptions[] = {"Manage your mods", "Customize the client", "Local profile and presets"}; const char *CardIcons[] = {"\uF1B2", "\uF1DE", FontIcon::USER};
	for(int I = 0; I < 3; ++I)
	{
		const float Stagger = Animate ? EaseOut(std::clamp((Reveal - .14f - I * .035f) / .72f, 0.0f, 1.0f)) : 1;
		CUIRect Card = R(.047f + I * .312f, .810f, .286f, .120f); const bool Click = Interact(8 + I, Card); Card.y += (1 - Stagger) * 8 * Scale - 2 * Scale * s_aHover[8 + I];
		if(I == 0 && HasTexture(m_ElyCardTexture)) Sprite(m_ElyCardTexture, Card, Stagger,88.f/2172,132.f/724,2084.f/2172,587.f/724);
		else {
			Card.Draw4(ColorRGBA(.48f,.47f,.55f,Stagger),ColorRGBA(.18f,.17f,.22f,Stagger),ColorRGBA(.18f,.17f,.23f,Stagger),ColorRGBA(.12f,.11f,.16f,Stagger),IGraphics::CORNER_ALL,25*Scale);
			CUIRect Inner=Card;Inner.Margin(1.3f*Scale,&Inner);
			Inner.Draw4(ColorRGBA(.135f,.13f,.17f,Stagger),ColorRGBA(.068f,.065f,.085f,Stagger),ColorRGBA(.06f,.055f,.08f,Stagger),ColorRGBA(.048f,.045f,.065f,Stagger),IGraphics::CORNER_ALL,24*Scale);
			if(I==2) {
				const vec2 C(Card.x+66*Scale,Card.y+Card.h*.5f);pGraphics->TextureClear();pGraphics->QuadsBegin();pGraphics->SetColor(White.WithAlpha(Stagger));
				for(int J=0;J<32;++J) DrawStroke(pGraphics,C+vec2(std::cos(J*2*pi/32)*9*Scale,std::sin(J*2*pi/32)*9*Scale-10*Scale),C+vec2(std::cos((J+1)*2*pi/32)*9*Scale,std::sin((J+1)*2*pi/32)*9*Scale-10*Scale),2.4f*Scale);
				for(int J=0;J<24;++J) {float T0=J*pi/24,T1=(J+1)*pi/24;DrawStroke(pGraphics,C+vec2(-17*std::cos(T0)*Scale,(17-13*std::sin(T0))*Scale),C+vec2(-17*std::cos(T1)*Scale,(17-13*std::sin(T1))*Scale),2.4f*Scale);}
				DrawStroke(pGraphics,C+vec2(-17*Scale,17*Scale),C+vec2(17*Scale,17*Scale),2.4f*Scale);pGraphics->QuadsEnd();
			}
			if(I!=2) Label(20 + I * 4, {Card.x + 33 * Scale, Card.y, 66 * Scale, Card.h}, CardIcons[I], 38, White.WithAlpha(Stagger), TEXTALIGN_MC, 2); Label(21 + I * 4, {Card.x + 123 * Scale, Card.y + 17 * Scale, Card.w - 170 * Scale, 38 * Scale}, CardNames[I], 29, White.WithAlpha(Stagger), TEXTALIGN_ML, 0); Label(22 + I * 4, {Card.x + 123 * Scale, Card.y + 60 * Scale, Card.w - 170 * Scale, 28 * Scale}, CardDescriptions[I], 21, Muted.WithAlpha(Stagger)); Label(23 + I * 4, {Card.x + Card.w - 62 * Scale, Card.y, 35 * Scale, Card.h}, FontIcon::CHEVRON_RIGHT, 26, White.WithAlpha(Stagger), TEXTALIGN_MC, 2); }
		if(s_aHover[8 + I] > .001f) Card.Draw(ColorRGBA(.7f, .62f, 1, .045f * Stagger * s_aHover[8 + I]), IGraphics::CORNER_ALL, 25 * Scale);
		if(Click) Enter(CMenus::PAGE_SETTINGS, I == 0 ? CMenus::SETTINGS_BESTCLIENT : (I == 1 ? CMenus::SETTINGS_GENERAL : CMenus::SETTINGS_PROFILES));
	}
	CUIRect Settings = R(.932f, .094f, .033f, .058f); const bool OpenSettings = Interact(11, Settings);
	Settings.Draw(ColorRGBA(.12f, .115f, .15f, Reveal), IGraphics::CORNER_ALL, 17 * Scale); DrawSettingsIcon(pGraphics, Settings.Center(), Scale * .95f, White);
	if(OpenSettings || (CanUse && CheckHotKey(KEY_S))) Enter(CMenus::PAGE_SETTINGS, CMenus::SETTINGS_GENERAL);
	bool Quit = false;
#if !defined(CONF_PLATFORM_ANDROID) && !defined(CONF_PLATFORM_IOS) && !defined(CONF_PLATFORM_EMSCRIPTEN)
	for(int I = 0; I < 3; ++I)
	{
		CUIRect Button = R(.864f + I * .038f, .014f, .038f, .059f); const bool Click = Interact(12 + I, Button);
		if(s_aHover[12 + I] > .001f) Button.Draw(ColorRGBA(1, 1, 1, .06f * Reveal * s_aHover[12 + I]), IGraphics::CORNER_ALL, 10 * Scale);
		const char *Icons[] = {FontIcon::MINUS, "\uF2D0", FontIcon::XMARK};
		if(I==1) {
			const vec2 C=Button.Center();const float R=7*Scale;
			pGraphics->TextureClear();pGraphics->QuadsBegin();pGraphics->SetColor(White);
			const vec2 P[]={C+vec2(-R,-R),C+vec2(R,-R),C+vec2(R,R),C+vec2(-R,R)};
			for(int J=0;J<4;++J)DrawStroke(pGraphics,P[J],P[(J+1)%4],2*Scale);pGraphics->QuadsEnd();
		} else Label(33 + I, Button, Icons[I], 22, White, TEXTALIGN_MC, 2);
		if(Click) { auto *pWindow = static_cast<IEngineGraphics *>(pGraphics); if(I == 0) pWindow->Minimize(); else if(I == 1) pWindow->ToggleMaximize(); else Quit = true; }
	}
#endif
	pText->SetFontPreset(EFontPreset::DEFAULT_FONT); BestClientApplyMenuFont(pText); pText->TextColor(PreviousColor);
	if(CanUse)
	{
		const bool Escape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE);
		if(Quit || Escape || CheckHotKey(KEY_Q))
		{
			if(g_Config.m_BcConfirmQuit || Escape || GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0)) pMenus->ShowQuitPopup(); else Client()->Quit();
			return;
		}
		if(Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P)) OpenServers();
		else if(CheckHotKey(KEY_D)) Enter(CMenus::PAGE_DEMOS);
		else if(CheckHotKey(KEY_E)) { static_cast<IEngineGraphics *>(pGraphics)->SetElyStartScreen(false); g_Config.m_ClEditor = 1; Input()->MouseModeRelative(); }
		else if(CheckHotKey(KEY_R)) { if(GameClient()->m_LocalServer.IsServerRunning()) GameClient()->m_LocalServer.KillServer(); else GameClient()->m_LocalServer.RunServer({}); }
	}
	if(m_ElyOpening && (!Animate || m_ElyIntro <= 0)) { m_ElyOpening = false; pMenus->SetShowStart(false); pMenus->SetMenuPage(m_ElyNextPage); }
}

bool CMenusStart::CheckHotKey(int Key) const
{
	return !Input()->ShiftIsPressed() && !Input()->ModifierIsPressed() && !Input()->AltIsPressed() &&
	       Input()->KeyPress(Key) &&
	       !GameClient()->m_GameConsole.IsActive();
}
