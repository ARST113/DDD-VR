#pragma once

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

class FfmpegAudioOutput {
public:
    FfmpegAudioOutput() = default;
    ~FfmpegAudioOutput();

    bool start(int sampleRate, int channelCount);
    void stop();
    void flush(int64_t positionUs);
    void setPlaying(bool playing);
    void setMuted(bool muted);
    bool enqueue(const int16_t* samples, int frameCount, int64_t ptsUs);

    bool active() const { return active_.load(); }
    bool buffering() const;
    int64_t positionUs() const { return playedPositionUs_.load(); }
    uint64_t underflowCount() const { return underflowCount_.load(); }

private:
    struct AudioChunk {
        std::vector<int16_t> samples;
        int frameCount = 0;
        int64_t ptsUs = 0;
        int64_t endUs = 0;
    };

    static void bufferQueueCallback(SLAndroidSimpleBufferQueueItf queue, void* context);
    void onBufferConsumed();
    bool submitAvailableLocked();
    void destroyObjects();

    static constexpr size_t kMaxQueuedChunks = 12;
    static constexpr size_t kHardwareQueueDepth = 4;

    SLObjectItf engineObject_ = nullptr;
    SLEngineItf engine_ = nullptr;
    SLObjectItf outputMixObject_ = nullptr;
    SLObjectItf playerObject_ = nullptr;
    SLPlayItf player_ = nullptr;
    SLAndroidSimpleBufferQueueItf bufferQueue_ = nullptr;
    SLVolumeItf volume_ = nullptr;

    int sampleRate_ = 48000;
    int channelCount_ = 2;
    std::atomic<bool> active_{false};
    std::atomic<bool> playing_{true};
    std::atomic<bool> muted_{false};
    std::atomic<int64_t> playedPositionUs_{-1};
    std::atomic<uint64_t> underflowCount_{0};
    mutable std::mutex mutex_;
    std::condition_variable spaceAvailable_;
    std::deque<AudioChunk> pending_;
    std::deque<AudioChunk> submitted_;
};
