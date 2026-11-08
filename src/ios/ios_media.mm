#include "ios_media.h"

#include <base/time.h>
#include <game/client/components/bestclient/media_decoder.h>

#import <AVFoundation/AVFoundation.h>
#import <CoreImage/CoreImage.h>
#import <ImageIO/ImageIO.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace
{
struct SVideo
{
	AVURLAsset *m_Asset;
	AVAssetTrack *m_Track;
	AVAssetReader *m_Reader;
	AVAssetReaderTrackOutput *m_Output;
	CIContext *m_Context;
	int m_Width = 0;
	int m_Height = 0;
	int m_FrameMs = 33;

	bool Restart()
	{
		[m_Reader cancelReading];
		m_Reader = [AVAssetReader assetReaderWithAsset:m_Asset error:nullptr];
		m_Output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:m_Track
			outputSettings:@{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)}];
		m_Output.alwaysCopiesSampleData = NO;
		if(!m_Reader || ![m_Reader canAddOutput:m_Output])
			return false;
		[m_Reader addOutput:m_Output];
		return [m_Reader startReading];
	}

	~SVideo() { [m_Reader cancelReading]; }
};

bool CopyImage(CGImageRef Image, CImageInfo &Out, int MaxDimension)
{
	const size_t SourceWidth = CGImageGetWidth(Image);
	const size_t SourceHeight = CGImageGetHeight(Image);
	if(SourceWidth == 0 || SourceHeight == 0)
		return false;
	const int Limit = MaxDimension > 0 ? std::min(MaxDimension, 4096) : 4096;
	const double Scale = std::min(1.0, Limit / (double)std::max(SourceWidth, SourceHeight));
	Out.m_Width = std::max<size_t>(1, std::lround(SourceWidth * Scale));
	Out.m_Height = std::max<size_t>(1, std::lround(SourceHeight * Scale));
	Out.m_Format = CImageInfo::FORMAT_RGBA;
	Out.Allocate();
	CGColorSpaceRef ColorSpace = CGColorSpaceCreateDeviceRGB();
	CGContextRef Context = CGBitmapContextCreate(Out.m_pData, Out.m_Width, Out.m_Height, 8,
		Out.m_Width * 4, ColorSpace, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
	CGColorSpaceRelease(ColorSpace);
	if(!Context)
	{
		Out.Free();
		return false;
	}
	CGContextDrawImage(Context, CGRectMake(0, 0, Out.m_Width, Out.m_Height), Image);
	CGContextRelease(Context);
	// DDNet textures use straight alpha; CoreGraphics produces premultiplied RGBA.
	for(size_t i = 0; i < Out.DataSize(); i += 4)
	{
		const unsigned Alpha = Out.m_pData[i + 3];
		if(Alpha > 0 && Alpha < 255)
			for(size_t c = 0; c < 3; ++c)
				Out.m_pData[i + c] = std::min(255u, (Out.m_pData[i + c] * 255u + Alpha / 2) / Alpha);
	}
	return true;
}

void AppendFrame(SMediaDecodedFrames &Out, SMediaRawFrame &&Frame, const SMediaDecodeLimits &Limits)
{
	Out.m_vFrames.push_back(std::move(Frame));
	while(Out.m_vFrames.size() > 1 &&
		((Limits.m_MaxFrames > 0 && (int)Out.m_vFrames.size() > Limits.m_MaxFrames) ||
			(Limits.m_MaxTotalBytes > 0 && Out.MemoryUsage() > Limits.m_MaxTotalBytes)))
	{
		size_t Write = 0;
		for(size_t i = 0; i < Out.m_vFrames.size(); i += 2)
		{
			if(Write != i)
				Out.m_vFrames[Write] = std::move(Out.m_vFrames[i]);
			if(i + 1 < Out.m_vFrames.size())
				Out.m_vFrames[Write].m_DurationMs = std::min(10000, Out.m_vFrames[Write].m_DurationMs + Out.m_vFrames[i + 1].m_DurationMs);
			++Write;
		}
		Out.m_vFrames.resize(Write);
	}
}

void Finish(SMediaDecodedFrames &Out)
{
	if(!Out.m_vFrames.empty())
	{
		Out.m_Width = Out.m_vFrames[0].m_Image.m_Width;
		Out.m_Height = Out.m_vFrames[0].m_Image.m_Height;
		Out.m_Animated = Out.m_vFrames.size() > 1;
		Out.m_AnimationStart = time_get();
	}
}
}

void *IosVideoOpen(const char *pPath, int &Width, int &Height, int MaxDimension)
{
	@autoreleasepool
	{
		if(!pPath || !pPath[0])
			return nullptr;
		auto Video = std::make_unique<SVideo>();
		Video->m_Asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:pPath]] options:nil];
		Video->m_Track = [Video->m_Asset tracksWithMediaType:AVMediaTypeVideo].firstObject;
		if(!Video->m_Track)
			return nullptr;
		const CGRect Bounds = CGRectApplyAffineTransform(CGRectMake(0, 0, Video->m_Track.naturalSize.width,
			Video->m_Track.naturalSize.height), Video->m_Track.preferredTransform);
		const double Longest = std::max(std::abs(Bounds.size.width), std::abs(Bounds.size.height));
		if(!std::isfinite(Longest) || Longest < 1)
			return nullptr;
		const int Limit = MaxDimension > 0 ? std::min(MaxDimension, 4096) : 2048;
		const double Scale = std::min(1.0, Limit / Longest);
		Video->m_Width = std::max(1L, std::lround(std::abs(Bounds.size.width) * Scale));
		Video->m_Height = std::max(1L, std::lround(std::abs(Bounds.size.height) * Scale));
		const float Fps = Video->m_Track.nominalFrameRate;
		Video->m_FrameMs = std::isfinite(Fps) && Fps > 0 ? std::clamp((int)std::lround(1000.0 / Fps), 8, 250) : 33;
		Video->m_Context = [CIContext contextWithOptions:@{kCIContextCacheIntermediates: @NO}];
		if(!Video->Restart())
			return nullptr;
		Width = Video->m_Width;
		Height = Video->m_Height;
		return Video.release();
	}
}

bool IosVideoRead(void *pState, std::vector<uint8_t> &Pixels, int &DurationMs, bool Loop)
{
	@autoreleasepool
	{
		auto *pVideo = static_cast<SVideo *>(pState);
		if(!pVideo)
			return false;
		CMSampleBufferRef Sample = [pVideo->m_Output copyNextSampleBuffer];
		if(!Sample && Loop && pVideo->Restart())
			Sample = [pVideo->m_Output copyNextSampleBuffer];
		if(!Sample)
			return false;
		CVPixelBufferRef Buffer = CMSampleBufferGetImageBuffer(Sample);
		if(!Buffer)
		{
			CFRelease(Sample);
			return false;
		}
		CIImage *Image = [[CIImage imageWithCVPixelBuffer:Buffer] imageByApplyingTransform:pVideo->m_Track.preferredTransform];
		const CGRect Extent = Image.extent;
		Image = [Image imageByApplyingTransform:CGAffineTransformMakeTranslation(-Extent.origin.x, -Extent.origin.y)];
		Image = [Image imageByApplyingTransform:CGAffineTransformMakeScale(pVideo->m_Width / Extent.size.width,
			pVideo->m_Height / Extent.size.height)];
		Pixels.resize((size_t)pVideo->m_Width * pVideo->m_Height * 4);
		CGColorSpaceRef Space = CGColorSpaceCreateDeviceRGB();
		[pVideo->m_Context render:Image toBitmap:Pixels.data() rowBytes:pVideo->m_Width * 4
			bounds:CGRectMake(0, 0, pVideo->m_Width, pVideo->m_Height) format:kCIFormatRGBA8 colorSpace:Space];
		CGColorSpaceRelease(Space);
		const double Seconds = CMTimeGetSeconds(CMSampleBufferGetDuration(Sample));
		DurationMs = std::isfinite(Seconds) && Seconds > 0 ? std::clamp((int)std::lround(Seconds * 1000.0), 8, 250) : pVideo->m_FrameMs;
		CFRelease(Sample);
		return true;
	}
}

void IosVideoClose(void *pState)
{
	delete static_cast<SVideo *>(pState);
}

bool IosDecodeMedia(const unsigned char *pData, size_t Size, SMediaDecodedFrames &Out, const SMediaDecodeLimits &Limits)
{
	Out.Free();
	if(!pData || Size == 0)
		return false;
	@autoreleasepool
	{
		NSData *Data = [NSData dataWithBytesNoCopy:const_cast<unsigned char *>(pData) length:Size freeWhenDone:NO];
		CGImageSourceRef Source = CGImageSourceCreateWithData((__bridge CFDataRef)Data, nullptr);
		if(Source)
		{
			const size_t Count = Limits.m_DecodeAllFrames ? CGImageSourceGetCount(Source) : std::min<size_t>(1, CGImageSourceGetCount(Source));
			int TimelineMs = 0;
			for(size_t i = 0; i < Count; ++i)
			{
				NSDictionary *Options = @{(id)kCGImageSourceCreateThumbnailFromImageAlways: @YES,
					(id)kCGImageSourceCreateThumbnailWithTransform: @YES,
					(id)kCGImageSourceThumbnailMaxPixelSize: @(Limits.m_MaxDimension > 0 ? std::min(Limits.m_MaxDimension, 4096) : 4096)};
				CGImageRef Image = CGImageSourceCreateThumbnailAtIndex(Source, i, (__bridge CFDictionaryRef)Options);
				if(!Image)
					break;
				SMediaRawFrame Frame;
				const bool Copied = CopyImage(Image, Frame.m_Image, Limits.m_MaxDimension);
				CGImageRelease(Image);
				if(!Copied)
					break;
				NSDictionary *Properties = CFBridgingRelease(CGImageSourceCopyPropertiesAtIndex(Source, i, nullptr));
				NSDictionary *Gif = Properties[(id)kCGImagePropertyGIFDictionary];
				NSNumber *Delay = Gif[(id)kCGImagePropertyGIFUnclampedDelayTime] ?: Gif[(id)kCGImagePropertyGIFDelayTime];
				Frame.m_DurationMs = Delay ? std::clamp((int)std::lround(Delay.doubleValue * 1000), 8, 10000) : 100;
				TimelineMs += Frame.m_DurationMs;
				AppendFrame(Out, std::move(Frame), Limits);
				if(Limits.m_MaxAnimationDurationMs > 0 && TimelineMs >= Limits.m_MaxAnimationDurationMs)
					break;
			}
			CFRelease(Source);
			Finish(Out);
			return !Out.Empty();
		}
		// AVFoundation identifies supported video containers from their contents.
		NSString *Path = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingPathExtension:@"mp4"]];
		if(![Data writeToFile:Path atomically:YES])
			return false;
		int Width, Height;
		void *Video = IosVideoOpen(Path.UTF8String, Width, Height, Limits.m_MaxDimension);
		if(Video)
		{
			int TimelineMs = 0;
			while(true)
			{
				std::vector<uint8_t> Pixels;
				int DurationMs;
				if(!IosVideoRead(Video, Pixels, DurationMs, false))
					break;
				SMediaRawFrame Frame;
				Frame.m_DurationMs = DurationMs;
				Frame.m_Image.m_Width = Width;
				Frame.m_Image.m_Height = Height;
				Frame.m_Image.m_Format = CImageInfo::FORMAT_RGBA;
				Frame.m_Image.Allocate();
				std::copy(Pixels.begin(), Pixels.end(), Frame.m_Image.m_pData);
				TimelineMs += DurationMs;
				AppendFrame(Out, std::move(Frame), Limits);
				if(!Limits.m_DecodeAllFrames || (Limits.m_MaxAnimationDurationMs > 0 && TimelineMs >= Limits.m_MaxAnimationDurationMs))
					break;
			}
			IosVideoClose(Video);
		}
		[[NSFileManager defaultManager] removeItemAtPath:Path error:nullptr];
		Finish(Out);
		return !Out.Empty();
	}
}
