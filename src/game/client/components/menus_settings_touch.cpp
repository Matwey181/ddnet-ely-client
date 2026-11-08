#if defined(__APPLE__)
#include <base/detect.h>
#if defined(CONF_PLATFORM_IOS)
#include <ios/ios_music.h>
#endif
#endif
#include <algorithm>
#include "menus.h"
#include <base/math.h>
#include <engine/client.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>
#include <algorithm>

// Touch settings: shared palette.
static const ColorRGBA gs_TouchAccent = ColorRGBA(0x9d / 255.0f, 0x4e / 255.0f, 0xdd / 255.0f, 1.0f);
static const ColorRGBA gs_TouchPanelBorder = ColorRGBA(0x2a / 255.0f, 0x2a / 255.0f, 0x3e / 255.0f, 0.85f);
static const ColorRGBA gs_TouchRowText = ColorRGBA(0xd8 / 255.0f, 0xd8 / 255.0f, 0xe8 / 255.0f, 1.0f);
static const ColorRGBA gs_TouchCheckBg = ColorRGBA(0x28 / 255.0f, 0x28 / 255.0f, 0x34 / 255.0f, 1.0f);

void CMenus::RenderSettingsTouch(CUIRect MainView)
{
	// ---- header (touch settings style) ------------------------------------
	{
		CUIRect Head, Chip, Title;
		MainView.HSplitTop(34.0f, &Head, &MainView);
		Head.VSplitLeft(Head.h, &Chip, &Title);
		Title.VSplitLeft(12.0f, nullptr, &Title);
		Chip.Draw(gs_TouchPanelBorder, IGraphics::CORNER_ALL, 10.0f);
		CUIRect ChipIn;
		Chip.Margin(1.5f, &ChipIn);
		ChipIn.Draw(gs_TouchAccent, IGraphics::CORNER_ALL, 9.0f);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f);
		Ui()->DoLabel(&ChipIn, "T", 19.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Ui()->DoLabel(&Title, Localize("Touch"), 24.0f, TEXTALIGN_ML);
	}

	MainView.HSplitTop(5.0f, nullptr, &MainView);
	{
		CUIRect Sub;
		MainView.HSplitTop(12.0f, &Sub, &MainView);
		TextRender()->TextColor(0.48f, 0.50f, 0.55f, 1.0f);
		Ui()->DoLabel(&Sub, Localize("Configure touch controls and movement joystick"), 11.0f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}

	MainView.HSplitTop(12.0f, nullptr, &MainView);

	// ---- one scroll region shared by every section ---------------------------
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 40.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);

	RenderTouchOptionsSection(MainView, &s_ScrollRegion);

	s_ScrollRegion.End();
}

void CMenus::RenderTouchOptionsSection(CUIRect &MainView, CScrollRegion *pScrollRegion)
{
	CUIRect Button;
#if defined(CONF_PLATFORM_IOS)
	MainView.HSplitTop(30.0f, &Button, &MainView);
	pScrollRegion->AddRect(Button);
	static CButtonContainer s_AppleMusicAccess;
	if(DoButton_Menu(&s_AppleMusicAccess, Localize("Allow Apple Music access"), 0, &Button))
		IosMusicAuthorize();
	MainView.HSplitTop(8.0f, nullptr, &MainView);
#endif

	// ---- section header (touch settings style: purple bar) --------------------------
	{
		CUIRect Head;
		MainView.HSplitTop(26.0f, &Head, &MainView);
		MainView.HSplitTop(10.0f, nullptr, &MainView);
		pScrollRegion->AddRect(Head);
		Head.Draw(gs_TouchAccent, IGraphics::CORNER_ALL, 7.0f);
		Head.VMargin(10.0f, &Head);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f);
		Ui()->DoLabel(&Head, "TOUCH CONTROLS", 12.0f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}

	MainView.HSplitTop(20.0f, &Button, &MainView);
	pScrollRegion->AddRect(Button);
	Ui()->DoScrollbarOption(&g_Config.m_ClTouchJoystickAimSensitivity, &g_Config.m_ClTouchJoystickAimSensitivity, &Button, Localize("Aim sensitivity (joystick)"), 1, 20000,
		&CUi::ms_LogarithmicScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);

	MainView.HSplitTop(5.0f, nullptr, &MainView);
	MainView.HSplitTop(16.0f, &Button, &MainView);
	pScrollRegion->AddRect(Button);
	TextRender()->TextColor(0.7f, 0.7f, 0.7f, 1.0f);
	Ui()->DoLabel(&Button, Localize("How far the aim moves per full swipe across the joystick button. Higher = more sensitive."), 12.0f, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	// Touch settings toggle row: rounded square on the left, purple with a
	// check mark when enabled.
	const auto TouchToggle = [&](const void *pId, const char *pLabel, int *pValue) {
		CUIRect Row;
		MainView.HSplitTop(28.0f, &Row, &MainView);
		MainView.HSplitTop(2.0f, nullptr, &MainView);
		pScrollRegion->AddRect(Row);
		const bool Clicked = Ui()->DoButtonLogic(pId, 0, &Row, BUTTONFLAG_LEFT) != 0;
		const bool Hot = Ui()->HotItem() == pId;
		if(Clicked)
			*pValue = *pValue ? 0 : 1;
		Row.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Hot ? 0.05f : 0.02f), IGraphics::CORNER_ALL, 8.0f);
		CUIRect Box, Label;
		Row.VSplitLeft(28.0f, &Box, &Label);
		Label.VSplitLeft(8.0f, nullptr, &Label);
		Box.Margin(4.0f, &Box);
		Box.Draw(gs_TouchPanelBorder, IGraphics::CORNER_ALL, 6.0f);
		CUIRect BoxIn;
		Box.Margin(1.5f, &BoxIn);
		BoxIn.Draw(*pValue ? gs_TouchAccent : gs_TouchCheckBg, IGraphics::CORNER_ALL, 5.0f);
		if(*pValue)
		{
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f);
			Ui()->DoLabel(&BoxIn, "\xE2\x9C\x93", 12.0f, TEXTALIGN_MC);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
		TextRender()->TextColor(gs_TouchRowText);
		Ui()->DoLabel(&Label, pLabel, 13.0f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	};

	// ---- touch button editor -----------------------------------------------
	if(g_Config.m_ClTouchControls)
	{
		MainView.HSplitTop(16.0f, nullptr, &MainView);
		MainView.HSplitTop(22.0f, &Button, &MainView);
		pScrollRegion->AddRect(Button);
		{
			CUIRect Icon, Title;
			Button.VSplitLeft(Button.h + 8.0f, &Icon, &Title);
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			const unsigned OldFlags = TextRender()->GetRenderFlags();
			TextRender()->SetRenderFlags(OldFlags | TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | TEXT_RENDER_FLAG_NO_X_BEARING | TEXT_RENDER_FLAG_NO_Y_BEARING | TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | TEXT_RENDER_FLAG_NO_OVERSIZE);
			TextRender()->TextColor(gs_TouchAccent);
			Ui()->DoLabel(&Icon, FontIcon::PENCIL, 15.0f, TEXTALIGN_ML);
			TextRender()->SetRenderFlags(OldFlags);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			Ui()->DoLabel(&Title, Localize("Button editor"), 16.0f, TEXTALIGN_ML);
		}

		if(Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK)
		{
			MainView.HSplitTop(6.0f, nullptr, &MainView);
			MainView.HSplitTop(20.0f, &Button, &MainView);
			pScrollRegion->AddRect(Button);
			static CButtonContainer s_EditTouchButtonsButton;
			if(DoButton_Menu(&s_EditTouchButtonsButton, Localize("Edit touch buttons"), 0, &Button))
			{
				GameClient()->m_TouchControls.SetEditingActive(true);
				GameClient()->m_TouchControls.ResetVirtualVisibilities();
				m_MenusIngameTouchControls.m_EditElement = CMenusIngameTouchControls::EElementType::LAYOUT;
				SetActive(false);
			}
			MainView.HSplitTop(2.0f, nullptr, &MainView);
			MainView.HSplitTop(28.0f, &Button, &MainView);
			pScrollRegion->AddRect(Button);
			TextRender()->TextColor(0.7f, 0.7f, 0.7f, 1.0f);
			Ui()->DoLabel(&Button, Localize("Tap a button to edit it. Drag to move it; buttons push each other instead of overlapping. Press Done to save."), 12.0f, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else
		{
			MainView.HSplitTop(6.0f, nullptr, &MainView);
			MainView.HSplitTop(20.0f, &Button, &MainView);
			pScrollRegion->AddRect(Button);
			TextRender()->TextColor(0.6f, 0.6f, 0.6f, 1.0f);
			Ui()->DoLabel(&Button, Localize("Join a game to edit the touch buttons."), 12.0f, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
	}

	static CButtonContainer s_JoystickMoveCheckBox;
	static CButtonContainer s_JoystickMoveFloatCheckBox;

	// ---- dynamic movement joystick ----------------------------------------
	MainView.HSplitTop(16.0f, nullptr, &MainView);
	MainView.HSplitTop(22.0f, &Button, &MainView);
	pScrollRegion->AddRect(Button);
	{
		CUIRect Icon, Title;
		Button.VSplitLeft(Button.h + 8.0f, &Icon, &Title);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		const unsigned OldFlags = TextRender()->GetRenderFlags();
		TextRender()->SetRenderFlags(OldFlags | TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | TEXT_RENDER_FLAG_NO_X_BEARING | TEXT_RENDER_FLAG_NO_Y_BEARING | TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | TEXT_RENDER_FLAG_NO_OVERSIZE);
		TextRender()->TextColor(gs_TouchAccent);
		Ui()->DoLabel(&Icon, FontIcon::ARROWS_LEFT_RIGHT, 15.0f, TEXTALIGN_ML);
		TextRender()->SetRenderFlags(OldFlags);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		Ui()->DoLabel(&Title, Localize("Movement joystick"), 16.0f, TEXTALIGN_ML);
	}

	MainView.HSplitTop(6.0f, nullptr, &MainView);
	TouchToggle(&s_JoystickMoveCheckBox, Localize("Use dynamic joystick instead of move buttons"), &g_Config.m_ClTouchJoystickMove);

	if(g_Config.m_ClTouchJoystickMove)
	{
		// Floating base: whether the joystick base follows the finger when it is dragged
		// past the edge of the base, or stays where the finger touched down.
		MainView.HSplitTop(6.0f, nullptr, &MainView);
		TouchToggle(&s_JoystickMoveFloatCheckBox, Localize("Floating joystick base"), &g_Config.m_ClTouchJoystickMoveFloat);
		MainView.HSplitTop(2.0f, nullptr, &MainView);
		MainView.HSplitTop(14.0f, &Button, &MainView);
		pScrollRegion->AddRect(Button);
		TextRender()->TextColor(0.7f, 0.7f, 0.7f, 1.0f);
		Ui()->DoLabel(&Button, g_Config.m_ClTouchJoystickMoveFloat ? Localize("On: the base follows your finger when you drag it past the edge.") : Localize("Off: the base stays where you touched, the stick clamps at the edge."),
			12.0f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

		// Live preview of the joystick activation zone.
		MainView.HSplitTop(8.0f, nullptr, &MainView);
		const float PreviewHeight = std::clamp(MainView.w / std::max(Graphics()->ScreenAspect(), 0.01f), 90.0f, 170.0f);
		CUIRect Preview;
		MainView.HSplitTop(PreviewHeight, &Preview, &MainView);
		pScrollRegion->AddRect(Preview);
		Preview.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_ALL, 8.0f);
		CUIRect ScreenRect;
		Preview.Margin(3.0f, &ScreenRect);

		CUIRect Zone;
		Zone.x = ScreenRect.x + ScreenRect.w * g_Config.m_ClTouchJoystickMoveZoneX / 100.0f;
		Zone.y = ScreenRect.y + ScreenRect.h * g_Config.m_ClTouchJoystickMoveZoneY / 100.0f;
		Zone.w = ScreenRect.w * g_Config.m_ClTouchJoystickMoveZoneW / 100.0f;
		Zone.h = ScreenRect.h * g_Config.m_ClTouchJoystickMoveZoneH / 100.0f;
		Zone.Draw(gs_TouchAccent.WithAlpha(0.12f), IGraphics::CORNER_ALL, 5.0f);
		CUIRect ZoneBorder = Zone;
		ZoneBorder.Margin(1.5f, &ZoneBorder);
		ZoneBorder.Draw(gs_TouchAccent.WithAlpha(0.35f), IGraphics::CORNER_ALL, 5.0f);

		// Joystick illustration at the center of the zone.
		const vec2 ZoneCenter = Zone.Center();
		const float StickRadius = std::clamp(ScreenRect.h * g_Config.m_ClTouchJoystickMoveSize / 100.0f, 5.0f, Zone.h * 0.45f);
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.35f);
		Graphics()->DrawCircle(ZoneCenter.x, ZoneCenter.y, StickRadius, 32);
		Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.30f);
		Graphics()->DrawCircle(ZoneCenter.x, ZoneCenter.y, std::max(StickRadius - 2.0f, 1.0f), 32);
		const vec2 KnobPos = ZoneCenter + vec2(StickRadius * 0.35f, 0.0f);
		const float KnobRadius = std::max(StickRadius * 0.4f, 2.0f);
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.85f);
		Graphics()->DrawCircle(KnobPos.x, KnobPos.y, KnobRadius, 24);
		Graphics()->QuadsEnd();

		const auto JoystickSlider = [&](int *pValue, const char *pLabel, int Min, int Max) {
			MainView.HSplitTop(5.0f, nullptr, &MainView);
			MainView.HSplitTop(20.0f, &Button, &MainView);
			pScrollRegion->AddRect(Button);
			Ui()->DoScrollbarOption(pValue, pValue, &Button, pLabel, Min, Max, &CUi::ms_LinearScrollbarScale, 0u, "%");
		};
		JoystickSlider(&g_Config.m_ClTouchJoystickMoveZoneX, Localize("Zone position (left)"), 0, 100);
		JoystickSlider(&g_Config.m_ClTouchJoystickMoveZoneY, Localize("Zone position (top)"), 0, 100);
		JoystickSlider(&g_Config.m_ClTouchJoystickMoveZoneW, Localize("Zone width"), 1, 100);
		JoystickSlider(&g_Config.m_ClTouchJoystickMoveZoneH, Localize("Zone height"), 1, 100);
		JoystickSlider(&g_Config.m_ClTouchJoystickMoveSize, Localize("Joystick size"), 5, 25);

		MainView.HSplitTop(6.0f, nullptr, &MainView);
		MainView.HSplitTop(16.0f, &Button, &MainView);
		pScrollRegion->AddRect(Button);
		TextRender()->TextColor(0.7f, 0.7f, 0.7f, 1.0f);
		Ui()->DoLabel(&Button, Localize("Touch inside the zone and the joystick appears under your finger. Drag it left and right to move."), 12.0f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
}
