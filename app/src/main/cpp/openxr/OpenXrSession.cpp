#include "OpenXrSession.h"
#include "../util/XrLog.h"
#if HAS_OPENXR
#include <cstring>
#include <vector>
#include <openxr/openxr_platform.h>

bool OpenXrSession::hasExtension(const char* name) {
    uint32_t count=0; xrEnumerateInstanceExtensionProperties(nullptr,0,&count,nullptr);
    std::vector<XrExtensionProperties> exts(count, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr,count,&count,exts.data());
    for (auto &e: exts) { XR_LOGI("DDDVR/OpenXRSession","ext=%s",e.extensionName); if (strcmp(e.extensionName,name)==0) return true; }
    return false;
}

bool OpenXrSession::initEgl() {
    eglDisplay_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay_ == EGL_NO_DISPLAY) return false;
    EGLint major=0,minor=0; if(!eglInitialize(eglDisplay_, &major, &minor)) return false;
    const EGLint attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLint num=0; if(!eglChooseConfig(eglDisplay_, attrs, &eglConfig_, 1, &num) || num<1) return false;
    const EGLint pbuf[] = {EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
    eglSurface_ = eglCreatePbufferSurface(eglDisplay_, eglConfig_, pbuf);
    const EGLint ctx[] = {EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, ctx);
    if (eglContext_ == EGL_NO_CONTEXT) return false;
    return eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_) == EGL_TRUE;
}
#endif

bool OpenXrSession::initialize() {
#if HAS_OPENXR
    if (!hasExtension(XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME)) { lastError_ = "XR_KHR_opengl_es_enable missing"; return false; }
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strcpy(ci.applicationInfo.applicationName, "DDD-VR");
    ci.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    const char* exts[] = {XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
    ci.enabledExtensionCount = 1; ci.enabledExtensionNames = exts;
    XrResult r = xrCreateInstance(&ci, &instance_); if (r != XR_SUCCESS) { lastError_ = "xrCreateInstance failed"; XR_LOGE("DDDVR/OpenXRSession","xrCreateInstance=%d",r); return false; }
    XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO}; sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = xrGetSystem(instance_, &sgi, &systemId_); if (r != XR_SUCCESS) { lastError_ = "xrGetSystem failed"; XR_LOGE("DDDVR/OpenXRSession","xrGetSystem=%d",r); return false; }

    uint32_t viewCount=0; xrEnumerateViewConfigurationViews(instance_, systemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
    std::vector<XrViewConfigurationView> viewCfg(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    xrEnumerateViewConfigurationViews(instance_, systemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, viewCfg.data());
    if (!viewCfg.empty()) { recWidth_ = viewCfg[0].recommendedImageRectWidth; recHeight_ = viewCfg[0].recommendedImageRectHeight; }

    if (!initEgl()) { lastError_ = "EGL init failed"; return false; }
    runtimeAvailable_ = true;
    return true;
#else
    lastError_ = "OpenXR unavailable";
    return false;
#endif
}

bool OpenXrSession::createSession(){
#if HAS_OPENXR
    XrGraphicsBindingOpenGLESAndroidKHR glBinding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    glBinding.display = eglDisplay_;
    glBinding.config = eglConfig_;
    glBinding.context = eglContext_;
    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO}; sci.next=&glBinding; sci.systemId=systemId_;
    XrResult r = xrCreateSession(instance_, &sci, &session_); if (r != XR_SUCCESS) { lastError_="xrCreateSession failed"; XR_LOGE("DDDVR/OpenXRSession","xrCreateSession=%d",r); return false; }
    return true;
#else
    return false;
#endif
}

bool OpenXrSession::createReferenceSpace(){
#if HAS_OPENXR
    XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO}; rs.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL; rs.poseInReferenceSpace.orientation.w=1.f;
    XrResult r = xrCreateReferenceSpace(session_, &rs, &appSpace_); if (r != XR_SUCCESS) { lastError_="xrCreateReferenceSpace failed"; return false; }
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
        }
        ev = {XR_TYPE_EVENT_DATA_BUFFER};
    }
#endif
}
