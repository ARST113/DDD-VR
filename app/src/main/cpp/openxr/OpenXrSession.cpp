#include "OpenXrSession.h"
#include "../util/XrLog.h"
#if HAS_OPENXR
#include <cstring>
#endif

bool OpenXrSession::initialize() {
#if HAS_OPENXR
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strcpy(ci.applicationInfo.applicationName, "DDD-VR");
    ci.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    if (xrCreateInstance(&ci, &instance_) != XR_SUCCESS) { lastError_ = "xrCreateInstance failed"; return false; }
    XR_LOGI("DDDVR/OpenXRSession", "xrCreateInstance success");

    XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (xrGetSystem(instance_, &sgi, &systemId_) != XR_SUCCESS) { lastError_ = "xrGetSystem failed"; return false; }
    XR_LOGI("DDDVR/OpenXRSession", "xrGetSystem success");
    runtimeAvailable_ = true;
    return true;
#else
    lastError_ = "OpenXR unavailable";
    return false;
#endif
}
bool OpenXrSession::createSession(){
#if HAS_OPENXR
    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO}; sci.systemId = systemId_;
    if (xrCreateSession(instance_, &sci, &session_) != XR_SUCCESS) { lastError_="xrCreateSession failed"; return false; }
    XR_LOGI("DDDVR/OpenXRSession","xrCreateSession success");
    return true;
#else
    return false;
#endif
}
bool OpenXrSession::createReferenceSpace(){
#if HAS_OPENXR
    XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO}; rs.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL; rs.poseInReferenceSpace.orientation.w=1.f;
    if (xrCreateReferenceSpace(session_, &rs, &appSpace_) != XR_SUCCESS) { lastError_="xrCreateReferenceSpace failed"; return false; }
    return true;
#else
    return false;
#endif
}
bool OpenXrSession::begin(){
#if HAS_OPENXR
    XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO}; bi.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    return xrBeginSession(session_, &bi) == XR_SUCCESS;
#else
    return false;
#endif
}
void OpenXrSession::pollEvents(){
#if HAS_OPENXR
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(instance_, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto* s = reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
            state_ = s->state;
            XR_LOGI("DDDVR/OpenXRSession", "session state=%d", state_);
        }
        ev = {XR_TYPE_EVENT_DATA_BUFFER};
    }
#endif
}
