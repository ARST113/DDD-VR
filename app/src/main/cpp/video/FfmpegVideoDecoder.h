#pragma once

#include "FfmpegAudioOutput.h"
#include "DolbyRpuParser.h"
#include "FfmpegVideoFrame.h"
#include <android/hardware_buffer.h>
#include <jni.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <map>
#include <string>
#include <thread>
#include <vector>

struct FfmpegAudioTrackInfo {
    std::string id;
    std::string title;
    std::string subtitle;
    int streamIndex = -1;
    bool selected = false;
};

struct FfmpegPlaybackSnapshot {
    bool running = false;
    bool playing = false;
    bool buffering = false;
    bool hdr = false;
    bool audioActive = false;
    int64_t positionMs = 0;
    int64_t durationMs = 0;
    int64_t bufferedPositionMs = 0;
    int width = 0;
    int height = 0;
    int selectedAudioStream = -1;
};

class FfmpegVideoDecoder {
public:
    FfmpegVideoDecoder() = default;
    ~FfmpegVideoDecoder();

    bool start(
        const std::string& uri,
        int64_t startPositionMs,
        JavaVM* javaVm,
        jobject outputSurface
    );
    void stop();
    void setPlaybackState(bool playing, int64_t positionMs, bool forceSeek);
    void setMuted(bool muted);
    bool selectAudioTrack(const std::string& trackId);
    FfmpegPlaybackSnapshot playbackSnapshot() const;
    std::vector<FfmpegAudioTrackInfo> audioTracks() const;
    bool pollDirectFrame(FfmpegVideoFrame* outFrame);
    void releaseDirectFrame();
    bool pollFrame(FfmpegVideoFrame* outFrame);
    bool pollHardwareBufferFrame(FfmpegHardwareBufferFrame* outFrame);
    bool pollHardwareSurfaceMetadata(FfmpegVideoFrame* outFrame, int64_t surfacePtsUs = -1);
    bool running() const { return running_.load(); }
    bool usingHardwareSurface() const { return hardwareSurfaceActive_.load(); }
    bool linked() const;
    std::string lastError() const;

private:
    void decodeLoop(std::string uri, int64_t startPositionMs);
    void pushFrame(FfmpegVideoFrame&& frame);
    bool publishDirectFrame(FfmpegVideoFrame&& frame);
    bool createP010ImageReader(int width, int height, ANativeWindow** outWindow);
    void destroyP010ImageReader();
    void setError(const std::string& error);
    void storeDolbyMetadata(std::shared_ptr<const DolbyRpuMetadata> metadata);
    std::shared_ptr<const DolbyRpuMetadata> dolbyMetadataForPts(int64_t ptsUs);
    void clearDolbyMetadata(int dolbyProfile);

    std::atomic<bool> running_{false};
    std::atomic<bool> playing_{true};
    std::atomic<bool> seekRequested_{false};
    std::atomic<bool> seekInFlight_{false};
    std::atomic<bool> hardwareSurfaceActive_{false};
    std::atomic<bool> buffering_{true};
    std::atomic<bool> hdr_{false};
    std::atomic<bool> muted_{false};
    std::atomic<int64_t> durationMs_{0};
    std::atomic<int32_t> videoWidth_{0};
    std::atomic<int32_t> videoHeight_{0};
    std::atomic<int32_t> selectedAudioStream_{-1};
    std::atomic<int32_t> requestedAudioStream_{-1};
    std::atomic<int64_t> requestedPositionMs_{0};
    std::atomic<int64_t> playbackStateUpdateNs_{0};
    std::atomic<int64_t> lastPresentedPositionMs_{-1};
    std::atomic<int32_t> hardwareTransfer_{0};
    std::atomic<int32_t> hardwarePrimaries_{0};
    std::atomic<int32_t> hardwareRange_{0};
    std::atomic<int32_t> hardwareDolbyProfile_{0};
    DolbyRpuParser dolbyRpuParser_;
    JavaVM* javaVm_ = nullptr;
    jobject outputSurface_ = nullptr;
    FfmpegAudioOutput audioOutput_;
    std::thread thread_;
    mutable std::mutex mutex_;
    mutable std::mutex directFrameMutex_;
    mutable std::mutex p010ImageReaderMutex_;
    mutable std::mutex dolbyMetadataMutex_;
    AImageReader* p010ImageReader_ = nullptr;
    std::condition_variable directFrameConsumed_;
    FfmpegVideoFrame directFrame_;
    bool directFrameAvailable_ = false;
    bool directFrameAcquired_ = false;
    std::deque<FfmpegVideoFrame> frames_;
    std::map<int64_t, std::shared_ptr<const DolbyRpuMetadata>> dolbyMetadataByPts_;
    std::vector<FfmpegAudioTrackInfo> audioTracks_;
    std::string lastError_;
};
