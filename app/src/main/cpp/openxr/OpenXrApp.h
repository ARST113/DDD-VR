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
    std::chrono::steady_clock::time_point startTime_{};
    std::chrono::steady_clock::time_point lastWaitLog_{};
    std::string lastError_;
};
