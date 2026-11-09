#include "ios_music.h"
#import <MediaPlayer/MediaPlayer.h>
#import <UIKit/UIKit.h>
#include <dlfcn.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>

namespace
{
using Clock = std::chrono::steady_clock;
std::mutex gs_Mutex;
SIosMusicSnapshot gs_Snapshot;
Clock::time_point gs_LastRequest{}, gs_LastReply{};
std::atomic<bool> gs_Pending{false};
std::atomic<bool> gs_TimedOut{false};
std::atomic<unsigned> gs_Generation{0};
std::atomic<int> gs_Mode{-1}; // 0 disabled, 1 system media, 2 Apple Music
using TRead = void (*)(dispatch_queue_t, void (^)(CFDictionaryRef));
using TCommand = Boolean (*)(int, CFDictionaryRef);
void *Remote()
{
	static void *pRemote = dlopen("/System/Library/PrivateFrameworks/MediaRemote.framework/MediaRemote", RTLD_LAZY | RTLD_LOCAL);
	return pRemote;
}
TRead Reader() { static TRead pRead = Remote() ? reinterpret_cast<TRead>(dlsym(Remote(), "MRMediaRemoteGetNowPlayingInfo")) : nullptr; return pRead; }
TCommand Sender() { static TCommand pSend = Remote() ? reinterpret_cast<TCommand>(dlsym(Remote(), "MRMediaRemoteSendCommand")) : nullptr; return pSend; }
id Value(NSDictionary *pInfo, const char *pName)
{
	auto ppKey = Remote() ? reinterpret_cast<CFStringRef *>(dlsym(Remote(), pName)) : nullptr;
	return ppKey && *ppKey ? pInfo[(__bridge NSString *)*ppKey] : nil;
}
std::string Text(id pValue)
{
	if(![pValue isKindOfClass:NSString.class]) return {};
	const char *pUtf8 = [(NSString *)pValue UTF8String];
	return pUtf8 ? pUtf8 : "";
}
int64_t Milliseconds(double Seconds)
{
	return std::isfinite(Seconds) && Seconds > 0.0 ? static_cast<int64_t>(std::min(Seconds, 864000.0) * 1000.0) : 0;
}
void Store(SIosMusicSnapshot Snapshot, unsigned Generation)
{
	std::lock_guard<std::mutex> Lock(gs_Mutex);
	if(Generation != gs_Generation.load()) return;
	gs_Snapshot = std::move(Snapshot);
	gs_LastReply = Clock::now();
	gs_Pending.store(false);
}
UIViewController *Presenter()
{
	for(UIScene *pScene in UIApplication.sharedApplication.connectedScenes)
		if([pScene isKindOfClass:UIWindowScene.class] && pScene.activationState == UISceneActivationStateForegroundActive)
			for(UIWindow *pWindow in ((UIWindowScene *)pScene).windows)
				if(pWindow.isKeyWindow)
				{
					UIViewController *pView = pWindow.rootViewController;
					while(pView.presentedViewController) pView = pView.presentedViewController;
					return pView;
				}
	return nil;
}
void Select(int Mode)
{
	std::lock_guard<std::mutex> Lock(gs_Mutex);
	gs_Generation.fetch_add(1);
	gs_Pending.store(false);
	gs_LastRequest = {};
	gs_Mode.store(Mode);
	gs_TimedOut.store(false);
	[[NSUserDefaults standardUserDefaults] setInteger:Mode forKey:@"ElyMusicSourceV2"];
	gs_Snapshot = {};
}
}

SIosMusicSnapshot IosMusicPoll()
{
	@autoreleasepool
	{
		if(gs_Mode.load() < 0)
		{
			NSUserDefaults *pDefaults = [NSUserDefaults standardUserDefaults];
			gs_Mode.store([pDefaults objectForKey:@"ElyMusicSourceV2"] ? static_cast<int>([pDefaults integerForKey:@"ElyMusicSourceV2"]) : 1);
		}
		const int Mode = gs_Mode.load();
		const auto Now = Clock::now();
		unsigned Generation;
		{
			std::lock_guard<std::mutex> Lock(gs_Mutex);
			if(gs_Pending && Now - gs_LastRequest > std::chrono::seconds(3))
				gs_TimedOut.store(true); // No response: stop requests, never build a callback backlog.
			if(Mode == 0 || gs_TimedOut) return {};
			if(gs_Pending || Now - gs_LastRequest < std::chrono::seconds(1))
				return Now - gs_LastReply < std::chrono::seconds(5) ? gs_Snapshot : SIosMusicSnapshot{};
			Generation = gs_Generation.load();
			gs_LastRequest = Now;
			gs_Pending.store(true);
		}
		if(Mode == 1)
		{
			if(auto pRead = Reader())
			{
				pRead(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^(CFDictionaryRef pData) {
					@autoreleasepool
					{
						SIosMusicSnapshot S;
						NSDictionary *pInfo = (__bridge NSDictionary *)pData;
						if([pInfo isKindOfClass:NSDictionary.class])
						{
							S.m_Title = Text(Value(pInfo, "kMRMediaRemoteNowPlayingInfoTitle"));
							S.m_Artist = Text(Value(pInfo, "kMRMediaRemoteNowPlayingInfoArtist"));
							S.m_Album = Text(Value(pInfo, "kMRMediaRemoteNowPlayingInfoAlbum"));
							S.m_Valid = !S.m_Title.empty();
							S.m_Playing = [Value(pInfo, "kMRMediaRemoteNowPlayingInfoPlaybackRate") doubleValue] > 0;
							S.m_DurationMs = Milliseconds([Value(pInfo, "kMRMediaRemoteNowPlayingInfoDuration") doubleValue]);
							double Position = [Value(pInfo, "kMRMediaRemoteNowPlayingInfoElapsedTime") doubleValue];
							id pStamp = Value(pInfo, "kMRMediaRemoteNowPlayingInfoTimestamp");
							if(S.m_Playing && [pStamp isKindOfClass:NSDate.class]) Position += std::max(0.0, -[(NSDate *)pStamp timeIntervalSinceNow]);
							S.m_PositionMs = Milliseconds(Position);
							if(S.m_DurationMs > 0) S.m_PositionMs = std::min(S.m_PositionMs, S.m_DurationMs);
							S.m_ArtKey = S.m_Title + "\n" + S.m_Artist + "\n" + S.m_Album;
							id pArt = Value(pInfo, "kMRMediaRemoteNowPlayingInfoArtworkData");
							if([pArt isKindOfClass:NSData.class] && [(NSData *)pArt length] <= 2 * 1024 * 1024)
							{
								const auto *pBytes = static_cast<const unsigned char *>([(NSData *)pArt bytes]);
								if(pBytes) S.m_vCover.assign(pBytes, pBytes + [(NSData *)pArt length]);
							}
						}
						Store(std::move(S), Generation);
					}
				});
			}
			else { gs_TimedOut.store(true); gs_Pending.store(false); }
		}
		else
		{
			dispatch_async(dispatch_get_main_queue(), ^{
				@autoreleasepool
				{
					SIosMusicSnapshot S;
					if([MPMediaLibrary authorizationStatus] == MPMediaLibraryAuthorizationStatusAuthorized)
					{
						MPMusicPlayerController *pPlayer = MPMusicPlayerController.systemMusicPlayer;
						MPMediaItem *pItem = pPlayer.nowPlayingItem;
						if(pItem)
						{
							S.m_Valid = true;
							S.m_Playing = pPlayer.playbackState == MPMusicPlaybackStatePlaying;
							S.m_Title = Text(pItem.title); S.m_Artist = Text(pItem.artist); S.m_Album = Text(pItem.albumTitle);
							S.m_DurationMs = Milliseconds(pItem.playbackDuration); S.m_PositionMs = Milliseconds(pPlayer.currentPlaybackTime);
							S.m_ArtKey = std::to_string(pItem.persistentID);
						}
					}
					Store(std::move(S), Generation);
				}
			});
		}
		std::lock_guard<std::mutex> Lock(gs_Mutex);
		return gs_Snapshot;
	}
}
void IosMusicAuthorize()
{
	dispatch_async(dispatch_get_main_queue(), ^{
		UIAlertController *pAlert = [UIAlertController alertControllerWithTitle:@"Music source" message:@"System media tries the currently playing app, including Spotify. It uses an undocumented iOS interface and may be unavailable on your iOS version or signing profile. Apple Music is optional. No Apple Music subscription is needed for system media." preferredStyle:UIAlertControllerStyleActionSheet];
		[pAlert addAction:[UIAlertAction actionWithTitle:@"System media (experimental)" style:UIAlertActionStyleDefault handler:^(UIAlertAction *) { Select(1); }]];
		[pAlert addAction:[UIAlertAction actionWithTitle:@"Apple Music" style:UIAlertActionStyleDefault handler:^(UIAlertAction *) {
			Select(2);
			[MPMediaLibrary requestAuthorization:^(MPMediaLibraryAuthorizationStatus) {}];
		}]];
		[pAlert addAction:[UIAlertAction actionWithTitle:@"Disabled" style:UIAlertActionStyleDefault handler:^(UIAlertAction *) { Select(0); }]];
		[pAlert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
		UIViewController *pView = Presenter();
		pAlert.popoverPresentationController.sourceView = pView.view;
		pAlert.popoverPresentationController.sourceRect = CGRectMake(pView.view.bounds.size.width / 2, pView.view.bounds.size.height / 2, 1, 1);
		[pView presentViewController:pAlert animated:YES completion:nil];
	});
}
void IosMusicCommand(int Command)
{
	if(gs_Mode == 1)
	{
		if(auto pSend = Sender()) pSend(Command == 0 ? 5 : Command == 2 ? 4 : 2, nullptr);
		return;
	}
	if(gs_Mode != 2) return;
	dispatch_async(dispatch_get_main_queue(), ^{
		@autoreleasepool
		{
			if([MPMediaLibrary authorizationStatus] != MPMediaLibraryAuthorizationStatusAuthorized) return;
			MPMusicPlayerController *pPlayer = MPMusicPlayerController.systemMusicPlayer;
			if(Command == 0) [pPlayer skipToPreviousItem];
			else if(Command == 2) [pPlayer skipToNextItem];
			else if(pPlayer.playbackState == MPMusicPlaybackStatePlaying) [pPlayer pause];
			else [pPlayer play];
		}
	});
}
