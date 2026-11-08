#ifndef IOS_IOS_MUSIC_H
#define IOS_IOS_MUSIC_H

#include <cstdint>
#include <string>
#include <vector>

struct SIosMusicSnapshot
{
	bool m_Valid = false;
	bool m_Playing = false;
	std::string m_Title;
	std::string m_Artist;
	std::string m_Album;
	std::string m_ArtKey;
	int64_t m_DurationMs = 0;
	int64_t m_PositionMs = 0;
	std::vector<unsigned char> m_vCover;
};

// All MediaPlayer calls are queued on the main thread. Poll never waits for it,
// so the HUD provider can shut down without deadlocking the application thread.
SIosMusicSnapshot IosMusicPoll();
void IosMusicAuthorize();
void IosMusicCommand(int Command); // 0 previous, 1 play/pause, 2 next

#endif
