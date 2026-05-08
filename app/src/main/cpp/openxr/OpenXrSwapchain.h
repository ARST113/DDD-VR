#pragma once
#include <openxr/openxr.h>
#include <GLES3/gl3.h>
#include <vector>

class OpenXrSwapchain {
public:
    bool create(XrSession session, int32_t width, int32_t height);
    int acquireImage();
    void releaseImage();
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
};
