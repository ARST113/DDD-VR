#pragma once
#if __has_include(<openxr/openxr.h>)
#include <openxr/openxr.h>
#define HAS_OPENXR 1
#else
#define HAS_OPENXR 0
#endif
#include <vector>

class OpenXrSwapchain {
public:
#if HAS_OPENXR
    bool create(XrSession session, int32_t width, int32_t height);
    int acquireImage();
    void releaseImage();
    XrSwapchain handle() const { return swapchain_; }
    int32_t width() const { return width_; }
    int32_t height() const { return height_; }
#else
    bool create(void*, int32_t, int32_t) { return false; }
    int acquireImage() { return -1; }
    void releaseImage() {}
#endif
private:
#if HAS_OPENXR
    XrSwapchain swapchain_{XR_NULL_HANDLE};
    std::vector<XrSwapchainImageOpenGLESKHR> images_;
    int32_t width_ = 0;
    int32_t height_ = 0;
    uint32_t activeIndex_ = 0;
#endif
};
