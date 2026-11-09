/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/dbg.h>
#include <base/io.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/config.h>
#include <engine/font_icons.h>
#include <engine/keys.h>
#include <base/time.h>
#include <cmath>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/storage.h>
// bestclient
#include <engine/updater.h>
#include <SDL.h>
// bestclient

#include <game/client/components/menu_background.h>
#include <game/client/components/hud_layout.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h> // bestclient
// bestclient
#include <game/client/components/bestclient/ui_theme/settings_nav.h>
#include <game/client/components/bestclient/ui_theme/style.h>
#include <game/client/components/bestclient/settings_search.h>
// bestclient
#include <game/localization.h>

#include <cctype>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

void CMenus::SetNeedSendInfo()
{
	if(m_Dummy)
		m_NeedSendDummyinfo = true;
	else
		m_NeedSendinfo = true;
}

CUi::EPopupMenuFunctionResult CMenus::PopupSettingsCountrySelection(void *pContext, CUIRect View, bool Active) // bestclient
{
	SPopupSettingsCountrySelectionContext *pPopupContext = static_cast<SPopupSettingsCountrySelectionContext *>(pContext);
	CMenus *pMenus = pPopupContext->m_pMenus;

	static CListBox s_ListBox;
	s_ListBox.SetActive(Active);
	s_ListBox.DoStart(50.0f, pMenus->GameClient()->m_CountryFlags.Num(), 8, 1, -1, &View, false);

	if(pPopupContext->m_New)
	{
		pPopupContext->m_New = false;
		s_ListBox.ScrollToSelected();
	}

	for(size_t i = 0; i < pMenus->GameClient()->m_CountryFlags.Num(); ++i)
	{
		const CCountryFlags::CCountryFlag &Entry = pMenus->GameClient()->m_CountryFlags.GetByIndex(i);
		const CListboxItem Item = s_ListBox.DoNextItem(&Entry, Entry.m_CountryCode == pPopupContext->m_Selection);
		if(!Item.m_Visible)
			continue;

		CUIRect FlagRect, Label;
		Item.m_Rect.Margin(5.0f, &FlagRect);
		FlagRect.HSplitBottom(12.0f, &FlagRect, &Label);
		Label.HSplitTop(2.0f, nullptr, &Label);
		const float OldWidth = FlagRect.w;
		FlagRect.w = FlagRect.h * 2.0f;
		FlagRect.x += (OldWidth - FlagRect.w) / 2.0f;
		pMenus->GameClient()->m_CountryFlags.Render(Entry.m_CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);
		pMenus->Ui()->DoLabel(&Label, Entry.m_aCountryCodeString, 10.0f, TEXTALIGN_MC);
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(NewSelected >= 0 && (size_t)NewSelected < pMenus->GameClient()->m_CountryFlags.Num())
		pPopupContext->m_Selection = pMenus->GameClient()->m_CountryFlags.GetByIndex(NewSelected).m_CountryCode;
	if(s_ListBox.WasItemSelected() || s_ListBox.WasItemActivated())
	{
		if(pPopupContext->m_pCountry != nullptr && NewSelected >= 0 && (size_t)NewSelected < pMenus->GameClient()->m_CountryFlags.Num())
		{
			*pPopupContext->m_pCountry = pPopupContext->m_Selection;
			pMenus->SetNeedSendInfo();
		}
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
} // bestclient


namespace
{
constexpr const char *EFFECTS_DIRECTORY = "ely/effects";
constexpr const char *EFFECTS_PREFIXES[] = {"ely_island_", "ely_music_", "ely_lyrics_", "ely_flash_", "ely_chat_"};
constexpr const char *EFFECTS_SECTIONS[] = {"island", "music", "lyrics", "flashes", "chat"};

struct SElyEffectValue
{
	SConfigVariable *m_pVariable;
	int64_t m_Number = 0;
	std::string m_Text;
};

struct SElyEffectsState
{
	std::vector<SElyEffectValue> m_vPending;
	bool m_ChatPlacementPending = false;
	vec2 m_ChatPosition = vec2(0.0f, 0.0f);
	int m_ChatScale = 100;
	std::string m_Status = "Presets are stored in ely/effects as data-only JSON.";
	std::string m_Name;
};
SElyEffectsState gs_ElyEffects;

int EffectSection(const char *pName)
{
	if(str_comp(pName, "cl_chat_size") == 0 || str_comp(pName, "cl_chat_width") == 0)
		return 4;
	for(int i = 0; i < 5; ++i)
		if(str_startswith(pName, EFFECTS_PREFIXES[i]))
			return i;
	return -1;
}

int EffectSectionIndex(const char *pName)
{
	if(!pName || !pName[0] || str_comp(pName, "all") == 0)
		return -1;
	for(int i = 0; i < 5; ++i)
		if(str_comp(pName, EFFECTS_SECTIONS[i]) == 0)
			return i;
	return -2;
}

std::vector<SConfigVariable *> EffectVariables(IConfigManager *pConfig)
{
	std::vector<SConfigVariable *> Result;
	pConfig->VisitVariables([](SConfigVariable *pVariable, void *pUser) {
		if(EffectSection(pVariable->m_pScriptName) >= 0)
			static_cast<std::vector<SConfigVariable *> *>(pUser)->push_back(pVariable);
	}, &Result);
	return Result;
}

std::string EffectLabel(const SConfigVariable *pVariable)
{
	std::string Label = pVariable->m_pHelp;
	const size_t Defaults = Label.find(" (default:");
	if(Defaults != std::string::npos)
		Label.resize(Defaults);
	return Label;
}

bool EffectPresetPath(const char *pName, char *pPath, size_t Size)
{
	if(!pName || !pName[0] || str_length(pName) > 48)
		return false;
	for(const unsigned char *pScan = (const unsigned char *)pName; *pScan; ++pScan)
		if(!std::isalnum(*pScan) && *pScan != '-' && *pScan != '_')
			return false;
	str_format(pPath, Size, "%s/%s.json", EFFECTS_DIRECTORY, pName);
	return true;
}

bool ExportEffects(IConfigManager *pConfig, IStorage *pStorage, const char *pName, int Section)
{
	char aPath[IO_MAX_PATH_LENGTH], aTemporary[IO_MAX_PATH_LENGTH];
	if(Section < -1 || !EffectPresetPath(pName, aPath, sizeof(aPath)))
	{
		gs_ElyEffects.m_Status = "Use a preset name with letters, numbers, '-' or '_'.";
		return false;
	}
	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("ely_effects_version");
	Writer.WriteIntValue(1);
	Writer.WriteAttribute("values");
	Writer.BeginObject();
	for(SConfigVariable *pVariable : EffectVariables(pConfig))
	{
		if(Section >= 0 && EffectSection(pVariable->m_pScriptName) != Section)
			continue;
		Writer.WriteAttribute(pVariable->m_pScriptName);
		if(pVariable->m_Type == SConfigVariable::VAR_INT)
			Writer.WriteIntValue(*static_cast<SIntConfigVariable *>(pVariable)->m_pVariable);
		else if(pVariable->m_Type == SConfigVariable::VAR_COLOR)
			Writer.WriteIntValue((int)*static_cast<SColorConfigVariable *>(pVariable)->m_pVariable);
		else
			Writer.WriteStrValue(static_cast<SStringConfigVariable *>(pVariable)->m_pStr);
	}
	Writer.EndObject();
	if(Section < 0 || Section == 4)
	{
		const auto Chat = HudLayout::Get(HudLayout::MODULE_CHAT, HudLayout::CANVAS_WIDTH, HudLayout::CANVAS_HEIGHT);
		Writer.WriteAttribute("chat_hud");
		Writer.BeginObject();
		Writer.WriteAttribute("x");
		Writer.WriteIntValue((int)std::round(Chat.m_X));
		Writer.WriteAttribute("y");
		Writer.WriteIntValue((int)std::round(Chat.m_Y));
		Writer.WriteAttribute("scale");
		Writer.WriteIntValue(Chat.m_Scale);
		Writer.EndObject();
	}
	Writer.EndObject();
	const std::string Json = Writer.GetOutputString();
	pStorage->CreateFolder("ely", IStorage::TYPE_SAVE);
	pStorage->CreateFolder(EFFECTS_DIRECTORY, IStorage::TYPE_SAVE);
	IOHANDLE File = pStorage->OpenFile(IStorage::FormatTmpPath(aTemporary, sizeof(aTemporary), aPath), IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		gs_ElyEffects.m_Status = "Could not open the preset for writing.";
		return false;
	}
	const bool Written = io_write(File, Json.data(), Json.size()) == Json.size();
	const bool Synced = io_sync(File) == 0;
	const bool Closed = io_close(File) == 0;
	if(!Written || !Synced || !Closed || !pStorage->RenameFile(aTemporary, aPath, IStorage::TYPE_SAVE))
	{
		pStorage->RemoveFile(aTemporary, IStorage::TYPE_SAVE);
		gs_ElyEffects.m_Status = "Could not save the preset.";
		return false;
	}
	gs_ElyEffects.m_Status = std::string("Exported ") + pName + ".json";
	return true;
}

bool PreviewEffects(IConfigManager *pConfig, IStorage *pStorage, const char *pName)
{
	gs_ElyEffects.m_vPending.clear();
	gs_ElyEffects.m_ChatPlacementPending = false;
	char aPath[IO_MAX_PATH_LENGTH];
	if(!EffectPresetPath(pName, aPath, sizeof(aPath)))
	{
		gs_ElyEffects.m_Status = "Invalid preset name.";
		return false;
	}
	IOHANDLE File = pStorage->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
	{
		gs_ElyEffects.m_Status = "Preset not found in ely/effects.";
		return false;
	}
	const int64_t Length = io_length(File);
	if(Length <= 0 || Length > 65536)
	{
		io_close(File);
		gs_ElyEffects.m_Status = "Preset must be a JSON file smaller than 64 KiB.";
		return false;
	}
	std::string Data((size_t)Length, '\0');
	const bool Read = io_read(File, Data.data(), Data.size()) == Data.size();
	io_close(File);
	json_value *pRoot = Read ? JsonParse(Data.data(), Data.size()) : nullptr;
	if(!pRoot)
	{
		gs_ElyEffects.m_Status = "Invalid JSON preset.";
		return false;
	}
	const json_value *pVersion = json_object_get(pRoot, "ely_effects_version");
	const json_value *pValues = json_object_get(pRoot, "values");
	const json_value *pChat = json_object_get(pRoot, "chat_hud");
	const bool HasChat = pChat && pChat->type != json_none;
	bool Valid = pRoot->type == json_object && pRoot->u.object.length == (HasChat ? 3u : 2u) &&
		pVersion && pVersion->type == json_integer && pVersion->u.integer == 1 &&
		pValues && pValues->type == json_object && pValues->u.object.length <= 100;
	const auto Variables = EffectVariables(pConfig);
	std::vector<SElyEffectValue> Pending;
	for(unsigned i = 0; Valid && i < pValues->u.object.length; ++i)
	{
		const auto &Entry = pValues->u.object.values[i];
		const auto Found = std::find_if(Variables.begin(), Variables.end(), [&](const SConfigVariable *pVariable) {
			return str_comp(pVariable->m_pScriptName, Entry.name) == 0;
		});
		if(Found == Variables.end() || std::any_of(Pending.begin(), Pending.end(), [&](const SElyEffectValue &Value) { return Value.m_pVariable == *Found; }))
		{
			Valid = false;
			break;
		}
		SElyEffectValue Value{*Found};
		if(Value.m_pVariable->m_Type == SConfigVariable::VAR_STRING)
		{
			const auto *pString = static_cast<SStringConfigVariable *>(Value.m_pVariable);
			Valid = Entry.value->type == json_string && Entry.value->u.string.length < pString->m_MaxSize;
			if(Valid)
			{
				Value.m_Text.assign(Entry.value->u.string.ptr, Entry.value->u.string.length);
				Valid = str_utf8_check(Value.m_Text.c_str()) && std::none_of(Value.m_Text.begin(), Value.m_Text.end(), [](unsigned char Character) { return Character < 32; });
			}
		}
		else
		{
			Valid = Entry.value->type == json_integer;
			if(Valid)
			{
				Value.m_Number = Entry.value->u.integer;
				if(Value.m_pVariable->m_Type == SConfigVariable::VAR_INT)
				{
					const auto *pInt = static_cast<SIntConfigVariable *>(Value.m_pVariable);
					Valid = Value.m_Number >= pInt->m_Min && Value.m_Number <= pInt->m_Max;
				}
				else
					Valid = Value.m_Number >= 0 && Value.m_Number <= 0xFFFFFF;
			}
		}
		if(Valid)
			Pending.push_back(std::move(Value));
	}
	vec2 ChatPosition(0.0f, 0.0f);
	int ChatScale = 100;
	if(Valid && HasChat)
	{
		const json_value *pX = json_object_get(pChat, "x");
		const json_value *pY = json_object_get(pChat, "y");
		const json_value *pScale = json_object_get(pChat, "scale");
		Valid = pChat->type == json_object && pChat->u.object.length == 3 &&
			pX && pX->type == json_integer && pX->u.integer >= -1000 && pX->u.integer <= 1000 &&
			pY && pY->type == json_integer && pY->u.integer >= -1000 && pY->u.integer <= 1000 &&
			pScale && pScale->type == json_integer && pScale->u.integer >= 25 && pScale->u.integer <= 300;
		if(Valid)
		{
			ChatPosition = vec2((float)pX->u.integer, (float)pY->u.integer);
			ChatScale = (int)pScale->u.integer;
		}
	}
	json_value_free(pRoot);
	if(!Valid || (Pending.empty() && !HasChat))
	{
		gs_ElyEffects.m_Status = "Preset rejected: unknown setting, invalid type or value outside its range.";
		return false;
	}
	gs_ElyEffects.m_vPending = std::move(Pending);
	gs_ElyEffects.m_ChatPlacementPending = HasChat;
	gs_ElyEffects.m_ChatPosition = ChatPosition;
	gs_ElyEffects.m_ChatScale = ChatScale;
	gs_ElyEffects.m_Name = pName;
	gs_ElyEffects.m_Status = "Preview ready: " + std::to_string(gs_ElyEffects.m_vPending.size() + (HasChat ? 3 : 0)) + " values. Apply when ready.";
	return true;
}

void ApplyEffects(IConfigManager *pConfig)
{
	for(const SElyEffectValue &Value : gs_ElyEffects.m_vPending)
	{
		if(Value.m_pVariable->m_Type == SConfigVariable::VAR_INT)
			static_cast<SIntConfigVariable *>(Value.m_pVariable)->SetValue((int)Value.m_Number);
		else if(Value.m_pVariable->m_Type == SConfigVariable::VAR_COLOR)
			static_cast<SColorConfigVariable *>(Value.m_pVariable)->SetValue((unsigned)Value.m_Number);
		else
			static_cast<SStringConfigVariable *>(Value.m_pVariable)->SetValue(Value.m_Text.c_str());
	}
	if(gs_ElyEffects.m_ChatPlacementPending)
	{
		HudLayout::SetPosition(HudLayout::MODULE_CHAT, gs_ElyEffects.m_ChatPosition.x, gs_ElyEffects.m_ChatPosition.y, HudLayout::POSITION_MODE_TOP_LEFT);
		HudLayout::SetScale(HudLayout::MODULE_CHAT, gs_ElyEffects.m_ChatScale);
	}
	if(!gs_ElyEffects.m_vPending.empty() || gs_ElyEffects.m_ChatPlacementPending)
	{
		const bool Saved = pConfig->Save();
		gs_ElyEffects.m_Status = "Applied preset " + gs_ElyEffects.m_Name + (Saved ? "." : " for this session; saving failed.");
		gs_ElyEffects.m_vPending.clear();
		gs_ElyEffects.m_ChatPlacementPending = false;
	}
}

void ResetEffects(IConfigManager *pConfig, int Section)
{
	if(Section < -1)
		return;
	for(SConfigVariable *pVariable : EffectVariables(pConfig))
		if(Section < 0 || EffectSection(pVariable->m_pScriptName) == Section)
			pVariable->ResetToDefault();
	if(Section < 0 || Section == 4)
	{
		HudLayout::ResetPosition(HudLayout::MODULE_CHAT);
		HudLayout::ResetScale(HudLayout::MODULE_CHAT);
	}
	gs_ElyEffects.m_Status = "Section reset to defaults.";
}

void DisableEffects()
{
	g_Config.m_ElyIslandReducedMotion = 1;
	g_Config.m_ElyFlashEnabled = 0;
	g_Config.m_ElyMusicEqualizer = 0;
	g_Config.m_ElyLyricsAnimation = 0;
	g_Config.m_ElyChatInputAnimation = 0;
	g_Config.m_ElyChatTyping = 0;
	gs_ElyEffects.m_Status = "Motion and reactive light effects are off.";
}

void ElyCard(const CUIRect &Rect, float Radius, bool Hot = false)
{
	Rect.Draw(ColorRGBA(0.27f, 0.27f, 0.30f, Hot ? 0.85f : 0.60f), IGraphics::CORNER_ALL, Radius);
	CUIRect Inner = Rect;
	Inner.Margin(0.65f, &Inner);
	Inner.Draw(ColorRGBA(Hot ? 0.105f : 0.073f, Hot ? 0.105f : 0.073f, Hot ? 0.12f : 0.085f, 0.98f), IGraphics::CORNER_ALL, std::max(0.0f, Radius - 0.65f));
}
}

void CMenus::OnConsoleInit()
{
	Console()->Register("ely_effects_settings", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) {
		auto *pMenus = static_cast<CMenus *>(pUser);
		pMenus->SetActive(true);
		pMenus->SetShowStart(false);
		g_Config.m_UiSettingsPage = SETTINGS_MUSIC_EFFECTS;
		if(pMenus->Client()->State() == IClient::STATE_ONLINE)
			pMenus->m_GamePage = PAGE_SETTINGS;
		else
			pMenus->SetMenuPage(PAGE_SETTINGS);
	}, this, "Open Ely Music & Effects settings");
	Console()->Register("ely_effects_export", "s[name] ?s[section]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) {
		auto *pMenus = static_cast<CMenus *>(pUser);
		ExportEffects(pMenus->ConfigManager(), pMenus->Storage(), pResult->GetString(0), EffectSectionIndex(pResult->NumArguments() > 1 ? pResult->GetString(1) : "all"));
		pMenus->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ely", gs_ElyEffects.m_Status.c_str());
	}, this, "Export a data-only Music & Effects preset (all/island/music/lyrics/flashes/chat)");
	Console()->Register("ely_effects_preview", "s[name]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) {
		auto *pMenus = static_cast<CMenus *>(pUser);
		PreviewEffects(pMenus->ConfigManager(), pMenus->Storage(), pResult->GetString(0));
		pMenus->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ely", gs_ElyEffects.m_Status.c_str());
	}, this, "Validate and preview a JSON preset without applying values");
	Console()->Register("ely_effects_apply", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) {
		auto *pMenus = static_cast<CMenus *>(pUser);
		ApplyEffects(pMenus->ConfigManager());
		pMenus->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ely", gs_ElyEffects.m_Status.c_str());
	}, this, "Apply the previously validated Music & Effects preset");
	Console()->Register("ely_effects_reset", "?s[section]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) {
		auto *pMenus = static_cast<CMenus *>(pUser);
		ResetEffects(pMenus->ConfigManager(), EffectSectionIndex(pResult->NumArguments() ? pResult->GetString(0) : "all"));
	}, this, "Reset Music & Effects settings by section");
	Console()->Register("ely_effects_disable", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *) {
		DisableEffects();
	}, this, "Immediately disable reactive flashes and Ely motion effects");
}

void CMenus::RenderSettingsElyMusicEffects(CUIRect MainView)
{
	#if defined(CONF_PLATFORM_IOS)
	const float S = std::min(Ui()->Screen()->h / 700.0f, Ui()->Screen()->w / 1050.0f);
	CUIRect PlatformNote;
	MainView.HSplitTop(25.0f, &PlatformNote, &MainView);
	Ui()->DoLabel(&PlatformNote, Localize("iOS: audio-reactive EQ and flashes cannot read other apps' sound. Demo preview is available."), 11.0f, TEXTALIGN_ML);
#else
	const float S = std::min(Ui()->Screen()->h / 941.0f, Ui()->Screen()->w / 1050.0f);
#endif
	const float RowHeight = 36.0f * S;
	static bool s_Advanced = false, s_Demo = false;
	static int s_Section = 0;
	static CLineInputBuffered<96> s_Search;
	static CLineInputBuffered<49> s_PresetName;
	static CButtonContainer s_AdvancedButton, s_DemoButton, s_DisableButton;
	CUIRect Toolbar;
	MainView.HSplitTop(34.0f * S, &Toolbar, &MainView);
	CUIRect Search, Advanced, Demo, Disable;
	Toolbar.VSplitRight(174.0f * S, &Toolbar, &Disable);
	Toolbar.VSplitRight(96.0f * S, &Toolbar, &Demo);
	Toolbar.VSplitRight(112.0f * S, &Search, &Advanced);
	Search.VSplitRight(10.0f * S, &Search, nullptr);
	s_Search.SetEmptyText("Search Music & Effects");
	Ui()->DoEditBox(&s_Search, &Search, 14.0f * S);
	if(DoButton_Menu(&s_AdvancedButton, s_Advanced ? "Advanced" : "Simple", s_Advanced, &Advanced))
		s_Advanced = !s_Advanced;
	if(DoButton_Menu(&s_DemoButton, s_Demo ? "Demo on" : "Demo", s_Demo, &Demo))
		s_Demo = !s_Demo;
	if(DoButton_Menu(&s_DisableButton, "Disable all effects", 0, &Disable))
		DisableEffects();
	MainView.HSplitTop(10.0f * S, nullptr, &MainView);

	const char *apSections[] = {"Dynamic Island", "Music Player", "Lyrics", "Reactive Flashes", "Chat & HUD"};
	CUIRect Tabs;
	MainView.HSplitTop(32.0f * S, &Tabs, &MainView);
	static CButtonContainer s_aTabs[5];
	for(int i = 0; i < 5; ++i)
	{
		CUIRect Tab;
		Tabs.VSplitLeft(Tabs.w / (5 - i), &Tab, &Tabs);
		Tab.VMargin(3.0f * S, &Tab);
		if(DoButton_Menu(&s_aTabs[i], apSections[i], s_Section == i, &Tab))
			s_Section = i;
	}
	MainView.HSplitTop(8.0f * S, nullptr, &MainView);

	// Preview is deliberately local UI simulation. Real media values are never synthesized.
	CUIRect Preview;
	MainView.HSplitTop(80.0f * S, &Preview, &MainView);
	Preview.Draw(ColorRGBA(0.025f, 0.025f, 0.035f, 0.65f), IGraphics::CORNER_ALL, 10.0f * S);
	const float Clock = s_Demo && !g_Config.m_ElyIslandReducedMotion ? (float)std::fmod(Client()->LocalTime(), 12.0) : 0.0f;
	const int Phase = (int)(Clock / 2.0f);
	CUIRect PreviewLabel = {Preview.x + 14.0f * S, Preview.y + 8.0f * S, Preview.w * 0.28f, 20.0f * S};
	Ui()->DoLabel(&PreviewLabel, s_Demo ? "DEMO · simulated preview" : "Preview · enable Demo to animate", 12.0f * S, TEXTALIGN_ML);
	const float TargetWidth = (Phase >= 1 && Phase <= 4 ? 210.0f : 112.0f) * S;
	const float TargetHeight = (Phase >= 2 && Phase <= 3 ? 56.0f : 31.0f) * S;
	static float s_Width = 0.0f, s_Height = 0.0f, s_WidthVelocity = 0.0f, s_HeightVelocity = 0.0f;
	if(!s_Demo || g_Config.m_ElyIslandReducedMotion || !g_Config.m_BcAnimations || s_Width <= 0.0f)
	{
		s_Width = TargetWidth;
		s_Height = TargetHeight;
		s_WidthVelocity = s_HeightVelocity = 0.0f;
	}
	else
	{
		const float Frequency = std::sqrt((float)g_Config.m_ElyIslandStiffness) * 1.6f;
		const float Damping = std::clamp(g_Config.m_ElyIslandDamping / 100.0f, 0.1f, 0.99f);
		BCUiAnimations::SpringTo(s_Width, s_WidthVelocity, TargetWidth, Client()->RenderFrameTime(), Frequency, Damping);
		BCUiAnimations::SpringTo(s_Height, s_HeightVelocity, TargetHeight, Client()->RenderFrameTime(), Frequency, Damping);
	}
	const CUIRect Island = {Preview.x + (Preview.w - s_Width) * 0.5f, Preview.y + 11.0f * S, s_Width, s_Height};
	Island.Draw(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ElyIslandColor)).WithAlpha(g_Config.m_ElyIslandOpacity / 100.0f), IGraphics::CORNER_ALL, 15.5f * S);
	const bool LiveMusic = !s_Demo && GameClient()->m_MusicPlayer.ElyIslandState().m_Music;
	const char *pPreviewText = s_Demo ? (Phase == 0 || Phase == 5 ? "ely.   00:24" : (Phase == 4 ? "Demo track · paused" : "Demo track · Ely")) :
		(LiveMusic ? "ely. · music active" : (Client()->State() == IClient::STATE_ONLINE ? "ely. · connected" : "ely. · offline"));
	Ui()->DoLabel(&Island, pPreviewText, 13.0f * S, TEXTALIGN_MC);
	if(s_Demo && g_Config.m_ElyMusicEqualizer && Phase >= 1 && Phase <= 3)
	{
		for(int i = 0; i < g_Config.m_ElyMusicEqBands; ++i)
		{
			const float H = (4.0f + 8.0f * std::abs(std::sin(Clock * 4.0f + i * 0.8f))) * S;
			const CUIRect Bar = {Island.x + Island.w - (29.0f - i * 3.4f) * S, Island.y + 24.0f * S - H, 1.6f * S, H};
			Bar.Draw(ColorRGBA(0.8f, 0.78f, 0.85f, 0.6f), IGraphics::CORNER_ALL, 0.8f * S);
		}
	}
	if(Phase == 2 || Phase == 3)
	{
		const CUIRect Lyric = {Preview.x + Preview.w * 0.26f, Preview.y + 52.0f * S, Preview.w * 0.48f, 18.0f * S};
		Ui()->DoLabel(&Lyric, Phase == 2 ? "Original demo line · follow the quiet light" : "Original demo line · another moment flows", 11.0f * S, TEXTALIGN_MC);
	}
	if(s_Demo && g_Config.m_ElyFlashEnabled && !g_Config.m_ElyIslandReducedMotion && Phase == 3)
	{
		const float Pulse = std::pow(std::max(0.0f, std::sin(Clock * pi)), 6.0f) * g_Config.m_ElyFlashBrightness / 100.0f;
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ElyFlashColor)).WithAlpha(Pulse));
		Graphics()->DrawCircle(Preview.x + Preview.w * 0.86f, Preview.y + Preview.h * 0.6f, 18.0f * S, 32);
		Graphics()->QuadsEnd();
	}
	const CUIRect DemoChat = {Preview.x + 14.0f * S, Preview.y + 45.0f * S, 120.0f * S, 21.0f * S};
	if(s_Demo)
	{
		DemoChat.Draw(ColorRGBA(0.09f, 0.09f, 0.10f, 0.9f), IGraphics::CORNER_ALL, 10.5f * S);
		Ui()->DoLabel(&DemoChat, Phase % 2 ? "Demo · Hello Ely" : "Demo · composing", 11.0f * S, TEXTALIGN_MC);
	}
	MainView.HSplitTop(8.0f * S, nullptr, &MainView);

	if(s_Section == 0)
	{
		CUIRect Presets;
		MainView.HSplitTop(28.0f * S, &Presets, &MainView);
		const char *apPresets[] = {"Minimal", "Smooth", "Jelly", "Liquid", "Custom"};
		static CButtonContainer s_aMotion[5];
		for(int i = 0; i < 5; ++i)
		{
			CUIRect Button;
			Presets.VSplitLeft(Presets.w / (5 - i), &Button, &Presets);
			Button.VMargin(3.0f * S, &Button);
			if(DoButton_Menu(&s_aMotion[i], apPresets[i], g_Config.m_ElyIslandPreset == i, &Button))
			{
				g_Config.m_ElyIslandPreset = i;
				if(i < 4)
				{
					const int aStiffness[] = {380, 260, 210, 150};
					const int aDamping[] = {100, 82, 70, 78};
					const int aElasticity[] = {0, 12, 20, 17};
					g_Config.m_ElyIslandStiffness = aStiffness[i];
					g_Config.m_ElyIslandDamping = aDamping[i];
					g_Config.m_ElyIslandElasticity = aElasticity[i];
					g_Config.m_ElyIslandExpandMs = i == 0 ? 150 : (i == 3 ? 420 : 300);
					g_Config.m_ElyIslandCollapseMs = i == 0 ? 150 : (i == 3 ? 360 : 260);
				}
			}
		}
		MainView.HSplitTop(6.0f * S, nullptr, &MainView);
	}

	CUIRect Footer;
	MainView.HSplitBottom(96.0f * S, &MainView, &Footer);
	static CScrollRegion s_Scroll;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollbarThickness = 5.0f;
	ScrollParams.m_ScrollUnit = RowHeight * 3.0f;
	s_Scroll.Begin(&MainView, &ScrollParams);
	CUIRect Body = MainView;
	if(s_Section == 4)
	{
		CUIRect PositionRow;
		Body.HSplitTop(RowHeight, &PositionRow, &Body);
		if(s_Scroll.AddRect(PositionRow))
		{
			const bool CanPosition = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
			if(CanPosition)
			{
				static CButtonContainer s_ChatPosition;
				if(DoButton_Menu(&s_ChatPosition, "Position chat in the HUD editor", 0, &PositionRow))
				{
					SetActive(false);
					GameClient()->m_HudEditor.Activate();
				}
			}
			else
				Ui()->DoLabel(&PositionRow, "Chat positioning is available in the in-game HUD editor.", 12.0f * S, TEXTALIGN_ML);
		}
	}
	const char *pSearch = s_Search.GetString();
	int Visible = 0;
	for(SConfigVariable *pVariable : EffectVariables(ConfigManager()))
	{
		if(str_comp(pVariable->m_pScriptName, "ely_flash_regions") == 0) continue;
		if(str_comp(pVariable->m_pScriptName, "ely_lyrics_position") == 0 || str_comp(pVariable->m_pScriptName, "ely_lyrics_missing") == 0) continue; // Lyrics follow the two-line reference layout.
		const int Group = EffectSection(pVariable->m_pScriptName);
		if(Group != s_Section && !pSearch[0])
			continue;
		if(pSearch[0] && !str_utf8_find_nocase(pVariable->m_pHelp, pSearch) && !str_find_nocase(pVariable->m_pScriptName, pSearch))
			continue;
		const bool Basic = str_comp(pVariable->m_pScriptName, "ely_flash_color") == 0 || str_comp(pVariable->m_pScriptName, "ely_flash_secondary_color") == 0 || (pVariable->m_Type == SConfigVariable::VAR_INT &&
			(static_cast<SIntConfigVariable *>(pVariable)->m_Max == 1 ||
			 str_comp(pVariable->m_pScriptName, "ely_island_opacity") == 0 ||
			 str_comp(pVariable->m_pScriptName, "ely_island_scale") == 0 ||
			 str_comp(pVariable->m_pScriptName, "ely_flash_intensity") == 0 ||
			 str_comp(pVariable->m_pScriptName, "ely_chat_typing_duration") == 0));
		if(!s_Advanced && !pSearch[0] && !Basic)
			continue;
		CUIRect Row;
		Body.HSplitTop(RowHeight, &Row, &Body);
		s_Scroll.AddRect(Row);
		++Visible;
		if(!s_Scroll.RectClipped(Row))
		{
			const bool WordAnimation = str_comp(pVariable->m_pScriptName, "ely_lyrics_animation") == 0;
			const std::string Label = WordAnimation ? "Animate lyric words" : EffectLabel(pVariable);
			GameClient()->m_Tooltips.DoToolTip(pVariable, &Row, WordAnimation ? "Enhanced LRC uses exact word timestamps. Word timing for ordinary LRC is estimated within each line." : pVariable->m_pHelp);
			if(pVariable->m_Type == SConfigVariable::VAR_INT)
			{
				auto *pInt = static_cast<SIntConfigVariable *>(pVariable);
				const int Previous = *pInt->m_pVariable;
				if(pInt->m_Max == 1 && pInt->m_Min == 0)
				{
					if(DoButton_CheckBox(pVariable, Label.c_str(), *pInt->m_pVariable, &Row))
						pInt->SetValue(!*pInt->m_pVariable);
				}
				else
					Ui()->DoScrollbarOption(pVariable, pInt->m_pVariable, &Row, Label.c_str(), pInt->m_Min, pInt->m_Max);
				if(Group == 0 && Previous != *pInt->m_pVariable && str_comp(pVariable->m_pScriptName, "ely_island_preset") != 0)
					g_Config.m_ElyIslandPreset = 4;
			}
			else if(pVariable->m_Type == SConfigVariable::VAR_COLOR)
			{
				auto *pColor = static_cast<SColorConfigVariable *>(pVariable);
				CUIRect Name, Swatch;
				Row.VSplitRight(48.0f * S, &Name, &Swatch);
				DoButton_ColorPicker(&Swatch, pColor->m_pVariable, false, color_cast<ColorRGBA>(ColorHSLA(pColor->m_Default)), true);
				Ui()->DoLabel(&Name, Label.c_str(), 12.0f * S, TEXTALIGN_ML);
			}
			else
			{
				static CLineInputBuffered<512> s_Source, s_Lyrics;
				auto *pString = static_cast<SStringConfigVariable *>(pVariable);
				auto &Input = Group == 1 ? s_Source : s_Lyrics;
				DoEditBoxWithLabel(&Input, &Row, Group == 1 ? "Media app ID (empty = automatic)" : "Authorized local .lrc file", pString->m_pDefault, pString->m_pStr, pString->m_MaxSize);
			}
		}
	}
	if(!Visible)
	{
		CUIRect Empty = MainView;
		Empty.h = RowHeight;
		Ui()->DoLabel(&Empty, "No matching settings", 14.0f * S, TEXTALIGN_ML);
		s_Scroll.AddRect(Empty);
	}
	s_Scroll.End();

	static CButtonContainer s_Reset, s_Save, s_Export, s_Preview, s_Apply, s_Folder;
	CUIRect Actions;
	Footer.HSplitTop(30.0f * S, &Actions, &Footer);
	const char *apActions[] = {"Reset section", "Save", "Export section", "Preview import", "Apply preset", "Open folder"};
	CButtonContainer *apButtons[] = {&s_Reset, &s_Save, &s_Export, &s_Preview, &s_Apply, &s_Folder};
	for(int i = 0; i < 6; ++i)
	{
		CUIRect Button;
		Actions.VSplitLeft(Actions.w / (6 - i), &Button, &Actions);
		Button.VMargin(3.0f * S, &Button);
		if(DoButton_Menu(apButtons[i], apActions[i], 0, &Button))
		{
			const char *pName = s_PresetName.IsEmpty() ? "my-style" : s_PresetName.GetString();
			if(i == 0) ResetEffects(ConfigManager(), s_Section);
			else if(i == 1) { ConfigManager()->Save(); gs_ElyEffects.m_Status = "Settings saved."; }
			else if(i == 2) ExportEffects(ConfigManager(), Storage(), pName, s_Section);
			else if(i == 3) PreviewEffects(ConfigManager(), Storage(), pName);
			else if(i == 4) ApplyEffects(ConfigManager());
			else
			{
				Storage()->CreateFolder("ely", IStorage::TYPE_SAVE);
				Storage()->CreateFolder(EFFECTS_DIRECTORY, IStorage::TYPE_SAVE);
				char aPath[IO_MAX_PATH_LENGTH];
				Storage()->GetCompletePath(IStorage::TYPE_SAVE, EFFECTS_DIRECTORY, aPath, sizeof(aPath));
				Client()->ViewFile(aPath);
			}
		}
	}
	CUIRect Name, Status;
	Footer.HSplitTop(8.0f * S, nullptr, &Footer);
	Footer.HSplitTop(27.0f * S, &Name, &Status);
	s_PresetName.SetEmptyText("Preset name · my-style");
	Ui()->DoEditBox(&s_PresetName, &Name, 13.0f * S);
	std::string StatusText = gs_ElyEffects.m_Status;
	if(gs_ElyEffects.m_ChatPlacementPending)
		StatusText += " Chat HUD position and scale included.";
	if(!gs_ElyEffects.m_vPending.empty())
	{
		StatusText += "  ";
		const size_t Count = std::min<size_t>(3, gs_ElyEffects.m_vPending.size());
		for(size_t i = 0; i < Count; ++i)
		{
			const SElyEffectValue &Value = gs_ElyEffects.m_vPending[i];
			if(i)
				StatusText += " · ";
			StatusText += EffectLabel(Value.m_pVariable);
			StatusText += " = ";
			StatusText += Value.m_pVariable->m_Type == SConfigVariable::VAR_STRING ? Value.m_Text : std::to_string(Value.m_Number);
		}
	}
	SLabelProperties StatusProps;
	StatusProps.m_EllipsisAtEnd = true;
	StatusProps.m_MaxWidth = Status.w;
	Ui()->DoLabel(&Status, StatusText.c_str(), 11.0f * S, TEXTALIGN_ML, StatusProps);
}

void CMenus::RenderSettingsElyGeneral(CUIRect MainView)
{
	const float S = Ui()->Screen()->h / 941.0f;
	static bool s_More = false;
	static CButtonContainer s_MoreButton;
	CUIRect More = {MainView.x + MainView.w - 210.0f * S, MainView.y - 30.0f * S, 210.0f * S, 30.0f * S};
	ElyCard(More, 15.0f * S, Ui()->MouseHovered(&More));
	Ui()->DoLabel(&More, s_More ? Localize("Back") : Localize("More settings"), 15.0f * S, TEXTALIGN_MC);
	if(Ui()->DoButtonLogic(&s_MoreButton, 0, &More, BUTTONFLAG_LEFT))
		s_More = !s_More;

	static CScrollRegion s_Scroll;
	CScrollRegionParams Params;
	Params.m_ScrollbarThickness = 6.0f;
	Params.m_ScrollUnit = 55.0f * S;
	Params.m_ScrollbarNoOuterMargin = true;
	s_Scroll.Begin(&MainView, &Params);
	CUIRect Body = MainView;
	Body.h = s_More ? std::max(650.0f, MainView.h) : std::max(std::floor(MainView.h), (g_Config.m_ClDyncam ? 500.0f : 430.0f) * S);
	s_Scroll.AddRect(Body);
	if(s_More)
		RenderSettingsGeneral(Body);
	else
	{
		CUIRect Left, Right;
		Body.VSplitMid(&Left, &Right, 38.0f * S);
		const float RowHeight = 62.0f * S, Gap = 12.0f * S;
		const auto Check = [&](CUIRect &Column, const void *pId, const char *pName, int *pValue) {
			CUIRect Row;
			Column.HSplitTop(RowHeight, &Row, &Column);
			Column.HSplitTop(Gap, nullptr, &Column);
			if(DoButton_CheckBox(pId, pName, *pValue != 0, &Row))
				*pValue = !*pValue;
		};
		const auto Slider = [&](CUIRect &Column, int *pValue, const char *pName, int Min, int Max, unsigned Flags) {
			CUIRect Card, Inside;
			Column.HSplitTop(96.0f * S, &Card, &Column);
			Column.HSplitTop(Gap, nullptr, &Column);
			ElyCard(Card, 22.0f * S);
			Card.Margin(22.0f * S, &Inside);
			Inside.h = Card.h - 28.0f * S;
			Ui()->DoScrollbarOption(pValue, pValue, &Inside, pName, Min, Max, &CUi::ms_LinearScrollbarScale, Flags | CUi::SCROLLBAR_OPTION_MULTILINE);
		};

		const bool Dyncam = g_Config.m_ClDyncam || g_Config.m_ClMouseFollowfactor > 0;
		CUIRect CameraRow;
		Left.HSplitTop(RowHeight, &CameraRow, &Left);
		Left.HSplitTop(Gap, nullptr, &Left);
		if(DoButton_CheckBox(&g_Config.m_ClDyncam, Localize("Dynamic Camera"), Dyncam, &CameraRow))
		{
			g_Config.m_ClDyncam = !Dyncam;
			if(Dyncam)
				g_Config.m_ClMouseFollowfactor = 0;
		}
		if(g_Config.m_ClDyncam)
		{
			int Smooth = g_Config.m_ClDyncamSmoothness != 0;
			const int Previous = Smooth;
			Check(Left, &g_Config.m_ClDyncamSmoothness, Localize("Smooth Dynamic Camera"), &Smooth);
			if(Smooth != Previous)
			{
				g_Config.m_ClDyncamSmoothness = Smooth ? 50 : 0;
				if(Smooth)
					g_Config.m_ClDyncamStabilizing = 50;
			}
		}
		Check(Left, &g_Config.m_ClAutoswitchWeapons, Localize("Switch weapon on pickup"), &g_Config.m_ClAutoswitchWeapons);
		Check(Left, &g_Config.m_ClAutoswitchWeaponsOutOfAmmo, Localize("Switch weapon when out of ammo"), &g_Config.m_ClAutoswitchWeaponsOutOfAmmo);
		Slider(Left, &g_Config.m_ClRefreshRate, Localize("Update Rate"), 10, 1000, CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_NOCLAMPVALUE | CUi::SCROLLBAR_OPTION_DELAYUPDATE);
		static char s_PowerId;
		int Power = g_Config.m_ClRefreshRate != 0 && g_Config.m_ClRefreshRate <= 480;
		const int PreviousPower = Power;
		Check(Left, &s_PowerId, Localize("Save power by lowering update rate"), &Power);
		if(Power != PreviousPower)
			g_Config.m_ClRefreshRate = Power ? 480 : 0;

		Check(Right, &g_Config.m_ClAutoDemoRecord, Localize("Automatically record demos"), &g_Config.m_ClAutoDemoRecord);
		Slider(Right, &g_Config.m_ClAutoDemoMax, Localize("Max demos"), 1, 1000, CUi::SCROLLBAR_OPTION_INFINITE);
		Check(Right, &g_Config.m_ClAutoScreenshot, Localize("Automatically take game over screenshot"), &g_Config.m_ClAutoScreenshot);
		Check(Right, &g_Config.m_ClAutoStatboardScreenshot, Localize("Automatically take statboard screenshot"), &g_Config.m_ClAutoStatboardScreenshot);
		Check(Right, &g_Config.m_ClAutoCSV, Localize("Automatically create statboard csv"), &g_Config.m_ClAutoCSV);
	}
	s_Scroll.End();
}

void CMenus::RenderSettings(CUIRect MainView)
{
	// bestclient
	if(m_AssetsEditorState.m_Open)
		SDL_ShowCursor(SDL_ENABLE);
	if(m_AssetsEditorState.m_Open && m_AssetsEditorState.m_ExploreSide >= 0 && !Ui()->IsPopupOpen() && Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		m_AssetsEditorState.m_ExploreSide = -1;
	if(m_AssetsEditorState.m_Open && m_AssetsEditorState.m_ExploreSide < 0 && g_Config.m_UiSettingsPage == SETTINGS_ASSETS)
	{
		RenderAssetsEditorScreen(*Ui()->Screen());
		return;
	}
	// bestclient


	BestClientUiTheme::CElySettingsScope ElyScope;
	TextRender()->SetCustomFace("DejaVu Sans");
	TextRender()->TextColor(0.96f, 0.96f, 0.975f, 1.0f);
	const bool Ingame = Client()->State() == IClient::STATE_ONLINE;
	const CUIRect Screen = Ingame ? MainView : *Ui()->Screen();
	#if defined(CONF_PLATFORM_IOS)
	const float S = std::min(Screen.h / 700.0f, Screen.w / 1050.0f);
#else
	const float S = std::min(Screen.h / 941.0f, Screen.w / 1050.0f);
#endif
	const float Margin = 44.0f * S;
	m_MenusStart.RenderElyBackground(Screen, true);
	const auto Label = [&](CUIRect Rect, const char *pText, float Size, int Align = TEXTALIGN_ML) {
		Ui()->DoLabel(&Rect, pText, Size * S, Align);
	};
	const auto Icon = [&](CUIRect Rect, const char *pText, float Size) {
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		Label(Rect, pText, Size, TEXTALIGN_MC);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	};
	const bool Modal = HasOpenPopup() || GameClient()->m_GameConsole.IsActive();
	const auto Click = [&](const void *pId, CUIRect Rect) {
		return !Modal && Ui()->DoButtonLogic(pId, 0, &Rect, BUTTONFLAG_LEFT);
	};
	static char s_Home;
	const CUIRect Brand = {Screen.x + Margin, Screen.y + 21.0f * S, 72.0f * S, 45.0f * S};
	TextRender()->SetCustomFace(BESTCLIENT_BETTER_FONT_FACE);
	Label(Brand, "ely.", 38.0f);
	TextRender()->SetCustomFace("DejaVu Sans");
	if(Click(&s_Home, Brand))
	{
		if(Ingame)
			m_GamePage = PAGE_GAME;
		else
			SetShowStart(true);
	}
	CUIRect Version = {Brand.x + 78.0f * S, Brand.y + 7.0f * S, 78.0f * S, 31.0f * S};
	ElyCard(Version, 15.5f * S);
	Label(Version, "v1.0.0", 17.0f, TEXTALIGN_MC);

	BestClientSettingsSearch::Update(Client()->RenderFrameTime());
	const float SearchWidth = std::min(500.0f * S, Screen.w * 0.35f);
	const CUIRect Search = {Screen.x + Screen.w - Margin - SearchWidth, Screen.y + 72.0f * S, SearchWidth, 44.0f * S};
	BestClientSettingsSearch::RenderElySearch(this, Ui(), GameClient(), Input(), Search);

#if !defined(CONF_PLATFORM_ANDROID) && !defined(CONF_PLATFORM_EMSCRIPTEN) && !defined(CONF_PLATFORM_IOS)
	static CButtonContainer s_aWindows[3];
	const char *apWindowIcons[] = {FontIcon::MINUS, "\uF2D0", FontIcon::XMARK};
	for(int i = 0; i < (Ingame ? 0 : 3); ++i)
	{
		const CUIRect Rect = {Screen.x + Screen.w - (184.0f - i * 60.0f) * S, Screen.y, 60.0f * S, 63.0f * S};
		if(Ui()->MouseHovered(&Rect))
			Rect.Draw(ColorRGBA(1, 1, 1, 0.08f), IGraphics::CORNER_NONE, 0);
		if(i == 1)
		{
			CUIRect Square = {Rect.x + Rect.w * 0.5f - 8.0f * S, Rect.y + Rect.h * 0.5f - 8.0f * S, 16.0f * S, 16.0f * S};
			Square.Draw(ColorRGBA(0.96f, 0.96f, 0.97f, 1), IGraphics::CORNER_ALL, 1.4f * S);
			Square.Margin(1.8f * S, &Square);
			Square.Draw(ColorRGBA(0.035f, 0.031f, 0.043f, 1), IGraphics::CORNER_NONE, 0);
		}
		else
			Icon(Rect, apWindowIcons[i], 22.0f);
		if(Click(&s_aWindows[i], Rect))
		{
			if(i == 0)
				static_cast<IEngineGraphics *>(Graphics())->Minimize();
			else if(i == 1)
				static_cast<IEngineGraphics *>(Graphics())->ToggleMaximize();
			else if(g_Config.m_BcConfirmQuit || GameClient()->Editor()->HasUnsavedData() || Client()->State() == IClient::STATE_ONLINE)
				ShowQuitPopup();
			else
				Client()->Quit();
		}
	}
#endif

	if(g_Config.m_UiSettingsPage == SETTINGS_LANGUAGE || g_Config.m_UiSettingsPage == SETTINGS_PLAYER)
		g_Config.m_UiSettingsPage = g_Config.m_UiSettingsPage == SETTINGS_LANGUAGE ? SETTINGS_GENERAL : SETTINGS_TEE;

	const int aPages[] = {SETTINGS_GENERAL, SETTINGS_TEE, SETTINGS_APPEARANCE, SETTINGS_ASSETS, SETTINGS_CONTROLS, SETTINGS_GRAPHICS, SETTINGS_SOUND, SETTINGS_DDNET, SETTINGS_TCLIENT, SETTINGS_BESTCLIENT, SETTINGS_MUSIC_EFFECTS, SETTINGS_TOUCH};
	
	const char *apTitles[] = {Localize("General"), Localize("Tee"), Localize("Appearance"), Localize("Assets"), Localize("Controls"), Localize("Graphics"), Localize("Sound"), Localize("DDNet"), "TClient", "Ely Client", "Music & Effects", Localize("Touch")};
	constexpr int NumPages = 12;
	int Selected = 0;
	for(int i = 0; i < NumPages; ++i)
	{
		if(g_Config.m_UiSettingsPage == aPages[i])
			Selected = i;
	}
	if(g_Config.m_UiSettingsPage == SETTINGS_PROFILES || g_Config.m_UiSettingsPage == SETTINGS_CONFIGS || g_Config.m_UiSettingsPage == SETTINGS_CREDITS)
		Selected = 8;

	#if defined(CONF_PLATFORM_IOS)
	const float DockWidth = Screen.w - Margin * 2.0f;
#else
	const float DockWidth = std::min(770.0f * S, Screen.w - Margin * 2.0f);
#endif
	#if defined(CONF_PLATFORM_IOS)
	const CUIRect Dock = {Screen.x + Margin, Screen.y + Screen.h - 86.0f, DockWidth, 72.0f};
#else
	const CUIRect Dock = {Screen.x + (Screen.w - DockWidth) * 0.5f, Screen.y + Screen.h - 78.0f * S, DockWidth, 60.0f * S};
#endif
	ElyCard(Dock, Dock.h * 0.5f);
	const float SlotWidth = (Dock.w - 20.0f * S) / NumPages;
	static float s_SelectedPosition = 0.0f;
	const float Blend = g_Config.m_BcAnimations ? 1.0f - std::exp(-Client()->RenderFrameTime() * 18.0f) : 1.0f;
	s_SelectedPosition = mix(s_SelectedPosition, (float)Selected, Blend);
	const CUIRect Selection = {Dock.x + 10.0f * S + s_SelectedPosition * SlotWidth, Dock.y + 7.0f * S, SlotWidth, Dock.h - 14.0f * S};
	Selection.Draw(ColorRGBA(0.28f, 0.28f, 0.31f, 0.92f), IGraphics::CORNER_ALL, Selection.h * 0.5f);
	static CButtonContainer s_aDock[NumPages];
	for(int i = 0; i < NumPages; ++i)
	{
		const CUIRect Slot = {Dock.x + 10.0f * S + i * SlotWidth, Dock.y + 7.0f * S, SlotWidth, Dock.h - 14.0f * S};
		TextRender()->TextColor(1, 1, 1, Selected == i || Ui()->MouseHovered(&Slot) ? 1.0f : 0.78f);
		static const char *s_apMaterialIcons[] = {"\uE8B8", "\uE7FD", "\uE40A", "\uE2C7", "\uE312", "\uE30A", "\uE050", "\uF06E", "\uE429", "\uE65F", "\uE405", "\uE913"};
		TextRender()->SetCustomFace("Material Icons Outlined");
		Label(Slot, s_apMaterialIcons[i], 30.0f, TEXTALIGN_MC);
		TextRender()->SetCustomFace("DejaVu Sans");
		GameClient()->m_Tooltips.DoToolTip(&s_aDock[i], &Slot, apTitles[i]);
		if(Click(&s_aDock[i], Slot))
		{
			g_Config.m_UiSettingsPage = aPages[i];
			Selected = i;
		}
	}
	TextRender()->TextColor(0.96f, 0.96f, 0.975f, 1);

	const bool NeedRestart = m_NeedRestartGraphics || m_NeedRestartSound || m_NeedRestartUpdate || m_NeedRestartDiscord;
	CUIRect RestartBar = {Screen.x + Margin, Dock.y - 34.0f * S, Screen.w - Margin * 2, 25.0f * S};
	const char *pHeading = apTitles[Selected];
	if(g_Config.m_UiSettingsPage == SETTINGS_PROFILES)
		pHeading = Localize("Profiles");
	else if(g_Config.m_UiSettingsPage == SETTINGS_CONFIGS)
		pHeading = Localize("Configs");
	else if(g_Config.m_UiSettingsPage == SETTINGS_CREDITS)
		pHeading = Localize("Credits");
	Label({Screen.x + Margin, Screen.y + 78.0f * S, Screen.w * 0.52f, 36.0f * S}, pHeading, 28.0f);

	MainView = {Screen.x + Margin, Screen.y + 148.0f * S, Screen.w - 2 * Margin, Dock.y - Screen.y - 164.0f * S};
	if(NeedRestart)
		MainView.h -= 35.0f * S;
	if(g_Config.m_UiSettingsPage == SETTINGS_GENERAL && Screen.w / Screen.h > 1.5f)
		MainView.w *= 0.92f;

	if(Selected == 8)
	{
		static CButtonContainer s_aExtras[4];
		const int aExtraPages[] = {SETTINGS_TCLIENT, SETTINGS_PROFILES, SETTINGS_CONFIGS, SETTINGS_CREDITS};
		const char *apExtras[] = {"TClient", Localize("Profiles"), Localize("Configs"), Localize("Credits")};
		CUIRect Tabs;
		MainView.HSplitTop(30.0f * S, &Tabs, &MainView);
		MainView.HSplitTop(12.0f * S, nullptr, &MainView);
		for(int i = 0; i < 4; ++i)
		{
			CUIRect Button;
			Tabs.VSplitLeft(Tabs.w / (4 - i), &Button, &Tabs);
			Button.VMargin(3.0f * S, &Button);
			if(DoButton_Menu(&s_aExtras[i], apExtras[i], g_Config.m_UiSettingsPage == aExtraPages[i], &Button))
				g_Config.m_UiSettingsPage = aExtraPages[i];
		}
	}

	const int Key = g_Config.m_UiSettingsPage * 10000 + GetTClientSettingsTab() * 100 + GetBestClientSettingsTab();
	static int s_LastKey = -1;
	static int64_t s_LastFrame = 0;
	static float s_Reveal = 0;
	const int64_t Now = time_get();
	if(Key != s_LastKey || Now - s_LastFrame > time_freq() / 2)
	{
		s_LastKey = Key;
		s_Reveal = 0;
	}
	s_LastFrame = Now;
	s_Reveal = g_Config.m_BcAnimations ? std::min(1.0f, s_Reveal + Client()->RenderFrameTime() / 0.19f) : 1.0f;
	const float Eased = 1.0f - std::pow(1.0f - s_Reveal, 3.0f);
	CUIRect Animated = MainView;
	Animated.y += (1.0f - Eased) * 18.0f * S;
	const bool Enabled = Ui()->Enabled();
	if(s_Reveal < 0.65f && !Ui()->IsPopupOpen())
		Ui()->SetEnabled(false);
	if(g_Config.m_UiSettingsPage == SETTINGS_GENERAL)
		RenderSettingsElyGeneral(Animated);
	else if(g_Config.m_UiSettingsPage == SETTINGS_MUSIC_EFFECTS)
		RenderSettingsElyMusicEffects(Animated);
	else
	{
		Ui()->ClipEnable(&MainView);
		RenderSettingsPage(Animated);
		Ui()->ClipDisable();
	}
	Ui()->SetEnabled(Enabled);
	if(Eased < 1.0f)
		MainView.Draw(ColorRGBA(0.035f, 0.035f, 0.045f, (1.0f - Eased) * 0.94f), IGraphics::CORNER_NONE, 0);

	if(NeedRestart)
	{
		// bestclient
		CUIRect RestartWarning, RestartButton;
#if defined(CONF_AUTOUPDATE)
		const bool UpdateReady = Updater()->GetCurrentState() == IUpdater::NEED_RESTART;
#else
		const bool UpdateReady = false;
#endif
		if(UpdateReady)
		{
			TextRender()->TextColor(0.7f, 1.0f, 0.7f, 1.0f);
			Ui()->DoLabel(&RestartBar, Localize("BestClient update downloaded! Restart to apply."), 14.0f, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else
		{
			RestartBar.VSplitRight(125.0f, &RestartWarning, &RestartButton);
			RestartWarning.VSplitRight(10.0f, &RestartWarning, nullptr);
			Ui()->DoLabel(&RestartWarning, Localize("You must restart the game for all settings to take effect."), 14.0f, TEXTALIGN_ML);

			static CButtonContainer s_RestartButton;
			if(DoButton_Menu(&s_RestartButton, Localize("Restart"), 0, &RestartButton))
			{
				if(Client()->State() == IClient::STATE_ONLINE || GameClient()->Editor()->HasUnsavedData())
				{
					m_Popup = POPUP_RESTART;
				}
				else
				{
					Client()->Restart();
				}
			}
		}
		// bestclient
	}
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	BestClientApplyMenuFont(TextRender());
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

bool CMenus::RenderHslaScrollbars(CUIRect *pRect, unsigned int *pColor, bool Alpha, float DarkestLight)
{
	const unsigned PrevPackedColor = *pColor;
	ColorHSLA Color(*pColor, Alpha);
	const ColorHSLA OriginalColor = Color;
	const char *apLabels[] = {Localize("Hue"), Localize("Sat."), Localize("Lht."), Localize("Alpha")};
	const float SizePerEntry = 20.0f;
	const float MarginPerEntry = 5.0f;
	const float PreviewMargin = 2.5f;
	const float PreviewHeight = 40.0f + 2 * PreviewMargin;
	const float OffY = (SizePerEntry + MarginPerEntry) * (3 + (Alpha ? 1 : 0)) - PreviewHeight;

	CUIRect Preview;
	pRect->VSplitLeft(PreviewHeight, &Preview, pRect);
	Preview.HSplitTop(OffY / 2.0f, nullptr, &Preview);
	Preview.HSplitTop(PreviewHeight, &Preview, nullptr);

	Preview.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 1.0f), IGraphics::CORNER_ALL, 4.0f + PreviewMargin);
	Preview.Margin(PreviewMargin, &Preview);
	Preview.Draw(color_cast<ColorRGBA>(Color.UnclampLighting(DarkestLight)), IGraphics::CORNER_ALL, 4.0f + PreviewMargin);

	auto &&RenderHueRect = [&](CUIRect *pColorRect) {
		float CurXOff = pColorRect->x;
		const float SizeColor = pColorRect->w / 6;

		// red to yellow
		{
			Graphics()->SetColor4(
				ColorRGBA(1, 0, 0, 1),
				ColorRGBA(1, 1, 0, 1),
				ColorRGBA(1, 1, 0, 1),
				ColorRGBA(1, 0, 0, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		// yellow to green
		CurXOff += SizeColor;
		{
			Graphics()->SetColor4(
				ColorRGBA(1, 1, 0, 1),
				ColorRGBA(0, 1, 0, 1),
				ColorRGBA(0, 1, 0, 1),
				ColorRGBA(1, 1, 0, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// green to turquoise
		{
			Graphics()->SetColor4(
				ColorRGBA(0, 1, 0, 1),
				ColorRGBA(0, 1, 1, 1),
				ColorRGBA(0, 1, 1, 1),
				ColorRGBA(0, 1, 0, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// turquoise to blue
		{
			Graphics()->SetColor4(
				ColorRGBA(0, 1, 1, 1),
				ColorRGBA(0, 0, 1, 1),
				ColorRGBA(0, 0, 1, 1),
				ColorRGBA(0, 1, 1, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// blue to purple
		{
			Graphics()->SetColor4(
				ColorRGBA(0, 0, 1, 1),
				ColorRGBA(1, 0, 1, 1),
				ColorRGBA(1, 0, 1, 1),
				ColorRGBA(0, 0, 1, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}

		CurXOff += SizeColor;
		// purple to red
		{
			Graphics()->SetColor4(
				ColorRGBA(1, 0, 1, 1),
				ColorRGBA(1, 0, 0, 1),
				ColorRGBA(1, 0, 0, 1),
				ColorRGBA(1, 0, 1, 1));

			IGraphics::CFreeformItem Freeform(
				CurXOff, pColorRect->y,
				CurXOff + SizeColor, pColorRect->y,
				CurXOff, pColorRect->y + pColorRect->h,
				CurXOff + SizeColor, pColorRect->y + pColorRect->h);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}
	};

	auto &&RenderSaturationRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColor) {
		ColorHSLA LeftColor = color_cast<ColorHSLA>(CurColor);
		ColorHSLA RightColor = color_cast<ColorHSLA>(CurColor);

		LeftColor.s = 0.0f;
		RightColor.s = 1.0f;

		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(LeftColor);
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(RightColor);

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	auto &&RenderLightingRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColor) {
		ColorHSLA LeftColor = color_cast<ColorHSLA>(CurColor);
		ColorHSLA RightColor = color_cast<ColorHSLA>(CurColor);

		LeftColor.l = DarkestLight;
		RightColor.l = 1.0f;

		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(LeftColor);
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(RightColor);

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	auto &&RenderAlphaRect = [&](CUIRect *pColorRect, const ColorRGBA &CurColorFull) {
		const ColorRGBA LeftColorRGBA = color_cast<ColorRGBA>(color_cast<ColorHSLA>(CurColorFull).WithAlpha(0.0f));
		const ColorRGBA RightColorRGBA = color_cast<ColorRGBA>(color_cast<ColorHSLA>(CurColorFull).WithAlpha(1.0f));

		Graphics()->SetColor4(LeftColorRGBA, RightColorRGBA, RightColorRGBA, LeftColorRGBA);

		IGraphics::CFreeformItem Freeform(
			pColorRect->x, pColorRect->y,
			pColorRect->x + pColorRect->w, pColorRect->y,
			pColorRect->x, pColorRect->y + pColorRect->h,
			pColorRect->x + pColorRect->w, pColorRect->y + pColorRect->h);
		Graphics()->QuadsDrawFreeform(&Freeform, 1);
	};

	for(int i = 0; i < 3 + Alpha; i++)
	{
		CUIRect Button, Label;
		pRect->HSplitTop(SizePerEntry, &Button, pRect);
		pRect->HSplitTop(MarginPerEntry, nullptr, pRect);
		Button.VSplitLeft(140.0f, &Label, &Button);
		Label.VMargin(10.0f, &Label);

		Button.Draw(ColorRGBA(0.15f, 0.15f, 0.15f, 1.0f), IGraphics::CORNER_ALL, 1.0f);

		CUIRect Rail;
		Button.Margin(2.0f, &Rail);

		char aBuf[32];

		// Hue
		if(i == 0)
			str_format(aBuf, sizeof(aBuf), "%s: %.1f° (%03d)", apLabels[i], Color[i] * 360.0f, round_to_int(Color[i] * 255.0f));
		// Lht
		else if(i == 2)
		{
			// handle internal light clamping, see `UnclampLighting`
			float Lht = DarkestLight + Color[i] * (1.0f - DarkestLight);
			str_format(aBuf, sizeof(aBuf), "%s: %.1f%% (%03d)", apLabels[i], Lht * 100.0f, round_to_int(Color[i] * 255.0f));
		}
		// Sat and Alpha
		else
			str_format(aBuf, sizeof(aBuf), "%s: %.1f%% (%03d)", apLabels[i], Color[i] * 100.0f, round_to_int(Color[i] * 255.0f));
		Ui()->DoLabel(&Label, aBuf, 12.0f, TEXTALIGN_ML);

		ColorRGBA HandleColor;
		Graphics()->TextureClear();
		Graphics()->TrianglesBegin();
		if(i == 0)
		{
			RenderHueRect(&Rail);
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, 1.0f, 0.5f, 1.0f));
		}
		else if(i == 1)
		{
			RenderSaturationRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, 1.0f, 0.5f, 1.0f)));
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, 0.5f, 1.0f));
		}
		else if(i == 2)
		{
			RenderLightingRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, 0.5f, 1.0f)));
			HandleColor = color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, Color.l, 1.0f).UnclampLighting(DarkestLight));
		}
		else if(i == 3)
		{
			RenderAlphaRect(&Rail, color_cast<ColorRGBA>(ColorHSLA(Color.h, Color.s, Color.l, 1.0f).UnclampLighting(DarkestLight)));
			HandleColor = color_cast<ColorRGBA>(Color.UnclampLighting(DarkestLight));
		}
		Graphics()->TrianglesEnd();

		Color[i] = Ui()->DoScrollbarH(&((char *)pColor)[i], &Button, Color[i], &HandleColor);
	}

	if(OriginalColor != Color)
	{
		*pColor = Color.Pack(Alpha);
	}
	return PrevPackedColor != *pColor;
}
