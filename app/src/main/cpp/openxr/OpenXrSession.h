#pragma once

#include "OpenXrPlatform.h"

#include <string>
#include <vector>

class OpenXrSession {
public:
    bool initializeLoaderAndInstance();
    bool prepareGraphics();
    bool createSession();
    bool createReferenceSpace();
    bool begin();
    bool end();
    void pollEvents();
    void shutdown();
    bool setHdrColorSpace(bool hdrVideo);
    XrSessionState currentState() const { return state_; }

    bool runtimeAvailable() const { return runtimeAvailable_; }
    bool hasInstance() const { return instance_ != XR_NULL_HANDLE; }
    bool isInitialized() const { return instance_ != XR_NULL_HANDLE && systemId_ != XR_NULL_SYSTEM_ID; }
    const std::string& lastError() const { return lastError_; }
    XrInstance instance() const { return instance_; }
    XrSession session() const { return session_; }
    XrSpace appSpace() const { return appSpace_; }
    uint32_t recommendedWidth() const { return recWidth_; }
    uint32_t recommendedHeight() const { return recHeight_; }

private:
    bool hasExtension(const char* name);
    void initializeColorSpaceExtension();
    bool supportsColorSpace(XrColorSpaceFB colorSpace) const;
    bool setColorSpaceFromPreference(const std::vector<XrColorSpaceFB>& preference, const char* reason);

    bool runtimeAvailable_ = false;
    std::string lastError_;
    XrInstance instance_{XR_NULL_HANDLE};
    XrSystemId systemId_{XR_NULL_SYSTEM_ID};
    XrSession session_{XR_NULL_HANDLE};
    XrSpace appSpace_{XR_NULL_HANDLE};
    XrSessionState state_{XR_SESSION_STATE_UNKNOWN};
    EGLDisplay eglDisplay_{EGL_NO_DISPLAY};
    EGLConfig eglConfig_{};
    EGLContext eglContext_{EGL_NO_CONTEXT};
    EGLSurface eglSurface_{EGL_NO_SURFACE};
    uint32_t recWidth_ = 2048;
    uint32_t recHeight_ = 2048;
    bool fbColorSpaceEnabled_ = false;
    bool fbColorSpaceReady_ = false;
    PFN_xrEnumerateColorSpacesFB xrEnumerateColorSpacesFB_ = nullptr;
    PFN_xrSetColorSpaceFB xrSetColorSpaceFB_ = nullptr;
    std::vector<XrColorSpaceFB> supportedColorSpaces_;
    XrColorSpaceFB currentColorSpace_ = XR_COLOR_SPACE_MAX_ENUM_FB;
};
