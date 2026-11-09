#include "ios_performance.h"
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#include <mach/mach.h>
#include <os/proc.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <string>

namespace
{
using Clock = std::chrono::steady_clock;
struct SSample
{
	Clock::time_point m_Start = Clock::now();
	Clock::time_point m_Last = m_Start;
	std::array<int64_t, 8> m_Phases{};
	std::array<int64_t, 2> m_Worst{};
	std::array<std::string, 2> m_Names;
	int m_Frames = 0;
	double m_FrameSeconds = 0;
};
SSample gs_Sample;
NSString *LogPath()
{
	return [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"ely-performance.log"];
}
}
void IosPerfComponent(const char *pName, bool Render, int64_t Nanoseconds)
{
	const int Index = Render ? 1 : 0;
	if(Nanoseconds > gs_Sample.m_Worst[Index])
	{
		gs_Sample.m_Worst[Index] = Nanoseconds;
		gs_Sample.m_Names[Index] = pName ? pName : "Unknown";
	}
}
void IosPerfPhase(int Phase, int64_t Nanoseconds)
{
	gs_Sample.m_Phases[Phase] += Nanoseconds;
}
void IosPerfFrame(float FrameSeconds)
{
	auto &S = gs_Sample;
	++S.m_Frames;
	S.m_FrameSeconds += FrameSeconds;
	const auto Now = Clock::now();
	const double Elapsed = std::chrono::duration<double>(Now - S.m_Last).count();
	if(Elapsed < 2.0)
		return;
	@autoreleasepool
	{
		task_vm_info_data_t Memory{};
		mach_msg_type_number_t Count = TASK_VM_INFO_COUNT;
		const bool HaveMemory = task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&Memory), &Count) == KERN_SUCCESS;
		const auto Thermal = NSProcessInfo.processInfo.thermalState;
		const double Age = std::chrono::duration<double>(Now - S.m_Start).count();
		static bool Started = false;
		FILE *pFile = fopen(LogPath().fileSystemRepresentation, Started ? "a" : "w");
		if(pFile)
		{
			if(!Started)
				fputs("Ely iOS performance v2. No player names, chat or account data. Heat: 0=nominal 1=fair 2=serious 3=critical. Timings are ms/frame; component timings are worst single calls.\n", pFile);
			Started = true;
			fprintf(pFile, "t=%.1f fps=%.1f update=%.2f render=%.2f swap=%.2f events=%.2f ram_mb=%.1f available_mb=%.1f heat=%ld worst_update=%s:%.2f worst_render=%s:%.2f voice_net=%.2f voice_capture=%.2f voice_playback=%.2f voice_devices=%.2f\n", Age,
				S.m_Frames / Elapsed, S.m_Phases[0] / 1e6 / S.m_Frames, S.m_Phases[1] / 1e6 / S.m_Frames,
				S.m_Phases[2] / 1e6 / S.m_Frames, S.m_Phases[3] / 1e6 / S.m_Frames,
				HaveMemory ? Memory.phys_footprint / 1048576.0 : -1.0, os_proc_available_memory() / 1048576.0,
				static_cast<long>(Thermal), S.m_Names[0].c_str(), S.m_Worst[0] / 1e6, S.m_Names[1].c_str(), S.m_Worst[1] / 1e6, S.m_Phases[4] / 1e6 / S.m_Frames, S.m_Phases[5] / 1e6 / S.m_Frames, S.m_Phases[6] / 1e6 / S.m_Frames, S.m_Phases[7] / 1e6 / S.m_Frames);
			fclose(pFile);
		}
	}
	S.m_Last = Now;
	S.m_Frames = 0;
	S.m_FrameSeconds = 0;
	S.m_Phases.fill(0);
	S.m_Worst.fill(0);
	// Bound the diagnostic file to one 20 minute session; continue sampling into a new file.
	if(std::chrono::duration<double>(Now - S.m_Start).count() > 1200.0)
	{
		S.m_Start = Now;
		@autoreleasepool { [@"" writeToFile:LogPath() atomically:YES encoding:NSUTF8StringEncoding error:nil]; }
	}
}
void IosPerfShare()
{
	dispatch_async(dispatch_get_main_queue(), ^{
		@autoreleasepool
		{
			UIWindow *pWindow = nil;
			for(UIScene *pScene in UIApplication.sharedApplication.connectedScenes)
				if([pScene isKindOfClass:UIWindowScene.class] && pScene.activationState == UISceneActivationStateForegroundActive)
					for(UIWindow *pCandidate in ((UIWindowScene *)pScene).windows)
						if(pCandidate.isKeyWindow) pWindow = pCandidate;
			UIViewController *pPresenter = pWindow.rootViewController;
			while(pPresenter.presentedViewController) pPresenter = pPresenter.presentedViewController;
			if(!pPresenter || ![[NSFileManager defaultManager] fileExistsAtPath:LogPath()]) return;
			UIActivityViewController *pShare = [[UIActivityViewController alloc] initWithActivityItems:@[[NSURL fileURLWithPath:LogPath()]] applicationActivities:nil];
			pShare.popoverPresentationController.sourceView = pPresenter.view;
			pShare.popoverPresentationController.sourceRect = CGRectMake(pPresenter.view.bounds.size.width / 2, pPresenter.view.bounds.size.height / 2, 1, 1);
			[pPresenter presentViewController:pShare animated:YES completion:nil];
		}
	});
}
