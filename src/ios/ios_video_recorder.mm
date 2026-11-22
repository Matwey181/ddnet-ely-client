#include <engine/client/ios_video_recorder.h>
#include <base/io.h>
#include <base/log.h>
#include <base/str.h>
#include <base/time.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/sound.h>
#include <engine/storage.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <Accelerate/Accelerate.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

struct CVideo::SImpl
{
    AVAssetWriter *m_pWriter = nil;
    AVAssetWriterInput *m_pInput = nil;
    AVAssetWriterInputPixelBufferAdaptor *m_pAdaptor = nil;
    NSString *m_pPath = nil;
};

CVideo::CVideo(IGraphics *pGraphics, ISound *pSound, IStorage *pStorage,
               int Width, int Height, int64_t LocalStartTime, const char *pName) :
    m_pImpl(std::make_unique<SImpl>()), m_pGraphics(pGraphics), m_pSound(pSound),
    m_pStorage(pStorage), m_Width(2560), m_Height(1440),
    m_Fps(std::clamp(g_Config.m_ClVideoRecorderFPS, 1, 120)),
    m_Time(time_get()), m_TickTime(time_freq() / m_Fps),
    m_LocalStartTime(LocalStartTime),
    m_LocalTime((m_Time - LocalStartTime) / (float)time_freq())
{
    str_copy(m_aName, pName);
    ms_pCurrentVideo = this;
}
CVideo::~CVideo() { ms_pCurrentVideo = nullptr; }

bool CVideo::Start()
{
    @autoreleasepool
    {
        if(m_Width < 2 || m_Height < 2)
            return false;
        char aWholePath[IO_MAX_PATH_LENGTH];
        IOHANDLE File = m_pStorage->OpenFile(m_aName, IOFLAG_WRITE, IStorage::TYPE_SAVE,
                                            aWholePath, sizeof(aWholePath));
        if(!File)
        {
            log_error("ios-video", "Cannot create video: %s", m_aName);
            return false;
        }
        io_close(File);
        m_pImpl->m_pPath = [NSString stringWithUTF8String:aWholePath];
        NSURL *Url = [NSURL fileURLWithPath:m_pImpl->m_pPath];
        [[NSFileManager defaultManager] removeItemAtURL:Url error:nil];
        NSError *Error = nil;
        m_pImpl->m_pWriter = [[AVAssetWriter alloc] initWithURL:Url fileType:AVFileTypeMPEG4 error:&Error];
        if(!m_pImpl->m_pWriter)
        {
            log_error("ios-video", "Writer error: %s", Error.localizedDescription.UTF8String);
            return false;
        }
        NSDictionary *Settings = @{
            AVVideoCodecKey: AVVideoCodecTypeH264,
            AVVideoWidthKey: @(m_Width),
            AVVideoHeightKey: @(m_Height),
            AVVideoCompressionPropertiesKey: @{
                AVVideoAverageBitRateKey: @(std::min(20000000, m_Width * m_Height * m_Fps / 12)),
                AVVideoExpectedSourceFrameRateKey: @(m_Fps),
                AVVideoMaxKeyFrameIntervalKey: @(m_Fps)
            }
        };
        m_pImpl->m_pInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo
                                                               outputSettings:Settings];
        m_pImpl->m_pInput.expectsMediaDataInRealTime = NO;
        NSDictionary *Attrs = @{
            (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
            (id)kCVPixelBufferWidthKey: @(m_Width),
            (id)kCVPixelBufferHeightKey: @(m_Height),
            (id)kCVPixelBufferIOSurfacePropertiesKey: @{}
        };
        m_pImpl->m_pAdaptor = [[AVAssetWriterInputPixelBufferAdaptor alloc]
            initWithAssetWriterInput:m_pImpl->m_pInput sourcePixelBufferAttributes:Attrs];
        if(![m_pImpl->m_pWriter canAddInput:m_pImpl->m_pInput])
            return false;
        [m_pImpl->m_pWriter addInput:m_pImpl->m_pInput];
        if(![m_pImpl->m_pWriter startWriting])
        {
            log_error("ios-video", "Cannot start MP4: %s",
                      m_pImpl->m_pWriter.error.localizedDescription.UTF8String);
            return false;
        }
        [m_pImpl->m_pWriter startSessionAtSourceTime:kCMTimeZero];
        m_PreviousBackButton = g_Config.m_ClBackButton;
        m_PreviousTouchControls = g_Config.m_ClTouchControls;
        g_Config.m_ClBackButton = 0;
        g_Config.m_ClTouchControls = 0;
        m_Recording.store(true);
        log_info("ios-video", "Rendering %dx%d at %d FPS", m_Width, m_Height, m_Fps);
        return true;
    }
}

void CVideo::NextVideoFrame()
{
    if(m_Recording.load())
    {
        m_Time += m_TickTime;
        m_LocalTime = (m_Time - m_LocalStartTime) / (float)time_freq();
    }
}

void CVideo::NextVideoFrameThread()
{
    @autoreleasepool
    {
        std::lock_guard<std::mutex> Guard(m_WriteMutex);
        if(!m_Recording.load() || !m_pImpl->m_pWriter ||
           m_pImpl->m_pWriter.status != AVAssetWriterStatusWriting)
            return;
        if(++m_FramesSeen < 2)
            return;

        uint32_t Width = 0, Height = 0;
        CImageInfo::EImageFormat Format = CImageInfo::FORMAT_RGBA;
        std::vector<uint8_t> Pixels;
        auto Readback = m_pGraphics->GetReadPresentedImageDataFuncUnsafe();
        if(!Readback)
            return;
        Readback(Width, Height, Format, Pixels);
        if(Format != CImageInfo::FORMAT_RGBA || Width == 0 || Height == 0 ||
           Pixels.size() < (size_t)Width * Height * 4)
        {
            log_error("ios-video", "Invalid framebuffer");
            return;
        }

        // Do not skip frames: offline export waits for hardware H.264 encoding.
        for(int Wait = 0; !m_pImpl->m_pInput.readyForMoreMediaData && Wait < 6000; ++Wait)
        {
            if(m_pImpl->m_pWriter.status != AVAssetWriterStatusWriting)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if(!m_pImpl->m_pInput.readyForMoreMediaData)
        {
            log_error("ios-video", "Encoder timeout");
            return;
        }

        // Crop symmetrically to 16:9: no stretching or black borders.
        const uint32_t CropWidth = (uint64_t)Width * 9 > (uint64_t)Height * 16
            ? (uint32_t)((uint64_t)Height * 16 / 9) : Width;
        const uint32_t CropHeight = (uint64_t)Width * 9 > (uint64_t)Height * 16
            ? Height : (uint32_t)((uint64_t)Width * 9 / 16);
        if(CropWidth == 0 || CropHeight == 0)
            return;
        const uint32_t StartX = (Width - CropWidth) / 2;
        const uint32_t StartY = (Height - CropHeight) / 2;
        std::vector<uint8_t> Upscaled((size_t)m_Width * m_Height * 4);
        vImage_Buffer Source = {
            Pixels.data() + ((size_t)StartY * Width + StartX) * 4,
            (vImagePixelCount)CropHeight, (vImagePixelCount)CropWidth, (size_t)Width * 4
        };
        vImage_Buffer Destination = {
            Upscaled.data(), (vImagePixelCount)m_Height,
            (vImagePixelCount)m_Width, (size_t)m_Width * 4
        };
        if(vImageScale_ARGB8888(&Source, &Destination, nullptr,
                               kvImageHighQualityResampling) != kvImageNoError)
        {
            log_error("ios-video", "Failed to scale demo to 2560x1440");
            return;
        }
        Pixels.swap(Upscaled);
        Width = (uint32_t)m_Width;

        CVPixelBufferRef Buffer = nullptr;
        CVReturn Status = CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault,
            m_pImpl->m_pAdaptor.pixelBufferPool, &Buffer);
        if(Status != kCVReturnSuccess || !Buffer)
            return;
        CVPixelBufferLockBaseAddress(Buffer, 0);
        auto *Dst = static_cast<uint8_t *>(CVPixelBufferGetBaseAddress(Buffer));
        const size_t RowBytes = CVPixelBufferGetBytesPerRow(Buffer);
        for(int Y = 0; Y < m_Height; ++Y)
        {
            const uint8_t *Src = Pixels.data() + (size_t)Y * Width * 4;
            uint8_t *Row = Dst + (size_t)Y * RowBytes;
            for(int X = 0; X < m_Width; ++X)
            {
                Row[4 * X] = Src[4 * X + 2];
                Row[4 * X + 1] = Src[4 * X + 1];
                Row[4 * X + 2] = Src[4 * X];
                Row[4 * X + 3] = 255;
            }
        }
        CVPixelBufferUnlockBaseAddress(Buffer, 0);
        CMTime Pts = CMTimeMake((int64_t)m_FramesWritten, m_Fps);
        if([m_pImpl->m_pAdaptor appendPixelBuffer:Buffer withPresentationTime:Pts])
            ++m_FramesWritten;
        else
            log_error("ios-video", "Frame encode failed: %s",
                      m_pImpl->m_pWriter.error.localizedDescription.UTF8String);
        CVPixelBufferRelease(Buffer);
    }
}

void CVideo::Stop()
{
    m_pGraphics->WaitForIdle();
    {
        std::lock_guard<std::mutex> Guard(m_WriteMutex);
        m_Recording.store(false);
    }
    g_Config.m_ClBackButton = m_PreviousBackButton;
    g_Config.m_ClTouchControls = m_PreviousTouchControls;
    @autoreleasepool
    {
        if(m_pImpl->m_pWriter && m_pImpl->m_pWriter.status == AVAssetWriterStatusWriting)
        {
            [m_pImpl->m_pInput markAsFinished];
            dispatch_semaphore_t Done = dispatch_semaphore_create(0);
            [m_pImpl->m_pWriter finishWritingWithCompletionHandler:^{
                dispatch_semaphore_signal(Done);
            }];
            dispatch_semaphore_wait(Done, DISPATCH_TIME_FOREVER);
        }
        if(m_pImpl->m_pWriter.status == AVAssetWriterStatusCompleted)
        {
            NSString *Documents = NSSearchPathForDirectoriesInDomains(
                NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
            NSString *Folder = [Documents stringByAppendingPathComponent:@"Rendered Videos"];
            [[NSFileManager defaultManager] createDirectoryAtPath:Folder
                                      withIntermediateDirectories:YES attributes:nil error:nil];
            NSString *Destination = [Folder stringByAppendingPathComponent:
                                      m_pImpl->m_pPath.lastPathComponent];
            if(![Destination isEqualToString:m_pImpl->m_pPath])
            {
                [[NSFileManager defaultManager] removeItemAtPath:Destination error:nil];
                NSError *Error = nil;
                if(![[NSFileManager defaultManager] copyItemAtPath:m_pImpl->m_pPath
                                                            toPath:Destination error:&Error])
                    log_error("ios-video", "Files export error: %s",
                              Error.localizedDescription.UTF8String);
            }
            log_info("ios-video", "MP4 ready in Files: %s", Destination.UTF8String);
        }
        else
            log_error("ios-video", "Video render failed");
    }
    delete this;
}
