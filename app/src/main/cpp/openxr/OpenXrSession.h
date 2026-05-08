#pragma once
#include <string>
#include "OpenXrPlatform.h"

class OpenXrSession {
public:
    bool initialize();
    bool createSession();
    bool createReferenceSpace();
    bool begin();
    void pollEvents();
    bool runtimeAvailable() const { return runtimeAvailable_; }
    const std::string& lastError() const { return lastError_; }
    XrInstance instance() const { return instance_; }
    XrSystemId systemId() const { return systemId_; }
    XrSession session() const { return session_; }
    XrSpace appSpace() const { return appSpace_; }
    uint32_t recommendedWidth() const { return recWidth_; }
    uint32_t recommendedHeight() const { return recHeight_; }
private:
    bool runtimeAvailable_ = false;
    std::string lastError_;
    bool initEgl();
    bool hasExtension(const char* name);
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
};
