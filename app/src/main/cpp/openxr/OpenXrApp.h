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

    OpenXrSession session_;
    OpenXrSwapchain swapchain_;
    OpenXrRenderer renderer_;
    OpenXrInput input_;
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
};
