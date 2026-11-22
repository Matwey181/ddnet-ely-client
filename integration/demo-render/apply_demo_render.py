#!/usr/bin/env python3
"""Enable iOS native demo MP4 rendering and clean default demo visuals."""
from pathlib import Path
import re
import sys
root = Path(sys.argv[1])
def replace(path, old, new):
    p = root / path
    data = p.read_text()
    if old not in data:
        raise RuntimeError(f"Missing anchor in {path}: {old[:70]}")
    p.write_text(data.replace(old, new, 1))

replace("CMakeLists.txt", "    src/ios/ios_media.mm\n",
        "    src/ios/ios_media.mm\n    src/ios/ios_video_recorder.mm\n")
replace("CMakeLists.txt",
        "src/ios/ios_media.mm src/ios/ios_music.mm src/ios/ios_performance.mm PROPERTIES",
        "src/ios/ios_media.mm src/ios/ios_music.mm src/ios/ios_performance.mm src/ios/ios_video_recorder.mm PROPERTIES")
replace("CMakeLists.txt",
        '  if(VIDEORECORDER)\n    target_compile_definitions',
        '  if(VIDEORECORDER OR TARGET_OS STREQUAL "ios")\n    target_compile_definitions')
replace("src/engine/client/video.cpp",
        "#if defined(CONF_VIDEORECORDER)\n",
        "#include <base/detect.h>\n#if defined(CONF_VIDEORECORDER) && !defined(CONF_PLATFORM_IOS)\n")

# FFmpeg is not linked on iOS; retain the existing native media decoders.
for media_path in ("src/game/client/components/bestclient/media_decoder.cpp",
                   "src/game/client/components/bestclient/menu_media_background.cpp"):
    media = root / media_path
    data = media.read_text()
    data = data.replace("#if defined(CONF_VIDEORECORDER)\n",
                        "#if defined(CONF_VIDEORECORDER) && !defined(CONF_PLATFORM_IOS)\n")
    if media_path.endswith("media_decoder.cpp"):
        data = data.replace("#if !defined(CONF_VIDEORECORDER)\n",
                            "#if !defined(CONF_VIDEORECORDER) || defined(CONF_PLATFORM_IOS)\n")
    media.write_text(data)

# Also hide overlays for existing installations with a saved settings.cfg.
replace("src/engine/client/client.cpp",
        "\tStartVideo(pVideoName, false);\n",
        """#if defined(CONF_PLATFORM_IOS)
    g_Config.m_ClNamePlates = 0;
    g_Config.m_ClSpecCursor = 0;
    g_Config.m_ClShowhud = 0;
    g_Config.m_ClShowChat = 0;
    g_Config.m_ClShowKillMessages = 0;
    g_Config.m_ClShowFinishMessages = 0;
    g_Config.m_ClVideoShowChat = 0;
    g_Config.m_ClVideoShowImportantAlerts = 0;
#endif
\tStartVideo(pVideoName, false);
""")

p = root / "src/engine/client/video.h"
data = p.read_text()
data = data.replace("#define ENGINE_CLIENT_VIDEO_H\n",
                    '#define ENGINE_CLIENT_VIDEO_H\n#include <base/detect.h>\n#if defined(CONF_PLATFORM_IOS)\n#include "ios_video_recorder.h"\n#else\n', 1)
assert data.rstrip().endswith("#endif")
idx = data.rfind("#endif")
p.write_text(data[:idx] + "#endif // iOS\n" + data[idx:])

p = root / "src/engine/shared/config_variables.h"
data = p.read_text()
for name in ["ClNamePlates", "ClShowhud", "ClShowChat", "ClShowKillMessages",
             "ClShowFinishMessages", "ClSpecCursor", "ClVideoShowChat",
             "ClVideoShowImportantAlerts"]:
    match = re.search(r"(?m)^MACRO_CONFIG_INT\(" + name + r", [^\n]+\)$", data)
    if not match:
        raise RuntimeError("Config variable not found: " + name)
    old = match.group(0)
    new = old.replace(", 1, 0,", ", 0, 0,", 1)
    if new == old:
        raise RuntimeError("Default was not 1: " + name)
    data = data.replace(old, "#if defined(CONF_PLATFORM_IOS)\n" + new +
                        "\n#else\n" + old + "\n#endif", 1)
p.write_text(data)

(root / "src/engine/client/ios_video_recorder.h").write_text(r'''#ifndef ENGINE_CLIENT_IOS_VIDEO_RECORDER_H
#define ENGINE_CLIENT_IOS_VIDEO_RECORDER_H
#include <engine/shared/video.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
class IGraphics;
class ISound;
class IStorage;
class CVideo final : public IVideo
{
public:
    CVideo(IGraphics *pGraphics, ISound *pSound, IStorage *pStorage,
           int Width, int Height, int64_t LocalStartTime, const char *pName);
    ~CVideo() override;
    static void Init() {}
    bool Start() override;
    void Stop() override;
    void Pause(bool Pause) override { m_Recording.store(!Pause); }
    bool IsRecording() const override { return m_Recording.load(); }
    void NextVideoFrame() override;
    void NextVideoFrameThread() override;
    void NextAudioFrame(ISoundMixFunc Mix) override { (void)Mix; }
    void NextAudioFrameTimeline(ISoundMixFunc Mix) override { (void)Mix; }
    int64_t Time() const override { return m_Time; }
    float LocalTime() const override { return m_LocalTime; }
    void SetLocalStartTime(int64_t Value) override { m_LocalStartTime = Value; }
private:
    struct SImpl;
    std::unique_ptr<SImpl> m_pImpl;
    IGraphics *m_pGraphics;
    ISound *m_pSound;
    IStorage *m_pStorage;
    int m_Width;
    int m_Height;
    int m_Fps;
    int m_PreviousBackButton = 1;
    int64_t m_Time;
    int64_t m_TickTime;
    int64_t m_LocalStartTime;
    float m_LocalTime;
    uint64_t m_FramesSeen = 0;
    uint64_t m_FramesWritten = 0;
    std::atomic<bool> m_Recording{false};
    std::mutex m_WriteMutex;
    char m_aName[256];
};
#endif
''')

(root / "src/ios/ios_video_recorder.mm").write_text(r'''#include <engine/client/ios_video_recorder.h>
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
    m_pStorage(pStorage), m_Width(Width & ~1), m_Height(Height & ~1),
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
        g_Config.m_ClBackButton = 0;
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
        if(Format != CImageInfo::FORMAT_RGBA || Width < (uint32_t)m_Width ||
           Height < (uint32_t)m_Height || Pixels.size() < (size_t)Width * Height * 4)
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
''')
print("Installed AVFoundation renderer and iOS clean demo defaults")
