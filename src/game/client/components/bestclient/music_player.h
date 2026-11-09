/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_MUSIC_PLAYER_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_MUSIC_PLAYER_H

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <memory>

class ITextRender;

float MusicPlayerLegacyHudAnchorOffsetCanvasX(ITextRender *pTextRender, int LayoutScale, float HudWidth, float TextScale, bool *pOutReady);

class CMusicPlayer : public CComponent
{
public:
	struct SHudReservation
	{
		CUIRect m_Rect{};
		bool m_Visible = false;
		bool m_Active = false;
		float m_PushAmount = 0.0f;
	};

	CMusicPlayer();
	~CMusicPlayer() override;

	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	bool OnInput(const IInput::CEvent &Event) override;
	struct SElyIslandState { bool m_Music = false; bool m_Expanded = false; bool m_HasLyrics = false; float m_ExpandedHeight = 45.0f; };
	SElyIslandState ElyIslandState();
	void RenderElyIslandContent(const CUIRect &Rect, float Alpha, float Scale);
	void ToggleElyIsland();
	void OnReset() override;
	void OnUpdate() override;
	void OnRender() override;
	void OnShutdown() override;
	void OnWindowResize() override;

	SHudReservation HudReservation() const;
	vec2 GetHudPushOffsetForRect(const CUIRect &Rect, float CanvasWidth, float CanvasHeight, float Padding = 0.0f) const;
	CUIRect GetHudEditorRect(bool ForcePreview = false) const;
	void RenderHudEditor(bool ForcePreview);

private:
	bool m_ElyExpanded = false;
	int64_t m_ElyLastPlaying = 0;
	class CImpl;
	std::unique_ptr<CImpl> m_pImpl;

	void RenderMusicPlayer(bool ForcePreview);
	void EnsureImpl();
};

#endif
