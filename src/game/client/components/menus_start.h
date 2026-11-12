/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_MENUS_START_H
#define GAME_CLIENT_COMPONENTS_MENUS_START_H

#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <array>
#include <string>

class CMenusStart : public CComponentInterfaces
{
public:
	void RenderStartMenu(CUIRect MainView);
	void RenderElyLoading(CUIRect View, const char *pCaption, const char *pContent, float Progress);
	void RenderElyBackground(CUIRect View, bool Settings);
	void UnloadElyAssets();
	void ClearElyLabels();

private:
	void DrawElyLoadingCard(CUIRect View, const char *pCaption, const char *pContent, float Progress, float Alpha);
	bool m_ElySawLoading = false, m_ElyWinkPlayed = false, m_ElyDrawingLoading = false;
	int64_t m_ElyStartupTick = 0;
	int m_ElyWinkSound = -1;
	enum EElyLabel
	{
		LABEL_BRAND,
		LABEL_VERSION,
		LABEL_PLAY_TAB,
		LABEL_SOCIAL,
		LABEL_STUDIO,
		LABEL_PLAY_ICON,
		LABEL_PLAY,
		LABEL_DDNET,
		LABEL_LAST_SERVER,
		NUM_LABELS = 64,
	};
	struct SElyLabel
	{
		STextContainerIndex m_Container;
		std::string m_Text;
		vec2 m_Position;
		vec2 m_Size;
		float m_FontSize = 0.0f;
	};
	bool CheckHotKey(int Key) const;
	void EnsureElyAssets();
	std::array<SElyLabel, NUM_LABELS> m_aElyLabels;
	IGraphics::CTextureHandle m_ElyLogoTexture;
	IGraphics::CTextureHandle m_ElyBackgroundTexture;
	IGraphics::CTextureHandle m_ElyHomeBackgroundTexture, m_ElyHomeMascotTexture;
	IGraphics::CTextureHandle m_ElyPlayTexture, m_ElyQuickTexture, m_ElyCardTexture, m_ElyNavTexture, m_ElyBrandTexture;
	IGraphics::CTextureHandle m_ElyLoadingReferenceTexture;
	float m_ElyBackgroundAspect = 1.0f;
	bool m_ElyAssetsLoaded = false;
	bool m_ElyOpening = false;
	float m_ElyIntro = 0.0f;
	float m_ElyPlayHover = 0.0f;
	vec2 m_ElyPointer;
	int64_t m_ElyLastRender = 0;
	int m_ElyNextPage = -1;
};

#endif
