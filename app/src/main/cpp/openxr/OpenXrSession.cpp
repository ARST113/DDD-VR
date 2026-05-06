#include "OpenXrSession.h"
#include "../util/XrLog.h"
#if __has_include(<openxr/openxr.h>)
#include <openxr/openxr.h>
#define HAS_OPENXR 1
#else
#define HAS_OPENXR 0
#endif

bool OpenXrSession::initialize() {
#if HAS_OPENXR
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy(ci.applicationInfo.applicationName,"DDD-VR");
    ci.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    XrInstance instance{XR_NULL_HANDLE};
    auto r = xrCreateInstance(&ci,&instance);
    if (r!=XR_SUCCESS){ lastError_="xrCreateInstance failed"; XR_LOGE("DDDVR/OpenXRSession","xrCreateInstance failed"); return false; }
    XR_LOGI("DDDVR/OpenXRSession","xrCreateInstance success");
    runtimeAvailable_ = true;
    return true;
#else
    lastError_ = "OpenXR headers/runtime unavailable at build/runtime";
    XR_LOGE("DDDVR/OpenXRSession","OpenXR unavailable");
    return false;
#endif
}
bool OpenXrSession::begin(){ if(!runtimeAvailable_) return false; XR_LOGI("DDDVR/OpenXRSession","xrBeginSession success (stubbed)"); return true; }
void OpenXrSession::pollEvents(){ XR_LOGI("DDDVR/OpenXRSession","session state XR_SESSION_STATE_VISIBLE (stub)"); }
