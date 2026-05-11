#include "OpenXrSwapchain.h"
#include "../util/XrLog.h"

bool OpenXrSwapchain::create(XrSession session, int32_t w, int32_t h) {
    width_ = w; height_ = h;
    uint32_t fmtCount = 0;
    xrEnumerateSwapchainFormats(session, 0, &fmtCount, nullptr);
    std::vector<int64_t> formats(fmtCount);
    xrEnumerateSwapchainFormats(session, fmtCount, &fmtCount, formats.data());
    int64_t chosen = 0; std::vector<int64_t> pref = {GL_SRGB8_ALPHA8, GL_RGBA8, 0x8058};
    for (auto p : pref) { for (auto f : formats) { if (f == p) { chosen = p; break; } } if (chosen != 0) break; }
    XR_LOGI("DDDVR/OpenXRSession", "xrEnumerateSwapchainFormats count=%u chosen=%lld", fmtCount, (long long)chosen);
    if (chosen == 0) { XR_LOGE("DDDVR/OpenXRCheck", "SWAPCHAIN_FAIL"); return false; }
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO}; ci.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT; ci.format = chosen; ci.sampleCount = 1; ci.width = (uint32_t)w; ci.height = (uint32_t)h; ci.faceCount = 1; ci.arraySize = 2; ci.mipCount = 1;
    XrResult r = xrCreateSwapchain(session, &ci, &swapchain_); XR_LOGI("DDDVR/OpenXRSession", "xrCreateSwapchain result=%d", r); if (r != XR_SUCCESS) return false;
    uint32_t count = 0; r = xrEnumerateSwapchainImages(swapchain_, 0, &count, nullptr); if (r != XR_SUCCESS || count == 0) return false;
    images_.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR}); r = xrEnumerateSwapchainImages(swapchain_, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(images_.data()));
    const bool ok = r == XR_SUCCESS; XR_LOGI("DDDVR/OpenXRCheck", ok ? "SWAPCHAIN_OK" : "SWAPCHAIN_FAIL"); return ok;
}

bool OpenXrSwapchain::acquireImage() { XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO}; XrResult ar = xrAcquireSwapchainImage(swapchain_, &ai, &activeIndex_); XR_LOGI("DDDVR/OpenXRRenderer", "xrAcquireSwapchainImage result=%d idx=%u", ar, activeIndex_); if (ar != XR_SUCCESS) return false; XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wi.timeout = XR_INFINITE_DURATION; XrResult wr = xrWaitSwapchainImage(swapchain_, &wi); XR_LOGI("DDDVR/OpenXRRenderer", "xrWaitSwapchainImage result=%d", wr); imageAcquired_ = (wr == XR_SUCCESS); return imageAcquired_; }

bool OpenXrSwapchain::releaseImage() { if (!imageAcquired_) return true; XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO}; XrResult r = xrReleaseSwapchainImage(swapchain_, &ri); XR_LOGI("DDDVR/OpenXRRenderer", "xrReleaseSwapchainImage result=%d", r); imageAcquired_ = false; const bool ok = r == XR_SUCCESS; XR_LOGI("DDDVR/OpenXRCheck", ok ? "SWAPCHAIN_OK" : "SWAPCHAIN_FAIL"); return ok; }
void OpenXrSwapchain::destroy() { if (swapchain_ != XR_NULL_HANDLE) { xrDestroySwapchain(swapchain_); swapchain_ = XR_NULL_HANDLE; } }
GLuint OpenXrSwapchain::activeColorTexture() const { return activeIndex_ < images_.size() ? images_[activeIndex_].image : 0; }
