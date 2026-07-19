#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>
#include <string>
#include <vector>

#include "OpenXrInput.h"
#include "OpenXrRenderer.h"
#include "OpenXrSession.h"
#include "OpenXrSwapchain.h"
#include "../video/FfmpegVideoDecoder.h"

class OpenXrApp {
public:
    OpenXrApp() = default;
    explicit OpenXrApp(OpenXrRenderConfig renderConfig) : renderConfig_(renderConfig) {}
    ~OpenXrApp();

    void setJavaBridge(JNIEnv* env, jobject bridge);
    void setVideoSize(int32_t width, int32_t height, float pixelWidthHeightRatio);
    void setDisplayAspectRatio(float aspectRatio);
    void setUiState(
        bool visible,
        bool playing,
        bool buffering,
        int64_t positionMs,
        int64_t durationMs,
        int64_t bufferedPositionMs,
        const std::string& title,
        const std::string& stereoModeLabel,
        const std::string& audioTrackLabel,
        const std::vector<std::string>& audioTrackLabels,
        int selectedAudioTrackIndex
    );
    void setPlayerUiState(const VrPlayerUiState& state);
    void startFfmpegVideoSource(const std::string& uri, int64_t startPositionMs);
    void stopFfmpegVideoSource();
    void setFfmpegPlaybackState(bool playing, int64_t positionMs, bool forceSeek);
    void setFfmpegMuted(bool muted);
    bool selectFfmpegAudioTrack(const std::string& trackId);
    FfmpegPlaybackSnapshot ffmpegPlaybackSnapshot() const;
    std::vector<FfmpegAudioTrackInfo> ffmpegAudioTracks() const;

    bool initialize();
    bool start();
    void pause();
    void resume();
    void destroy();

    bool isRuntimeAvailable() const { return session_.runtimeAvailable(); }
    const std::string& lastError() const { return lastError_; }

private:
    void loop();
    void stopAndJoinThread(const char* reason);
    bool initOnRenderThread();
    JNIEnv* attachCurrentThread(bool* didAttach) const;
    void detachCurrentThread(bool didAttach) const;
    void dispatchInputActionOnRenderThread(OpenXrInputActionCode code);
    void dispatchTimelineSeekOnRenderThread(int32_t progressPermille);
    void dispatchAudioTrackSelectedOnRenderThread(int32_t trackIndex);
    void dispatchPlayerUiActionOnRenderThread(const VrPlayerPanelAction& action);
    void applyPendingXrColorSpaceOnRenderThread();
    void applyPendingFfmpegVideoRequestOnRenderThread();
    bool createVideoSurfaceOnRenderThread();
    bool updateVideoSurfaceOnRenderThread();
    void applyPendingVideoSizeOnRenderThread(JNIEnv* env);
    void releaseVideoSurfaceOnRenderThread();
    void releaseJavaRefs();

    OpenXrSession session_;
    OpenXrSwapchain swapchain_;
    OpenXrRenderer renderer_;
    FfmpegVideoDecoder ffmpegVideoDecoder_;
    OpenXrInput input_;
    OpenXrRenderConfig renderConfig_;
    std::thread thread_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> sessionRunning_{false};
    std::atomic<bool> pendingStart_{false};
    std::atomic<bool> pendingFfmpegVideoRequest_{false};
    std::atomic<bool> pendingFfmpegStopRequest_{false};
    std::mutex initMutex_;
    std::condition_variable initCv_;
    bool initDone_ = false;
    bool initOk_ = false;
    std::atomic<bool> firstFrameSubmitted_{false};
    std::atomic<bool> exitRequested_{false};
    std::atomic<bool> restartRequested_{false};
    std::atomic<bool> androidPaused_{false};
    std::atomic<bool> stoppedBySeethroughOrFocusLoss_{false};
    uint64_t frameCount_{0};
    uint64_t frameCountBeforeStop_{0};
    uint64_t beginSessionCount_{0};
    uint64_t endSessionCount_{0};
    std::chrono::steady_clock::time_point startTime_{};
    std::chrono::steady_clock::time_point lastWaitLog_{};
    std::atomic<bool> fboOkSeen_{false};
    std::atomic<int32_t> pendingHdrColorSpaceIntent_{0};
    int32_t appliedHdrColorSpaceIntent_ = -1;
    std::string lastError_;
    std::mutex ffmpegVideoMutex_;
    std::string pendingFfmpegVideoUri_;
    int64_t pendingFfmpegVideoStartMs_ = 0;

    jobject javaBridgeRef_ = nullptr;
    jclass videoSurfaceClass_ = nullptr;
    jmethodID bridgeOnVideoSurfaceReady_ = nullptr;
    jmethodID bridgeOnInputAction_ = nullptr;
    jmethodID bridgeOnTimelineSeek_ = nullptr;
    jmethodID bridgeOnAudioTrackSelected_ = nullptr;
    jmethodID bridgeOnPlayerUiAction_ = nullptr;
    jmethodID videoSurfaceCtor_ = nullptr;
    jmethodID videoSurfaceGetSurface_ = nullptr;
    jmethodID videoSurfaceUpdateTexImage_ = nullptr;
    jmethodID videoSurfaceTimestampNs_ = nullptr;
    jmethodID videoSurfaceSetDefaultBufferSize_ = nullptr;
    jmethodID videoSurfaceRelease_ = nullptr;
    jobject videoSurfaceRef_ = nullptr;
    jobject videoDecoderSurfaceRef_ = nullptr;
    jfloatArray videoTransformArray_ = nullptr;
    int64_t videoSurfaceTimestampNsValue_ = -1;
    float videoTransform_[16] = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f
    };
    std::atomic<int32_t> pendingVideoWidth_{0};
    std::atomic<int32_t> pendingVideoHeight_{0};
    std::atomic<float> pendingVideoPixelWidthHeightRatio_{1.f};
    std::atomic<float> pendingDisplayAspectRatio_{0.f};
    int32_t appliedVideoWidth_ = 0;
    int32_t appliedVideoHeight_ = 0;
    int32_t appliedRendererVideoWidth_ = 0;
    int32_t appliedRendererVideoHeight_ = 0;
    float appliedRendererPixelWidthHeightRatio_ = 0.f;
    float appliedDisplayAspectRatio_ = -1.f;
    bool videoFrameSeen_ = false;
    uint64_t videoFrameUpdateCount_ = 0;
    uint64_t videoFrameStatsBaseCount_ = 0;
    std::chrono::steady_clock::time_point videoFrameStatsStart_{};
};
