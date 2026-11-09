/* Copyright © 2026 BestProject Team */
#include "widgets.h"

#include <base/math.h>

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <game/client/ui.h>

#include <algorithm>

namespace BestClientUiTheme
{

void DrawCheckBoxChecked(const CUIRect *pBox, float ColorMul)
{
	CUIRect Fill = *pBox;
	Fill.Margin(std::max(1.0f, Fill.h * 0.1f), &Fill);
	DrawUiCheckbox(&Fill, ColorRGBA(1.0f, 1.0f, 1.0f, 0.82f * ColorMul), true, IGraphics::CORNER_ALL, CheckBoxRounding(Fill.h));
}

float DoScrollbarH(CUi *pUi, const void *pId, const CUIRect *pRect, float Current, const char *pValueText)
{
	Current = std::clamp(Current, 0.0f, 1.0f);

	CUIRect Rail;
	pRect->HMargin(3.0f, &Rail);
	const float Rounding = SliderRounding(Rail.h);

	const bool InsideRail = pUi->MouseHovered(&Rail);
	bool Grabbed = false;

	if(pUi->CheckActiveItem(pId))
	{
		if(pUi->MouseButton(0))
		{
			Grabbed = true;
			if(pUi->Input()->ShiftIsPressed())
				pUi->SetMouseSlow(true);
		}
		else
		{
			pUi->SetActiveItem(nullptr);
		}
	}
	else if(pUi->HotItem() == pId)
	{
		if(InsideRail && pUi->MouseButtonClicked(0))
		{
			pUi->SetActiveItem(pId);
			Grabbed = true;
		}
	}

	if(InsideRail && !pUi->MouseButton(0))
		pUi->SetHotItem(pId);

	float ReturnValue = Current;
	if(Grabbed)
		ReturnValue = std::clamp((pUi->MouseX() - Rail.x) / std::max(Rail.w, 0.001f), 0.0f, 1.0f);

	const bool Active = pUi->CheckActiveItem(pId);
	const bool Hovered = pUi->HotItem() == pId;
	if(IsElySettingsScope())
	{
		const float Radius = std::max(2.0f, pRect->h * 0.21f);
		const CUIRect Track = {pRect->x + Radius, pRect->y + pRect->h * 0.42f, std::max(1.0f, pRect->w - Radius * 2.0f), pRect->h * 0.16f};
		Track.Draw(ColorRGBA(0.25f, 0.25f, 0.28f, 1), IGraphics::CORNER_ALL, Track.h * 0.5f);
		CUIRect Fill = Track;
		Fill.w = Track.w * ReturnValue;
		if(Fill.w > 0)
			Fill.Draw(ColorRGBA(0.95f, 0.95f, 0.96f, 1), IGraphics::CORNER_ALL, std::min(Fill.w, Fill.h) * 0.5f);
		const float KnobRadius = Active || Hovered ? Radius * 1.08f : Radius;
		const CUIRect Knob = {Track.x + Track.w * ReturnValue - KnobRadius, pRect->y + pRect->h * 0.5f - KnobRadius, KnobRadius * 2, KnobRadius * 2};
		Knob.Draw(ColorRGBA(0.98f, 0.98f, 0.99f, 1), IGraphics::CORNER_ALL, KnobRadius);
		return ReturnValue;
	}
	DrawUiScrollbar(&Rail, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), false, IGraphics::CORNER_ALL, Rounding);

	CUIRect Fill = Rail;
	if(ReturnValue > 0.001f)
	{
		Fill.w = std::max(Rail.w * ReturnValue, Rounding * 2.0f);
		Fill.w = std::min(Fill.w, Rail.w);
		const float FillAlpha = Active ? 0.55f : (Hovered ? 0.5f : 0.45f);
		DrawUiScrollbar(&Fill, ColorRGBA(1.0f, 1.0f, 1.0f, FillAlpha), Active || Hovered, IGraphics::CORNER_ALL, Rounding);
	}
	else
	{
		Fill.w = 0.0f;
	}

	if(pValueText && pValueText[0])
	{
		const float FontSize = pRect->h * CUi::ms_FontmodHeight * 0.8f;
		SLabelProperties Props;
		if(!IsCustomUiTextGradient())
			Props.SetColor(SliderValueColor());
		const ColorRGBA OldOutline = pUi->TextRender()->GetTextOutlineColor();
		pUi->TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.65f));
		pUi->DoLabel(&Rail, pValueText, FontSize, TEXTALIGN_MC, Props);
		pUi->TextRender()->TextOutlineColor(OldOutline);
	}

	return ReturnValue;
}

} // namespace BestClientUiTheme
