#pragma once
#include <string>
#if __has_include(<openxr/openxr.h>)
#include <openxr/openxr.h>
#define HAS_OPENXR 1
#else
#define HAS_OPENXR 0
using XrSessionState = int;
#endif

class OpenXrSession {
public:
    bool initialize();
    bool createSession();
    bool createReferenceSpace();
    bool begin();
    void pollEvents();
    bool runtimeAvailable() const { return runtimeAvailable_; }
    const std::string& lastError() const { return lastError_; }
#if HAS_OPENXR
    XrInstance instance() const { return instance_; }
    XrSystemId systemId() const { return systemId_; }
    XrSession session() const { return session_; }
    XrSpace appSpace() const { return appSpace_; }
#endif
private:
    bool runtimeAvailable_ = false;
    std::string lastError_;
#if HAS_OPENXR
    XrInstance instance_{XR_NULL_HANDLE};
    XrSystemId systemId_{XR_NULL_SYSTEM_ID};
    XrSession session_{XR_NULL_HANDLE};
    XrSpace appSpace_{XR_NULL_HANDLE};
    XrSessionState state_{XR_SESSION_STATE_UNKNOWN};
#endif
};
