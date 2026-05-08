#pragma once
#include <thread>
#include <atomic>
#include "OpenXrSession.h"
#include "OpenXrSwapchain.h"
#include "OpenXrRenderer.h"
#include "OpenXrInput.h"
class OpenXrApp {
public:
    bool initialize();
    bool start();
    void pause();
    void resume();
    void destroy();
    bool isRuntimeAvailable() const { return session_.runtimeAvailable(); }
    const std::string& lastError() const { return session_.lastError(); }
    unsigned int videoTextureId() const { return renderer_.videoTextureId(); }
private:
    void loop();
    OpenXrSession session_;
    OpenXrSwapchain swapchain_;
    OpenXrRenderer renderer_;
    OpenXrInput input_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};
