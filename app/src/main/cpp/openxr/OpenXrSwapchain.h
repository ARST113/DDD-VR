#pragma once

#include "OpenXrPlatform.h"
#include <vector>

class OpenXrSwapchain {
public:
    bool create(XrSession session, int32_t width, int32_t height);
    bool acquireImage();
    bool releaseImage();
    void destroy();
    XrSwapchain handle() const { return swapchain_; }
    int32_t width() const { return width_; }
    int32_t height() const { return height_; }
    GLuint activeColorTexture() const;

private:
    XrSwapchain swapchain_{XR_NULL_HANDLE};
    std::vector<XrSwapchainImageOpenGLESKHR> images_;
    int32_t width_ = 0;
    int32_t height_ = 0;
    uint32_t activeIndex_ = 0;
    bool imageAcquired_ = false;
};
