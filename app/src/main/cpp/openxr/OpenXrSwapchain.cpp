#include "OpenXrSwapchain.h"
#include "../util/XrLog.h"
#if HAS_OPENXR
bool OpenXrSwapchain::create(XrSession session, int32_t w, int32_t h) {
    width_ = w; height_ = h;
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.format = GL_RGBA8;
    ci.sampleCount = 1;
    ci.width = (uint32_t)w;
    ci.height = (uint32_t)h;
    ci.faceCount = 1;
    ci.arraySize = 2;
    ci.mipCount = 1;
    if (xrCreateSwapchain(session, &ci, &swapchain_) != XR_SUCCESS) return false;
    uint32_t count = 0;
    xrEnumerateSwapchainImages(swapchain_, 0, &count, nullptr);
    images_.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
    xrEnumerateSwapchainImages(swapchain_, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(images_.data()));
    XR_LOGI("DDDVR/OpenXRSession", "swapchain created images=%u", count);
    return true;
}
int OpenXrSwapchain::acquireImage() {
    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    xrAcquireSwapchainImage(swapchain_, &ai, &activeIndex_);
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wi.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(swapchain_, &wi);
    return (int)activeIndex_;
}
void OpenXrSwapchain::releaseImage() {
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(swapchain_, &ri);
}
#endif
