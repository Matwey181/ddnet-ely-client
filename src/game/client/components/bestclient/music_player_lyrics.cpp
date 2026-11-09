/* Copyright © 2026 BestProject Team */
#include "music_player_lyrics.h"
#include <engine/shared/config.h>

#include <base/dbg.h>
#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/http.h>
#include <engine/shared/json.h>
#include <engine/textrender.h>

#include <game/client/bc_ui_animations.h>
#include <game/client/components/bestclient/version.h>
#include <game/client/ui.h>
#include <game/version.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>

namespace
{
	static constexpr float LYRICS_SLOT_WIDTH = 70.0f;
	static constexpr float LYRICS_LINE_SLIDE_MS = 260.0f;
	static constexpr float LYRICS_TITLE_MARQUEE_GAP_FACTOR = 2.5f;
	static constexpr ColorRGBA LYRICS_PASSED_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
	static constexpr ColorRGBA LYRICS_UPCOMING_COLOR(0.45f, 0.45f, 0.48f, 1.0f);
	static constexpr size_t LYRICS_CACHE_MAX = 64;

	static bool IsLyricsWhitespace(int Codepoint)
	{
		return (Codepoint >= 9 && Codepoint <= 13) || Codepoint == 0x20 || Codepoint == 0x85 || Codepoint == 0xA0 ||
			Codepoint == 0x1680 || (Codepoint >= 0x2000 && Codepoint <= 0x200A) || Codepoint == 0x2028 || Codepoint == 0x2029 ||
			Codepoint == 0x202F || Codepoint == 0x205F || Codepoint == 0x3000;
	}

	static const char *JsonStringOrEmpty(const json_value *pValue)
	{
		if(pValue == nullptr || pValue == &json_value_none || pValue->type != json_string)
			return "";
		const char *pStr = json_string_get(pValue);
		return pStr != nullptr ? pStr : "";
	}

	class CTextWidthCacheEntry
	{
	public:
		std::string m_Text;
		float m_FontSize = -1.0f;
		float m_Width = 0.0f;
	};
	static constexpr size_t TEXT_CACHE_SIZE = 64;
	static CTextWidthCacheEntry s_aLyricsTextWidthCache[TEXT_CACHE_SIZE];

	static float CachedTextWidth(ITextRender *pTextRender, const char *pText, float FontSize)
	{
		if(pTextRender == nullptr || pText == nullptr || pText[0] == '\0')
			return 0.0f;

		size_t Hash = std::hash<std::string_view>{}(pText);
		Hash ^= std::hash<float>{}(FontSize) + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
		size_t Index = Hash % TEXT_CACHE_SIZE;

		if(s_aLyricsTextWidthCache[Index].m_FontSize == FontSize && s_aLyricsTextWidthCache[Index].m_Text == pText)
			return s_aLyricsTextWidthCache[Index].m_Width;

		float Width = pTextRender->TextWidth(FontSize, pText, -1, -1.0f);
		s_aLyricsTextWidthCache[Index].m_Text = pText;
		s_aLyricsTextWidthCache[Index].m_FontSize = FontSize;
		s_aLyricsTextWidthCache[Index].m_Width = Width;
		return Width;
	}

	static void InvalidateLyricsTextWidthCache()
	{
		for(auto &Entry : s_aLyricsTextWidthCache)
		{
			Entry.m_FontSize = -1.0f;
			Entry.m_Text.clear();
			Entry.m_Width = 0.0f;
		}
	}
} // namespace

float CMusicPlayerLyrics::LyricsTextSlotWidth(float Scale, float WidthScale)
{
	return LYRICS_SLOT_WIDTH * Scale * WidthScale;
}

void CMusicPlayerLyrics::TickDisplay(float Delta)
{
	if(m_DisplayState == EDisplayState::NotFound)
		m_NotFoundDisplayMs += std::max(0.0f, Delta) * 1000.0f;
	else if(m_DisplayState == EDisplayState::Offline)
		m_OfflineDisplayMs += std::max(0.0f, Delta) * 1000.0f;
}

int CMusicPlayerLyrics::ResolveDisplayLineIndex() const
{
	if(m_DisplayState == EDisplayState::Idle)
		return FALLBACK_BRAND;
	if(m_DisplayState == EDisplayState::Loading)
		return m_TrackTitle.empty() ? FALLBACK_ELLIPSIS : FALLBACK_TITLE;
	if(m_DisplayState == EDisplayState::NotFound)
		return (m_NotFoundDisplayMs < (float)NOT_FOUND_HOLD_MS) ? FALLBACK_NOT_FOUND : FALLBACK_TITLE;
	if(m_DisplayState == EDisplayState::Offline)
	{
		if(m_OfflineDisplayMs < (float)OFFLINE_HOLD_MS)
			return FALLBACK_NO_CONNECTION;
		if(!m_TrackTitle.empty())
			return FALLBACK_TITLE;
		return FALLBACK_BRAND;
	}

	if(m_DisplayState != EDisplayState::Ready)
		return LINE_NONE;

	const int64_t PositionMs = CurrentPositionMs();
	int LineIndex = FindLineIndex(PositionMs);
	if(LineIndex < 0 && !m_vLines.empty())
	{
		const int64_t CountdownRemainingMs = m_vLines.front().m_StartMs - PositionMs;
		if(CountdownRemainingMs > 2000)
			return -3;
		if(CountdownRemainingMs > 1000)
			return -2;
		if(CountdownRemainingMs > 0)
			return -1;
		return 0;
	}
	return LineIndex;
}

float CMusicPlayerLyrics::PreferredTextSlotWidth(ITextRender *pTextRender, float FontSize, float MaxWidth, float Scale, float WidthScale) const
{
	const float ClampedMax = std::max(0.0f, MaxWidth);
	if(ClampedMax <= 0.0f)
		return 0.0f;

	const int LineIndex = ResolveDisplayLineIndex();
	const bool ShowBrand = LineIndex == FALLBACK_BRAND;
	const bool ShowTitle = LineIndex == FALLBACK_TITLE;
	if(!ShowBrand && !ShowTitle)
		return ClampedMax;

	const char *pText = ShowBrand ? FallbackText(FALLBACK_BRAND) : FallbackText(FALLBACK_TITLE);
	if(pTextRender == nullptr || pText == nullptr || pText[0] == '\0')
		return ClampedMax;

	const float Pad = 1.2f * Scale * WidthScale;
	const float TextW = CachedTextWidth(pTextRender, pText, FontSize);
	return std::clamp(TextW + Pad * 2.0f, 0.0f, ClampedMax);
}

void CMusicPlayerLyrics::Reset()
{
	AbortRequest();
	m_ActiveKey.clear();
	m_RequestKey.clear();
	m_DisplayState = EDisplayState::Idle;
	m_vLines.clear();
	ClearActiveTrack();
}

void CMusicPlayerLyrics::ClearActiveTrack()
{
	m_ElyCurrentLine = -1;
	m_ElyCurrentWord = m_ElyPreviousWord = -1;
	m_ElyLineTransition = m_ElyWordTransition = 1.0f;
	m_ElyLayoutLine = -1;
	m_vElyFlowWords.clear();
	m_CurrentLineIndex = LINE_NONE;
	m_OutgoingLineIndex = LINE_NONE;
	m_LineTransitionT = 1.0f;
	m_LayoutValid = false;
	m_LayoutText.clear();
	m_vCharMetrics.clear();
	m_BaseLineWidth = 0.0f;
	m_NotFoundDisplayMs = 0.0f;
	m_OfflineDisplayMs = 0.0f;
	m_TitleMarqueeOffset = 0.0f;
	m_ClockPositionMs = 0;
	m_ClockTick = 0;
	m_ClockPlaying = false;
	m_ClockDurationMs = 0;
	m_LastSnapshotPositionMs = -1;
}

void CMusicPlayerLyrics::ClearLayoutState()
{
	m_ElyCurrentLine = -1;
	m_ElyCurrentWord = m_ElyPreviousWord = -1;
	m_ElyLineTransition = m_ElyWordTransition = 1.0f;
	m_ElyLayoutLine = -1;
	m_vElyFlowWords.clear();
	m_CurrentLineIndex = LINE_NONE;
	m_OutgoingLineIndex = LINE_NONE;
	m_LineTransitionT = 1.0f;
	m_LayoutValid = false;
	m_LayoutText.clear();
	m_vCharMetrics.clear();
	m_BaseLineWidth = 0.0f;
	InvalidateLyricsTextWidthCache();
}

void CMusicPlayerLyrics::Disable()
{
	if(!IsActive() && m_DisplayState == EDisplayState::Idle)
		return;
	AbortRequest();
	m_ActiveKey.clear();
	m_DisplayState = EDisplayState::Idle;
	m_vLines.clear();
	ClearActiveTrack();
}

void CMusicPlayerLyrics::AbortRequest()
{
	if(m_pRequest)
	{
		m_pRequest->Abort();
		m_pRequest.reset();
	}
	m_RequestKey.clear();
}

std::string CMusicPlayerLyrics::BuildCacheKey(const char *pTitle, const char *pArtist, int64_t DurationMs)
{
	(void)DurationMs;
	std::string Key;
	Key.reserve(128);
	Key += "v3|";
	Key += pArtist ? pArtist : "";
	Key += '|';
	Key += pTitle ? pTitle : "";
	return Key;
}

bool CMusicPlayerLyrics::ParseLrcTimestamp(const char *pText, int64_t &OutMs, const char **ppEnd)
{
	if(pText == nullptr || (pText[0] != '[' && pText[0] != '<'))
		return false;
	const char Closing = pText[0] == '[' ? ']' : '>';

	int Minutes = 0;
	int Seconds = 0;
	int Fraction = 0;
	int FractionDigits = 0;
	const char *p = pText + 1;
	if(!std::isdigit((unsigned char)*p))
		return false;

	while(std::isdigit((unsigned char)*p))
	{
		Minutes = Minutes * 10 + (*p - '0');
		if(Minutes > 9999)
			return false;
		++p;
	}
	if(*p != ':')
		return false;
	++p;
	if(!std::isdigit((unsigned char)*p))
		return false;
	while(std::isdigit((unsigned char)*p))
	{
		Seconds = Seconds * 10 + (*p - '0');
		if(Seconds >= 60)
			return false;
		++p;
	}
	if(*p == '.' || *p == ',')
	{
		++p;
		while(std::isdigit((unsigned char)*p) && FractionDigits < 3)
		{
			Fraction = Fraction * 10 + (*p - '0');
			++FractionDigits;
			++p;
		}
		while(std::isdigit((unsigned char)*p))
			++p;
	}
	if(*p != Closing)
		return false;
	++p;

	while(FractionDigits > 0 && FractionDigits < 3)
	{
		Fraction *= 10;
		++FractionDigits;
	}

	OutMs = (int64_t)Minutes * 60000 + (int64_t)Seconds * 1000 + Fraction;
	if(ppEnd)
		*ppEnd = p;
	return true;
}

bool CMusicPlayerLyrics::ParseSyncedLyrics(const char *pSyncedLyrics, std::vector<SLine> &vOut)
{
	vOut.clear();
	if(pSyncedLyrics == nullptr || pSyncedLyrics[0] == '\0')
		return false;

	const char *p = pSyncedLyrics;
	std::vector<int64_t> vTimestamps;
	while(*p)
	{
		while(*p == '\r' || *p == '\n')
			++p;
		if(*p == '\0')
			break;

		const char *pLineStart = p;
		while(*p && *p != '\n' && *p != '\r')
			++p;
		const char *pLineEnd = p;

		vTimestamps.clear();
		const char *pCursor = pLineStart;
		while(pCursor < pLineEnd)
		{
			int64_t TimestampMs = 0;
			const char *pAfter = nullptr;
			if(*pCursor != '[' || !ParseLrcTimestamp(pCursor, TimestampMs, &pAfter) || pAfter == nullptr || pAfter > pLineEnd)
				break;
			vTimestamps.push_back(TimestampMs);
			pCursor = pAfter;
		}

		while(pCursor < pLineEnd && (*pCursor == ' ' || *pCursor == '\t'))
			++pCursor;

		if(vTimestamps.empty())
			continue;

		std::string Text;
		std::vector<std::pair<int, int64_t>> vWordMarks;
		while(pCursor < pLineEnd)
		{
			int64_t TimestampMs;
			const char *pAfter = nullptr;
			if(*pCursor == '<' && ParseLrcTimestamp(pCursor, TimestampMs, &pAfter) && pAfter <= pLineEnd)
			{
				vWordMarks.emplace_back((int)Text.size(), TimestampMs);
				pCursor = pAfter;
			}
			else
				Text += *pCursor++;
		}
		while(!Text.empty() && (Text.back() == ' ' || Text.back() == '\t'))
			Text.pop_back();

		if(Text.empty())
			continue;

		for(int64_t TimestampMs : vTimestamps)
		{
			SLine Line;
			Line.m_StartMs = TimestampMs;
			Line.m_Text = Text;
			const char *pText = Text.c_str();
			while(*pText)
			{
				const char *pStart = pText;
				const int Codepoint = str_utf8_decode(&pText);
				if(IsLyricsWhitespace(Codepoint))
					continue;
				while(*pText)
				{
					const char *pNext = pText;
					if(IsLyricsWhitespace(str_utf8_decode(&pNext)))
						break;
					pText = pNext;
				}
				SWord Word;
				Word.m_ByteOffset = pStart - Text.c_str();
				Word.m_ByteLength = pText - pStart;
				Line.m_vWords.push_back(Word);
			}
			for(const auto &[Offset, Time] : vWordMarks)
			{
				const int64_t ShiftedTime = std::max<int64_t>(0, Time + TimestampMs - vTimestamps.front());
				const auto It = std::find_if(Line.m_vWords.begin(), Line.m_vWords.end(), [&](const SWord &Word) {
					return Word.m_ByteOffset + Word.m_ByteLength > Offset;
				});
				if(It == Line.m_vWords.end())
				{
					if(!Line.m_vWords.empty())
						Line.m_vWords.back().m_EndMs = ShiftedTime;
				}
				else if(!It->m_ExactStart)
				{
					It->m_StartMs = ShiftedTime;
					It->m_ExactStart = true;
					Line.m_HasWordTiming = true;
				}
			}
			vOut.push_back(std::move(Line));
		}
	}

	if(vOut.empty())
		return false;

	std::stable_sort(vOut.begin(), vOut.end(), [](const SLine &A, const SLine &B) {
		return A.m_StartMs < B.m_StartMs;
	});
	MergeConsecutiveIdenticalLines(vOut);
	for(size_t i = 0; i < vOut.size(); ++i)
		BuildWordTimings(vOut[i], i + 1 < vOut.size() ? vOut[i + 1].m_StartMs : vOut[i].m_StartMs + 4000);
	return !vOut.empty();
}

void CMusicPlayerLyrics::BuildWordTimings(SLine &Line, int64_t EndMs)
{
	if(Line.m_vWords.empty())
		return;
	std::vector<float> vWeights;
	for(const auto &Word : Line.m_vWords)
	{
		float Weight = 0.0f;
		const char *p = Line.m_Text.c_str() + Word.m_ByteOffset;
		const char *pEnd = p + Word.m_ByteLength;
		while(p < pEnd)
		{
			const int Glyph = str_utf8_decode(&p);
			Weight += Glyph == ',' || Glyph == ';' || Glyph == ':' ? 0.45f : Glyph == '.' || Glyph == '!' || Glyph == '?' ? 0.75f : 1.0f;
		}
		vWeights.push_back(std::max(1.0f, Weight));
	}
	if(Line.m_vWords.front().m_StartMs < 0)
		Line.m_vWords.front().m_StartMs = Line.m_StartMs;
	EndMs = std::max(Line.m_StartMs + 1, Line.m_vWords.back().m_EndMs >= 0 ? Line.m_vWords.back().m_EndMs : EndMs);
	size_t First = 0;
	while(First < Line.m_vWords.size())
	{
		size_t Next = First + 1;
		while(Next < Line.m_vWords.size() && !Line.m_vWords[Next].m_ExactStart)
			++Next;
		const int64_t PreviousEnd = First > 0 ? Line.m_vWords[First - 1].m_EndMs : Line.m_StartMs;
		const int64_t Start = std::max(PreviousEnd, Line.m_vWords[First].m_StartMs);
		const int64_t End = std::max(Start + 1, Next < Line.m_vWords.size() ? Line.m_vWords[Next].m_StartMs : EndMs);
		float TotalWeight = 0.0f;
		for(size_t i = First; i < Next; ++i)
			TotalWeight += vWeights[i];
		float Elapsed = 0.0f;
		for(size_t i = First; i < Next; ++i)
		{
			auto &Word = Line.m_vWords[i];
			Word.m_StartMs = Start + (int64_t)((End - Start) * (Elapsed / TotalWeight));
			Elapsed += vWeights[i];
			Word.m_EndMs = Start + (int64_t)((End - Start) * (Elapsed / TotalWeight));
		}
		First = Next;
	}
}

void CMusicPlayerLyrics::MergeConsecutiveIdenticalLines(std::vector<SLine> &vLines)
{
	if(vLines.size() < 2)
		return;

	static constexpr int64_t NearDuplicateMs = 150;

	std::vector<SLine> vMerged;
	vMerged.reserve(vLines.size());
	for(SLine &Line : vLines)
	{
		if(!vMerged.empty() &&
			vMerged.back().m_Text == Line.m_Text &&
			Line.m_StartMs - vMerged.back().m_StartMs <= NearDuplicateMs)
		{
			continue;
		}
		vMerged.push_back(std::move(Line));
	}
	vLines = std::move(vMerged);
}

void CMusicPlayerLyrics::ApplyCacheEntry(const SCacheEntry &Entry)
{
	m_DisplayState = Entry.m_State;
	m_vLines = Entry.m_vLines;
	ClearLayoutState();
	if(Entry.m_State == EDisplayState::NotFound)
		m_NotFoundDisplayMs = 0.0f;
}

void CMusicPlayerLyrics::StartRequest(IHttp *pHttp, const char *pTitle, const char *pArtist, const char *pAlbum, int64_t DurationMs)
{
	if(pHttp == nullptr)
	{
		m_DisplayState = EDisplayState::Offline;
		return;
	}

	AbortRequest();

	char aEscapedTitle[512];
	char aEscapedArtist[512];
	char aEscapedAlbum[512];
	EscapeUrl(aEscapedTitle, pTitle ? pTitle : "");
	EscapeUrl(aEscapedArtist, pArtist ? pArtist : "");
	EscapeUrl(aEscapedAlbum, pAlbum ? pAlbum : "");

	// Search tolerates album/version metadata differences from Windows media sessions.
	char aUrl[2048];
	str_format(aUrl, sizeof(aUrl), "https://lrclib.net/api/search?track_name=%s&artist_name=%s", aEscapedTitle, aEscapedArtist);

	m_pRequest = HttpGet(aUrl);
	m_pRequest->Timeout(CTimeout{10000, 0, 500, 10});
	m_pRequest->LogProgress(HTTPLOG::FAILURE);
	m_pRequest->FailOnErrorStatus(false);
	m_pRequest->HeaderString("Lrclib-Client", "BestClient/" BESTCLIENT_VERSION " (https://github.com/BestProjectTeam/BestClient)");
	m_RequestKey = m_ActiveKey;
	m_DisplayState = EDisplayState::Loading;
	pHttp->Run(m_pRequest);
}

void CMusicPlayerLyrics::ProcessRequest()
{
	if(!m_pRequest || !m_pRequest->Done())
		return;

	std::shared_ptr<IHttpRequest> pFinished = m_pRequest;
	m_pRequest.reset();
	const std::string FinishedKey = m_RequestKey;
	m_RequestKey.clear();
	const bool ApplyToActive = !FinishedKey.empty() && FinishedKey == m_ActiveKey;

	auto FailOffline = [&]() {
		if(!ApplyToActive)
			return;
		m_DisplayState = EDisplayState::Offline;
		m_vLines.clear();
		ClearActiveTrack();
	};

	if(pFinished->State() != EHttpState::DONE || pFinished->StatusCode() == 0)
	{
		FailOffline();
		return;
	}

	const int StatusCode = pFinished->StatusCode();
	if(StatusCode != 404 && (StatusCode < 200 || StatusCode >= 300))
	{
		FailOffline();
		return;
	}

	SCacheEntry Entry;
	Entry.m_State = EDisplayState::NotFound;
	if(StatusCode != 404)
	{
		json_value *pRoot = pFinished->ResultJson();
		const json_value *pCandidate = pRoot;
		if(pRoot && pRoot->type == json_array)
		{
			pCandidate = nullptr;
			double BestDistance = 1e30;
			for(unsigned i = 0; i < pRoot->u.array.length; ++i)
			{
				const json_value *pEntry = pRoot->u.array.values[i];
				if(pEntry->type != json_object || !JsonStringOrEmpty(json_object_get(pEntry, "syncedLyrics"))[0]) continue;
				const json_value *pSeconds = json_object_get(pEntry, "duration");
				const double Seconds = pSeconds->type == json_double ? pSeconds->u.dbl : (pSeconds->type == json_integer ? (double)pSeconds->u.integer : 0.0);
				const double Distance = m_ClockDurationMs > 0 ? std::fabs(Seconds - m_ClockDurationMs / 1000.0) : 0.0;
				if((m_ClockDurationMs <= 0 || Distance <= 10.0) && Distance < BestDistance) { BestDistance = Distance; pCandidate = pEntry; }
			}
		}
		if(pCandidate != nullptr && pCandidate != &json_value_none && pCandidate->type == json_object)
		{
			const char *pSynced = JsonStringOrEmpty(json_object_get(pCandidate, "syncedLyrics"));
			if(ParseSyncedLyrics(pSynced, Entry.m_vLines))
				Entry.m_State = EDisplayState::Ready;
		}
		if(pRoot)
			json_value_free(pRoot);
	}

	if(!FinishedKey.empty())
	{
		if(m_Cache.size() >= LYRICS_CACHE_MAX)
			m_Cache.clear();
		m_Cache[FinishedKey] = Entry;
	}
	if(ApplyToActive)
		ApplyCacheEntry(Entry);
}

int64_t CMusicPlayerLyrics::CurrentPositionMs() const
{
	int64_t Position = std::max<int64_t>(0, m_ClockPositionMs);
	if(m_ClockPlaying && m_ClockTick > 0)
		Position += ((time_get() - m_ClockTick) * 1000) / time_freq();
	if(m_ClockDurationMs > 0)
		Position = std::min(Position, m_ClockDurationMs);
	return Position;
}

void CMusicPlayerLyrics::SyncMediaClock(int64_t SnapshotPositionMs, int64_t DurationMs, bool Playing, bool ForceReset)
{
	SnapshotPositionMs = std::max<int64_t>(0, SnapshotPositionMs);
	m_ClockDurationMs = std::max<int64_t>(0, DurationMs);
	const int64_t Now = time_get();
	const int64_t ExpectedPosition = CurrentPositionMs();

	if(!ForceReset && m_ClockTick != 0 && SnapshotPositionMs + BACKWARDS_SEEK_THRESHOLD_MS < ExpectedPosition && SnapshotPositionMs != m_LastSnapshotPositionMs)
		ClearLayoutState();

	if(ForceReset || m_ClockTick == 0 || !Playing || !m_ClockPlaying)
	{
		m_ClockPositionMs = SnapshotPositionMs;
		m_LastSnapshotPositionMs = SnapshotPositionMs;
		m_ClockTick = Now;
		m_ClockPlaying = Playing;
		return;
	}

	// SMTC snapshots may repeat for several polls. Keep the running clock between updates.
	if(SnapshotPositionMs == m_LastSnapshotPositionMs)
		return;
	m_LastSnapshotPositionMs = SnapshotPositionMs;
	const int64_t Error = SnapshotPositionMs - ExpectedPosition;
	m_ClockPositionMs = std::abs(Error) > BACKWARDS_SEEK_THRESHOLD_MS ? SnapshotPositionMs :
		ExpectedPosition + std::clamp<int64_t>(Error / 8, -20, 20);
	m_ClockTick = Now;
	m_ClockPlaying = true;
	if(m_ClockDurationMs > 0)
		m_ClockPositionMs = std::min(m_ClockPositionMs, m_ClockDurationMs);
}

void CMusicPlayerLyrics::Update(IHttp *pHttp, const char *pTitle, const char *pArtist, const char *pAlbum, int64_t DurationMs, int64_t SnapshotPositionMs, bool Playing)
{
	ProcessRequest();

	if(pTitle != nullptr && pTitle[0] != '\0')
		m_TrackTitle = pTitle;

	const bool HasIdentity = pTitle && pTitle[0] != '\0' && pArtist && pArtist[0] != '\0';
	if(!HasIdentity)
	{
		if(!m_ActiveKey.empty())
		{
			m_ActiveKey.clear();
			m_DisplayState = EDisplayState::Idle;
			m_vLines.clear();
			m_TrackTitle.clear();
			ClearActiveTrack();
		}
		return;
	}

	const std::string Key = BuildCacheKey(pTitle, pArtist, DurationMs);
	const bool NewTrack = Key != m_ActiveKey;
	if(NewTrack)
	{
		if(m_pRequest && m_RequestKey == Key)
		{
			m_ActiveKey = Key;
			if(m_DisplayState == EDisplayState::Idle)
				m_DisplayState = EDisplayState::Loading;
			SyncMediaClock(SnapshotPositionMs, DurationMs, Playing, true);
			return;
		}

		AbortRequest();
		m_ActiveKey = Key;
		m_vLines.clear();
		ClearActiveTrack();
		SyncMediaClock(SnapshotPositionMs, DurationMs, Playing, true);

		const auto It = m_Cache.find(Key);
		if(It != m_Cache.end())
		{
			ApplyCacheEntry(It->second);
			return;
		}

		m_DisplayState = EDisplayState::Loading;
		StartRequest(pHttp, pTitle, pArtist, pAlbum, DurationMs);
		return;
	}

	SyncMediaClock(SnapshotPositionMs, DurationMs, Playing, false);
}

int CMusicPlayerLyrics::FindLineIndex(int64_t PositionMs) const
{
	if(m_vLines.empty())
		return -1;
	if(PositionMs < m_vLines.front().m_StartMs)
		return -1;

	int Lo = 0;
	int Hi = (int)m_vLines.size() - 1;
	int Best = 0;
	while(Lo <= Hi)
	{
		const int Mid = Lo + (Hi - Lo) / 2;
		if(m_vLines[Mid].m_StartMs <= PositionMs)
		{
			Best = Mid;
			Lo = Mid + 1;
		}
		else
		{
			Hi = Mid - 1;
		}
	}
	return Best;
}

float CMusicPlayerLyrics::LineProgress(int LineIndex, int64_t PositionMs) const
{
	if(LineIndex < 0 || LineIndex >= (int)m_vLines.size())
		return 0.0f;

	const int64_t StartMs = m_vLines[LineIndex].m_StartMs;
	int64_t EndMs = StartMs + 4000;
	if(LineIndex + 1 < (int)m_vLines.size())
		EndMs = std::max(StartMs + 1, m_vLines[LineIndex + 1].m_StartMs);

	if(PositionMs <= StartMs)
		return 0.0f;
	if(PositionMs >= EndMs)
		return 1.0f;
	return (float)(PositionMs - StartMs) / (float)(EndMs - StartMs);
}

float CMusicPlayerLyrics::CountdownProgress(int CountdownIndex, int64_t RemainingMs) const
{
	if(CountdownIndex == -3)
	{
		if(RemainingMs >= 3000)
			return 0.0f;
		return std::clamp((3000.0f - (float)RemainingMs) / 1000.0f, 0.0f, 1.0f);
	}
	if(CountdownIndex == -2)
		return std::clamp((2000.0f - (float)RemainingMs) / 1000.0f, 0.0f, 1.0f);
	if(CountdownIndex == -1)
		return std::clamp((1000.0f - (float)RemainingMs) / 1000.0f, 0.0f, 1.0f);
	return 0.0f;
}

const char *CMusicPlayerLyrics::FallbackText(int Index) const
{
	if(Index == FALLBACK_NO_CONNECTION)
		return "No connection";
	if(Index == FALLBACK_ELLIPSIS)
		return "…";
	if(Index == FALLBACK_NOT_FOUND)
		return "Lyrics not found";
	if(Index == FALLBACK_BRAND)
		return CLIENT_NAME;
	if(Index == FALLBACK_TITLE)
		return m_TrackTitle.empty() ? "Unknown title" : m_TrackTitle.c_str();
	return "";
}

void CMusicPlayerLyrics::EnsureLayout(ITextRender *pTextRender, float FontSize, int LineIndex)
{
	if(pTextRender == nullptr)
	{
		m_LayoutValid = false;
		return;
	}

	std::string Text;
	if(IsCountdownIndex(LineIndex))
	{
		char aDigit[4];
		str_format(aDigit, sizeof(aDigit), "%d", CountdownDigit(LineIndex));
		Text = aDigit;
	}
	else if(IsFallbackIndex(LineIndex))
	{
		Text = FallbackText(LineIndex);
	}
	else if(LineIndex >= 0 && LineIndex < (int)m_vLines.size())
	{
		Text = m_vLines[LineIndex].m_Text;
	}
	else
	{
		m_LayoutValid = false;
		return;
	}

	if(Text.empty())
	{
		m_LayoutValid = false;
		return;
	}

	if(m_LayoutValid && m_LayoutText == Text && std::fabs(m_LayoutFontSize - FontSize) < 0.01f)
		return;

	m_LayoutText = Text;
	m_LayoutFontSize = FontSize;
	m_vCharMetrics.clear();
	m_BaseLineWidth = pTextRender->TextWidth(FontSize, Text.c_str(), -1, -1.0f);

	const char *p = Text.c_str();
	while(*p)
	{
		const char *pPrev = p;
		const int Code = str_utf8_decode(&p);
		if(Code == 0)
			break;

		SCharMetric Metric;
		Metric.m_ByteOffset = (int)(pPrev - Text.c_str());
		Metric.m_ByteLength = (int)(p - pPrev);
		Metric.m_PrefixWidth = pTextRender->TextWidth(FontSize, Text.c_str(), Metric.m_ByteOffset, -1.0f);
		m_vCharMetrics.push_back(Metric);
	}

	m_LayoutValid = !m_vCharMetrics.empty();
}

void CMusicPlayerLyrics::BuildColorSplits(float ProgressChars, float Alpha, std::vector<STextColorSplit> &vOut) const
{
	vOut.clear();
	if(m_vCharMetrics.empty())
		return;

	const int CharCount = (int)m_vCharMetrics.size();
	const float Clamped = std::clamp(ProgressChars, 0.0f, (float)CharCount);
	const int FullyPassed = (int)Clamped;
	const float Frac = Clamped - (float)FullyPassed;

	auto WithA = [Alpha](ColorRGBA Color) {
		return Color.WithAlpha(Color.a * Alpha);
	};

	if(FullyPassed <= 0 && Frac <= 0.001f)
	{
		vOut.emplace_back(0, -1, WithA(LYRICS_UPCOMING_COLOR));
		return;
	}

	if(FullyPassed >= CharCount)
	{
		vOut.emplace_back(0, -1, WithA(LYRICS_PASSED_COLOR));
		return;
	}

	const int PassedEndByte = m_vCharMetrics[FullyPassed].m_ByteOffset;
	if(PassedEndByte > 0)
		vOut.emplace_back(0, PassedEndByte, WithA(LYRICS_PASSED_COLOR));

	const SCharMetric &Current = m_vCharMetrics[FullyPassed];
	if(Frac > 0.001f && Frac < 0.999f)
	{
		const ColorRGBA Blend(
			LYRICS_PASSED_COLOR.r + (LYRICS_UPCOMING_COLOR.r - LYRICS_PASSED_COLOR.r) * (1.0f - Frac),
			LYRICS_PASSED_COLOR.g + (LYRICS_UPCOMING_COLOR.g - LYRICS_PASSED_COLOR.g) * (1.0f - Frac),
			LYRICS_PASSED_COLOR.b + (LYRICS_UPCOMING_COLOR.b - LYRICS_PASSED_COLOR.b) * (1.0f - Frac),
			1.0f);
		vOut.emplace_back(Current.m_ByteOffset, Current.m_ByteLength, WithA(Blend));
		const int Next = FullyPassed + 1;
		if(Next < CharCount)
			vOut.emplace_back(m_vCharMetrics[Next].m_ByteOffset, -1, WithA(LYRICS_UPCOMING_COLOR));
	}
	else if(Frac >= 0.999f)
	{
		vOut.emplace_back(Current.m_ByteOffset, Current.m_ByteLength, WithA(LYRICS_PASSED_COLOR));
		const int Next = FullyPassed + 1;
		if(Next < CharCount)
			vOut.emplace_back(m_vCharMetrics[Next].m_ByteOffset, -1, WithA(LYRICS_UPCOMING_COLOR));
	}
	else
	{
		vOut.emplace_back(Current.m_ByteOffset, -1, WithA(LYRICS_UPCOMING_COLOR));
	}
}

float CMusicPlayerLyrics::PlayheadXInLine(float ProgressChars) const
{
	if(m_vCharMetrics.empty())
		return 0.0f;

	const int CharCount = (int)m_vCharMetrics.size();
	const float Clamped = std::clamp(ProgressChars, 0.0f, (float)CharCount);
	const int Index = std::min((int)Clamped, CharCount - 1);
	const float Frac = Clamped - (float)Index;

	const float Prefix = m_vCharMetrics[Index].m_PrefixWidth;
	float CharWidth = 0.0f;
	if(Index + 1 < CharCount)
		CharWidth = m_vCharMetrics[Index + 1].m_PrefixWidth - Prefix;
	else
		CharWidth = std::max(0.0f, m_BaseLineWidth - Prefix);

	return Prefix + CharWidth * std::clamp(Frac, 0.0f, 1.0f);
}

float CMusicPlayerLyrics::ComputeTextStartX(float AreaLeft, float AreaWidth, float CenterX, float PlayheadX) const
{
	const float IdealStartX = CenterX - PlayheadX;

	if(m_BaseLineWidth <= AreaWidth)
	{
		const float MinStartX = AreaLeft + AreaWidth - m_BaseLineWidth;
		const float MaxStartX = AreaLeft;
		if(MinStartX >= MaxStartX)
			return AreaLeft + (AreaWidth - m_BaseLineWidth) * 0.5f;
		return std::clamp(IdealStartX, MinStartX, MaxStartX);
	}

	const float MaxStartX = CenterX;
	const float MinStartX = AreaLeft + AreaWidth - m_BaseLineWidth;
	return std::clamp(IdealStartX, MinStartX, MaxStartX);
}

void CMusicPlayerLyrics::Render(ITextRender *pTextRender, CUi *pUi, const CUIRect &Area, float FontSize, float Delta)
{
	if(pTextRender == nullptr || pUi == nullptr || Area.w <= 0.0f || Area.h <= 0.0f)
		return;
	if(m_vColorSplits.capacity() < 3)
		m_vColorSplits.reserve(3);

	const int64_t PositionMs = CurrentPositionMs();
	int LineIndex = ResolveDisplayLineIndex();
	int64_t CountdownRemainingMs = 0;
	if(IsCountdownIndex(LineIndex) && !m_vLines.empty())
		CountdownRemainingMs = m_vLines.front().m_StartMs - PositionMs;

	if(LineIndex != m_CurrentLineIndex)
	{
		const bool SequentialForward =
			m_CurrentLineIndex != LINE_NONE &&
			LineIndex == m_CurrentLineIndex + 1;
		const bool MessageToResult =
			(m_CurrentLineIndex == FALLBACK_NOT_FOUND || m_CurrentLineIndex == FALLBACK_NO_CONNECTION) &&
			(LineIndex == FALLBACK_TITLE || LineIndex == FALLBACK_BRAND);
		if(SequentialForward || MessageToResult)
		{
			m_OutgoingLineIndex = m_CurrentLineIndex;
			m_LineTransitionT = 0.0f;
		}
		else
		{
			m_OutgoingLineIndex = LINE_NONE;
			m_LineTransitionT = 1.0f;
		}
		m_CurrentLineIndex = LineIndex;
		m_LayoutValid = false;
	}

	if(m_LineTransitionT < 1.0f)
	{
		m_LineTransitionT = std::clamp(m_LineTransitionT + Delta * 1000.0f / (g_Config.m_ElyIslandEnabled ? std::max(1, g_Config.m_ElyLyricsTransitionMs) : LYRICS_LINE_SLIDE_MS), 0.0f, 1.0f);
		if(m_LineTransitionT >= 1.0f)
			m_OutgoingLineIndex = LINE_NONE;
	}

	if(m_CurrentLineIndex == LINE_NONE)
		return;

	EnsureLayout(pTextRender, FontSize, m_CurrentLineIndex);
	if(!m_LayoutValid || m_vCharMetrics.empty())
		return;

	const bool FallbackMode = IsFallbackIndex(m_CurrentLineIndex);
	const bool TitleMarquee = m_CurrentLineIndex == FALLBACK_TITLE && m_LineTransitionT >= 1.0f && m_BaseLineWidth > Area.w + 0.5f;
	if(TitleMarquee)
	{
		const float Gap = FontSize * LYRICS_TITLE_MARQUEE_GAP_FACTOR;
		const float LoopW = m_BaseLineWidth + Gap;
		const float Speed = std::max(22.0f, FontSize * 2.8f);
		m_TitleMarqueeOffset += Delta * Speed;
		if(m_TitleMarqueeOffset >= LoopW)
			m_TitleMarqueeOffset = fmodf(m_TitleMarqueeOffset, LoopW);
	}
	else if(m_CurrentLineIndex != FALLBACK_TITLE)
	{
		m_TitleMarqueeOffset = 0.0f;
	}

	const float Progress = IsCountdownIndex(m_CurrentLineIndex) ?
				       CountdownProgress(m_CurrentLineIndex, CountdownRemainingMs) :
				       (FallbackMode ? 0.0f : LineProgress(m_CurrentLineIndex, PositionMs));
	const float ProgressChars = Progress * (float)m_vCharMetrics.size();
	const float CenterX = Area.x + Area.w * 0.5f;
	float TextStartX;
	if(TitleMarquee)
	{
		TextStartX = Area.x - m_TitleMarqueeOffset;
	}
	else if(IsCountdownIndex(m_CurrentLineIndex) || FallbackMode)
	{
		TextStartX = CenterX - m_BaseLineWidth * 0.5f;
	}
	else
	{
		const float PlayheadX = PlayheadXInLine(ProgressChars);
		TextStartX = ComputeTextStartX(Area.x, Area.w, CenterX, PlayheadX);
	}

	const float SlideT = g_Config.m_ElyIslandEnabled && (g_Config.m_ElyIslandReducedMotion || !g_Config.m_ElyLyricsAnimation || !g_Config.m_BcAnimations) ? 1.0f : BCUiAnimations::EaseOutCubic(m_LineTransitionT);
	const float BaseY = Area.y + (Area.h - FontSize) * 0.5f;
	const float IncomingY = BaseY + (1.0f - SlideT) * Area.h;
	const float OutgoingY = BaseY - SlideT * Area.h;

	auto DrawDisplay = [&](int DrawIndex, float X, float Y, float ProgressCharsForColor, float Alpha, int ColorMode) {
		if(DrawIndex == LINE_NONE || Alpha <= 0.001f)
			return;
		if(!IsCountdownIndex(DrawIndex) && !IsFallbackIndex(DrawIndex) && (DrawIndex < 0 || DrawIndex >= (int)m_vLines.size()))
			return;

		if(g_Config.m_ElyIslandEnabled) Alpha *= g_Config.m_ElyLyricsOpacity / 100.0f;
		const char *pText = nullptr;
		char aDigit[4];
		float TextW = 0.0f;
		if(IsCountdownIndex(DrawIndex))
		{
			if(DrawIndex == m_CurrentLineIndex)
			{
				pText = m_LayoutText.c_str();
				TextW = m_BaseLineWidth;
			}
			else
			{
				str_format(aDigit, sizeof(aDigit), "%d", CountdownDigit(DrawIndex));
				pText = aDigit;
				TextW = pTextRender->TextWidth(FontSize, aDigit, -1, -1.0f);
			}
		}
		else if(IsFallbackIndex(DrawIndex))
		{
			pText = (DrawIndex == m_CurrentLineIndex) ? m_LayoutText.c_str() : FallbackText(DrawIndex);
			TextW = (DrawIndex == m_CurrentLineIndex) ? m_BaseLineWidth : pTextRender->TextWidth(FontSize, pText, -1, -1.0f);
		}
		else
		{
			pText = m_vLines[DrawIndex].m_Text.c_str();
			TextW = m_BaseLineWidth;
		}

		CUIRect Clip = Area;
		pUi->ClipEnable(&Clip);

		m_vColorSplits.clear();
		if(ColorMode == 1)
			m_vColorSplits.emplace_back(0, -1, LYRICS_UPCOMING_COLOR.WithAlpha(Alpha));
		else if(ColorMode == 2)
			m_vColorSplits.emplace_back(0, -1, LYRICS_PASSED_COLOR.WithAlpha(Alpha));
		else if(IsCountdownIndex(DrawIndex) && DrawIndex == m_CurrentLineIndex)
			BuildColorSplits(ProgressCharsForColor, Alpha, m_vColorSplits);
		else if(IsCountdownIndex(DrawIndex) || IsFallbackIndex(DrawIndex))
			m_vColorSplits.emplace_back(0, -1, LYRICS_PASSED_COLOR.WithAlpha(Alpha));
		else
			BuildColorSplits(ProgressCharsForColor, Alpha, m_vColorSplits);

		if(g_Config.m_ElyIslandEnabled)
		{
			// Ordinary LRC carries line timestamps only; do not invent word timing.
			const ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ElyLyricsColor));
			m_vColorSplits.clear();
			m_vColorSplits.emplace_back(0, -1, Color.WithAlpha(Alpha * (g_Config.m_ElyLyricsActiveHighlight && DrawIndex != m_CurrentLineIndex ? .55f : 1.0f)));
		}
		auto DrawOnce = [&](float DrawX) {
			CTextCursor Cursor;
			Cursor.m_FontSize = FontSize;
			Cursor.m_Flags = TEXTFLAG_RENDER;
			Cursor.SetPosition(vec2(DrawX, Y));
			Cursor.m_vColorSplits = m_vColorSplits;
			pTextRender->TextColor(LYRICS_UPCOMING_COLOR.WithAlpha(Alpha));
			pTextRender->TextEx(&Cursor, pText, -1);
		};

		const bool MarqueeCopy = DrawIndex == m_CurrentLineIndex && TitleMarquee;
		DrawOnce(X);
		if(MarqueeCopy)
			DrawOnce(X + TextW + FontSize * LYRICS_TITLE_MARQUEE_GAP_FACTOR);

		pUi->ClipDisable();
	};

	if(m_OutgoingLineIndex != LINE_NONE && m_LineTransitionT < 1.0f)
	{
		float OutWidth;
		if(IsCountdownIndex(m_OutgoingLineIndex))
		{
			char aDigit[4];
			str_format(aDigit, sizeof(aDigit), "%d", CountdownDigit(m_OutgoingLineIndex));
			OutWidth = CachedTextWidth(pTextRender, aDigit, FontSize);
		}
		else if(IsFallbackIndex(m_OutgoingLineIndex))
		{
			OutWidth = CachedTextWidth(pTextRender, FallbackText(m_OutgoingLineIndex), FontSize);
		}
		else
		{
			OutWidth = CachedTextWidth(pTextRender, m_vLines[m_OutgoingLineIndex].m_Text.c_str(), FontSize);
		}
		const float OutX = CenterX - OutWidth * 0.5f;
		const int OutColorMode = IsMutedFallback(m_OutgoingLineIndex) ? 1 : 2;
		DrawDisplay(m_OutgoingLineIndex, OutX, OutgoingY, 0.0f, 1.0f - SlideT, OutColorMode);
	}

	const float ActiveAlpha = m_OutgoingLineIndex != LINE_NONE ? SlideT : 1.0f;
	int ActiveColorMode = 0;
	if(IsMutedFallback(m_CurrentLineIndex))
		ActiveColorMode = 1;
	else if(m_CurrentLineIndex == FALLBACK_TITLE || m_CurrentLineIndex == FALLBACK_BRAND)
		ActiveColorMode = 2;
	DrawDisplay(m_CurrentLineIndex, TextStartX, IncomingY, ProgressChars, ActiveAlpha, ActiveColorMode);
}

void CMusicPlayerLyrics::UpdateLocal(const char *pLrc, const char *pKey, const char *pTitle, int64_t PositionMs, int64_t DurationMs, bool Playing)
{
	const std::string Key = std::string("local|") + pKey;
	const bool Changed = Key != m_ActiveKey;
	if(Changed)
	{
		AbortRequest();
		ClearLayoutState();
		m_ActiveKey = Key;
		m_TrackTitle = pTitle;
		m_vLines.clear();
		const bool Valid = ParseSyncedLyrics(pLrc, m_vLines);
		m_DisplayState = Valid ? EDisplayState::Ready : EDisplayState::NotFound;
	}
	SyncMediaClock(PositionMs, DurationMs, Playing, Changed);
}


void CMusicPlayerLyrics::RenderElyFlow(ITextRender *pTextRender, CUi *pUi, const CUIRect &Area, float FontSize, float Delta, float Alpha)
{
	if(!pTextRender || !pUi || !HasSyncedLines() || Area.w <= 0 || Area.h <= 0 || Alpha <= 0.001f)
		return;
	const int64_t PositionMs = CurrentPositionMs();
	const int Current = FindLineIndex(PositionMs);
	if(Current < 0 || Current >= (int)m_vLines.size())
		return;

	if(Current != m_ElyCurrentLine)
	{
		m_ElyLineTransition = m_ElyCurrentLine >= 0 ? 0.0f : 1.0f;
		m_ElyCurrentLine = Current;
		m_ElyCurrentWord = m_ElyPreviousWord = -1;
		m_ElyWordTransition = 1.0f;
	}
	const auto &Line = m_vLines[Current];
	if(Line.m_vWords.empty())
		return;

	// The layout always reserves full-size advances, so a highlighted word cannot move its neighbours.
	if(m_ElyLayoutLine != Current || m_ElyLayoutWidth != Area.w || m_ElyLayoutHeight != Area.h || m_ElyRequestedFont != FontSize)
	{
		m_ElyLayoutLine = Current;
		m_ElyLayoutWidth = Area.w;
		m_ElyLayoutHeight = Area.h;
		m_ElyRequestedFont = FontSize;
		m_vElyFlowWords.clear();
		std::vector<float> vWidths, vGaps;
		float TotalWidth = 0.0f;
		for(size_t i = 0; i < Line.m_vWords.size(); ++i)
		{
			const auto &Word = Line.m_vWords[i];
			const std::string Text = Line.m_Text.substr(Word.m_ByteOffset, Word.m_ByteLength);
			const float Width = CachedTextWidth(pTextRender, Text.c_str(), FontSize);
			float Gap = 0.0f;
			if(i > 0)
			{
				const auto &Previous = Line.m_vWords[i - 1];
				const int From = Previous.m_ByteOffset + Previous.m_ByteLength;
				const std::string Whitespace = Line.m_Text.substr(From, Word.m_ByteOffset - From);
				Gap = CachedTextWidth(pTextRender, Whitespace.c_str(), FontSize);
			}
			vWidths.push_back(Width);
			vGaps.push_back(Gap);
			m_vElyFlowWords.push_back({Text, 0, 0, Width});
			TotalWidth += Gap + Width;
		}

		size_t Break = Line.m_vWords.size();
		float WidestRow = TotalWidth;
		if(TotalWidth > Area.w && Line.m_vWords.size() > 1)
		{
			float LeftWidth = 0.0f;
			for(size_t i = 1; i < Line.m_vWords.size(); ++i)
			{
				LeftWidth += vGaps[i - 1] + vWidths[i - 1];
				const float RightWidth = TotalWidth - LeftWidth - vGaps[i];
				const float Candidate = std::max(LeftWidth, RightWidth);
				if(Candidate < WidestRow)
				{
					WidestRow = Candidate;
					Break = i;
				}
			}
		}
		const int Rows = Break < Line.m_vWords.size() ? 2 : 1;
		const float Scale = std::min({1.0f, Area.w * 0.98f / std::max(1.0f, WidestRow), Area.h / std::max(1.0f, FontSize * Rows * 1.28f)});
		m_ElyFlowFontSize = FontSize * Scale;
		for(int Row = 0; Row < Rows; ++Row)
		{
			const size_t First = Row == 0 ? 0 : Break;
			const size_t Last = Row == 0 ? Break : Line.m_vWords.size();
			float RowWidth = 0.0f;
			for(size_t i = First; i < Last; ++i)
				RowWidth += vWidths[i] + (i > First ? vGaps[i] : 0.0f);
			float X = (Area.w - RowWidth * Scale) * 0.5f;
			const float Y = (Area.h - Rows * m_ElyFlowFontSize * 1.28f) * 0.5f + Row * m_ElyFlowFontSize * 1.28f;
			for(size_t i = First; i < Last; ++i)
			{
				if(i > First)
					X += vGaps[i] * Scale;
				auto &Word = m_vElyFlowWords[i];
				Word.m_X = X;
				Word.m_Y = Y;
				Word.m_Advance = vWidths[i] * Scale;
				X += Word.m_Advance;
			}
		}
	}

	int64_t WordPosition = PositionMs;
	// Plain LRC supplies line times only. Its optional karaoke follows an estimated word schedule.
	if(!Line.m_HasWordTiming && Current + 1 == (int)m_vLines.size() && m_ClockDurationMs > Line.m_StartMs)
	{
		const double Progress = std::clamp((PositionMs - Line.m_StartMs) / (double)(m_ClockDurationMs - Line.m_StartMs), 0.0, 1.0);
		WordPosition = Line.m_StartMs + (int64_t)((Line.m_vWords.back().m_EndMs - Line.m_StartMs) * Progress);
	}
	const bool Karaoke = g_Config.m_ElyLyricsActiveHighlight && (Line.m_HasWordTiming || g_Config.m_ElyLyricsAnimation);
	int ActiveWord = -1;
	if(Karaoke)
	{
		for(size_t i = 0; i < Line.m_vWords.size(); ++i)
		{
			if(WordPosition >= Line.m_vWords[i].m_StartMs && WordPosition < Line.m_vWords[i].m_EndMs)
			{
				ActiveWord = i;
				break;
			}
		}
	}
	if(ActiveWord != m_ElyCurrentWord)
	{
		m_ElyPreviousWord = m_ElyCurrentWord;
		m_ElyCurrentWord = ActiveWord;
		m_ElyWordTransition = 0.0f;
	}
	const bool Motion = g_Config.m_BcAnimations && !g_Config.m_ElyIslandReducedMotion && g_Config.m_ElyLyricsAnimation;
	const float Dt = std::clamp(Delta, 0.0f, 0.1f);
	m_ElyLineTransition = Motion ? std::min(1.0f, m_ElyLineTransition + Dt * 1000.0f / std::max(1, g_Config.m_ElyLyricsTransitionMs)) : 1.0f;
	m_ElyWordTransition = Motion ? std::min(1.0f, m_ElyWordTransition + Dt / 0.12f) : 1.0f;
	const float LineT = BCUiAnimations::EaseOutCubic(m_ElyLineTransition);
	const float WordT = BCUiAnimations::EaseOutCubic(m_ElyWordTransition);
	const ColorRGBA OldColor = pTextRender->GetTextColor();
	const ColorRGBA OldOutline = pTextRender->GetTextOutlineColor();
	pTextRender->TextOutlineColor(0, 0, 0, 0);
	pUi->ClipEnable(&Area);
	for(size_t i = 0; i < m_vElyFlowWords.size(); ++i)
	{
		const auto &Word = m_vElyFlowWords[i];
		const float Highlight = (int)i == m_ElyCurrentWord ? WordT : ((int)i == m_ElyPreviousWord ? 1.0f - WordT : 0.0f);
		const float SizeScale = 0.90f + 0.10f * Highlight;
		const float Size = m_ElyFlowFontSize * SizeScale;
		const float X = Area.x + Word.m_X + Word.m_Advance * (1.0f - SizeScale) * 0.5f;
		const float Y = Area.y + Word.m_Y + (m_ElyFlowFontSize - Size) * 0.5f + (1.0f - LineT) * m_ElyFlowFontSize * 0.08f;
		CTextCursor Cursor;
		Cursor.SetPosition(vec2(X, Y));
		Cursor.m_FontSize = Size;
		Cursor.m_Flags = TEXTFLAG_RENDER;
		const float Shade = 0.80f + 0.20f * Highlight;
		pTextRender->TextColor(Shade, Shade, Shade, Alpha * (0.85f + 0.15f * LineT));
		pTextRender->TextEx(&Cursor, Word.m_Text.c_str());
	}
	pUi->ClipDisable();
	pTextRender->TextColor(OldColor);
	pTextRender->TextOutlineColor(OldOutline);
}

