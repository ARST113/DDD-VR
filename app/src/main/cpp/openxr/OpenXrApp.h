#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>
#include <string>

#include "OpenXrInput.h"
#include "OpenXrRenderer.h"
#include "OpenXrSession.h"
#include "OpenXrSwapchain.h"

class OpenXrApp {
public:
    OpenXrApp() = default;
    explicit OpenXrApp(OpenXrRenderConfig renderConfig) : renderConfig_(renderConfig) {}
    ~OpenXrApp();

    void setJavaBridge(JNIEnv* env, jobject bridge);
    void setVideoSize(int32_t width, int32_t height);
    void setUiState(bool visible, int32_t progressPermille, bool playing);

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
    bool createVideoSurfaceOnRenderThread();
    bool updateVideoSurfaceOnRenderThread();
    void applyPendingVideoSizeOnRenderThread(JNIEnv* env);
    void releaseVideoSurfaceOnRenderThread();
    void releaseJavaRefs();

    OpenXrSession session_;
    OpenXrSwapchain swapchain_;
    OpenXrRenderer renderer_;
    OpenXrInput input_;
    OpenXrRenderConfig renderConfig_;
    std::thread thread_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> sessionRunning_{false};
    std::atomic<bool> pendingStart_{false};
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
    std::string lastError_;

    jobject javaBridgeRef_ = nullptr;
    jclass videoSurfaceClass_ = nullptr;
    jmethodID bridgeOnVideoSurfaceReady_ = nullptr;
    jmethodID bridgeOnInputAction_ = nullptr;
    jmethodID bridgeOnTimelineSeek_ = nullptr;
    jmethodID videoSurfaceCtor_ = nullptr;
    jmethodID videoSurfaceGetSurface_ = nullptr;
    jmethodID videoSurfaceUpdateTexImage_ = nullptr;
    jmethodID videoSurfaceSetDefaultBufferSize_ = nullptr;
    jmethodID videoSurfaceRelease_ = nullptr;
    jobject videoSurfaceRef_ = nullptr;
    jfloatArray videoTransformArray_ = nullptr;
    float videoTransform_[16] = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f
    };
    std::atomic<int32_t> pendingVideoWidth_{0};
    std::atomic<int32_t> pendingVideoHeight_{0};
    int32_t appliedVideoWidth_ = 0;
    int32_t appliedVideoHeight_ = 0;
    bool videoFrameSeen_ = false;
    uint64_t videoFrameUpdateCount_ = 0;
};
