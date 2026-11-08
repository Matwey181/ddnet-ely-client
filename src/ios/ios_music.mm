#include "ios_music.h"

#import <MediaPlayer/MediaPlayer.h>
#import <UIKit/UIKit.h>

#include <atomic>
#include <cmath>
#include <mutex>

namespace
{
std::mutex gs_MusicMutex;
SIosMusicSnapshot gs_Snapshot;
std::atomic<bool> gs_PollPending{false};

std::string Utf8(NSString *pString)
{
	const char *pUtf8 = pString.UTF8String;
	return pUtf8 ? pUtf8 : "";
}

int64_t Milliseconds(double Seconds)
{
	return std::isfinite(Seconds) && Seconds > 0.0 ? static_cast<int64_t>(Seconds * 1000.0) : 0;
}
}

SIosMusicSnapshot IosMusicPoll()
{
	if(!gs_PollPending.exchange(true))
	{
		dispatch_async(dispatch_get_main_queue(), ^{
			@autoreleasepool
			{
				SIosMusicSnapshot Snapshot;
				if([MPMediaLibrary authorizationStatus] == MPMediaLibraryAuthorizationStatusAuthorized)
				{
					MPMusicPlayerController *pPlayer = [MPMusicPlayerController systemMusicPlayer];
					MPMediaItem *pItem = pPlayer.nowPlayingItem;
					if(pItem != nil)
					{
						Snapshot.m_Valid = true;
						Snapshot.m_Playing = pPlayer.playbackState == MPMusicPlaybackStatePlaying;
						Snapshot.m_Title = Utf8(pItem.title);
						Snapshot.m_Artist = Utf8(pItem.artist);
						Snapshot.m_Album = Utf8(pItem.albumTitle);
						Snapshot.m_DurationMs = Milliseconds(pItem.playbackDuration);
						Snapshot.m_PositionMs = Milliseconds(pPlayer.currentPlaybackTime);
						Snapshot.m_ArtKey = std::to_string(pItem.persistentID) + Snapshot.m_Title + Snapshot.m_Artist;
						bool NeedCover;
						{
							std::lock_guard<std::mutex> Lock(gs_MusicMutex);
							NeedCover = gs_Snapshot.m_ArtKey != Snapshot.m_ArtKey;
							if(!NeedCover)
								Snapshot.m_vCover = gs_Snapshot.m_vCover;
						}
						if(NeedCover)
						{
							UIImage *pCover = [pItem.artwork imageWithSize:CGSizeMake(256, 256)];
							NSData *pData = pCover ? UIImagePNGRepresentation(pCover) : nil;
							if(pData.length > 0)
							{
								const auto *pBytes = static_cast<const unsigned char *>(pData.bytes);
								Snapshot.m_vCover.assign(pBytes, pBytes + pData.length);
							}
						}
					}
				}
				{
					std::lock_guard<std::mutex> Lock(gs_MusicMutex);
					gs_Snapshot = std::move(Snapshot);
				}
				gs_PollPending.store(false);
			}
		});
	}
	std::lock_guard<std::mutex> Lock(gs_MusicMutex);
	return gs_Snapshot;
}

void IosMusicAuthorize()
{
	dispatch_async(dispatch_get_main_queue(), ^{
		[MPMediaLibrary requestAuthorization:^(MPMediaLibraryAuthorizationStatus Status) {
			(void)Status;
		}];
	});
}

void IosMusicCommand(int Command)
{
	dispatch_async(dispatch_get_main_queue(), ^{
		if([MPMediaLibrary authorizationStatus] != MPMediaLibraryAuthorizationStatusAuthorized)
			return;
		MPMusicPlayerController *pPlayer = [MPMusicPlayerController systemMusicPlayer];
		if(Command == 0)
			[pPlayer skipToPreviousItem];
		else if(Command == 2)
			[pPlayer skipToNextItem];
		else if(pPlayer.playbackState == MPMusicPlaybackStatePlaying)
			[pPlayer pause];
		else
			[pPlayer play];
	});
}
