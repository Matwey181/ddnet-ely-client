#ifndef ENGINE_CLIENT_IOS_VIDEO_RECORDER_H
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
