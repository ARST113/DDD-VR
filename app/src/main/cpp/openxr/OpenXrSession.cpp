#include "OpenXrSession.h"
#include "OpenXrLoader.h"
#include "../util/XrLog.h"
#include <cstring>
#include <vector>

namespace {
const char* sessionStateToString(XrSessionState state){
 switch(state){case XR_SESSION_STATE_UNKNOWN:return "XR_SESSION_STATE_UNKNOWN";case XR_SESSION_STATE_IDLE:return "XR_SESSION_STATE_IDLE";case XR_SESSION_STATE_READY:return "XR_SESSION_STATE_READY";case XR_SESSION_STATE_SYNCHRONIZED:return "XR_SESSION_STATE_SYNCHRONIZED";case XR_SESSION_STATE_VISIBLE:return "XR_SESSION_STATE_VISIBLE";case XR_SESSION_STATE_FOCUSED:return "XR_SESSION_STATE_FOCUSED";case XR_SESSION_STATE_STOPPING:return "XR_SESSION_STATE_STOPPING";case XR_SESSION_STATE_LOSS_PENDING:return "XR_SESSION_STATE_LOSS_PENDING";case XR_SESSION_STATE_EXITING:return "XR_SESSION_STATE_EXITING";default:return "XR_SESSION_STATE_UNKNOWN";}
}
}

bool OpenXrSession::hasExtension(const char* name) { uint32_t count = 0; xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr); std::vector<XrExtensionProperties> exts(count, {XR_TYPE_EXTENSION_PROPERTIES}); xrEnumerateInstanceExtensionProperties(nullptr, count, &count, exts.data()); for (auto& e : exts) if (strcmp(e.extensionName, name) == 0) return true; return false; }

bool OpenXrSession::initializeLoaderAndInstance() {
    XR_LOGI("DDDVR/OpenXRSession", "OpenXrSession::initialize enter");
    const XrResult loaderResult = dddvr::openxr::initializeLoader();
    XR_LOGI("DDDVR/OpenXRSession", "OpenXrSession::initializeLoader result=%s code=%d", loaderResult == XR_SUCCESS ? "true" : "false", loaderResult);
    XR_LOGI("DDDVR/OpenXRSession", "xrInitializeLoaderKHR result=%d", loaderResult);
    if (loaderResult != XR_SUCCESS) { XR_LOGE("DDDVR/OpenXRCheck", "LOADER_FAIL"); lastError_ = "xrInitializeLoaderKHR failed: " + std::to_string(loaderResult); return false; }
    XR_LOGI("DDDVR/OpenXRCheck", "LOADER_OK");
    if (!hasExtension(XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME) || !hasExtension(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME)) { lastError_ = "required extension missing"; return false; }
    std::vector<const char*> enabled{XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME};
    XrInstanceCreateInfoAndroidKHR ai{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR}; ai.applicationVM = dddvr::openxr::javaVm(); ai.applicationActivity = dddvr::openxr::applicationActivity();
    XR_LOGI("DDDVR/OpenXRSession", "androidCreateInfo vm=%p activity=%p", ai.applicationVM, ai.applicationActivity);
    if (!ai.applicationVM || !ai.applicationActivity) { lastError_ = "Android create instance info missing VM or Activity"; return false; }
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO}; ci.next = &ai; std::strcpy(ci.applicationInfo.applicationName, "DDD-VR"); ci.applicationInfo.apiVersion = XR_CURRENT_API_VERSION; ci.enabledExtensionCount = static_cast<uint32_t>(enabled.size()); ci.enabledExtensionNames = enabled.data();
    XrResult r = xrCreateInstance(&ci, &instance_); XR_LOGI("DDDVR/OpenXRSession", "xrCreateInstance result=%d", r); if (r != XR_SUCCESS) { XR_LOGE("DDDVR/OpenXRCheck", "INSTANCE_FAIL"); lastError_ = "xrCreateInstance failed: " + std::to_string(r); return false; } XR_LOGI("DDDVR/OpenXRCheck", "INSTANCE_OK");
    XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO}; sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY; r = xrGetSystem(instance_, &sgi, &systemId_); XR_LOGI("DDDVR/OpenXRSession", "xrGetSystem result=%d", r); if (r != XR_SUCCESS) { XR_LOGE("DDDVR/OpenXRCheck", "SYSTEM_FAIL"); lastError_ = "xrGetSystem failed: " + std::to_string(r); return false; } XR_LOGI("DDDVR/OpenXRCheck", "SYSTEM_OK");
    return true;
}

bool OpenXrSession::prepareGraphics() {
    PFN_xrGetOpenGLESGraphicsRequirementsKHR fn = nullptr;
    XrResult r = xrGetInstanceProcAddr(instance_, "xrGetOpenGLESGraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction*>(&fn));
    if (r != XR_SUCCESS || !fn) { lastError_ = "GraphicsRequirements proc missing"; return false; }
    XrGraphicsRequirementsOpenGLESKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    r = fn(instance_, systemId_, &req);
    XR_LOGI("DDDVR/OpenXRSession", "xrGetOpenGLESGraphicsRequirementsKHR result=%d min=%llu max=%llu", r, (unsigned long long)req.minApiVersionSupported, (unsigned long long)req.maxApiVersionSupported);
    if (r != XR_SUCCESS) { XR_LOGE("DDDVR/OpenXRCheck", "GL_REQUIREMENTS_FAIL"); lastError_ = "GraphicsRequirements failed: " + std::to_string(r); return false; }
    XR_LOGI("DDDVR/OpenXRCheck", "GL_REQUIREMENTS_OK");

    eglDisplay_ = eglGetDisplay(EGL_DEFAULT_DISPLAY); if (eglDisplay_ == EGL_NO_DISPLAY) { lastError_ = "eglGetDisplay failed"; return false; }
    EGLint major = 0, minor = 0; if (!eglInitialize(eglDisplay_, &major, &minor)) { lastError_ = "eglInitialize failed"; return false; }
    const EGLint attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLint num = 0; if (!eglChooseConfig(eglDisplay_, attrs, &eglConfig_, 1, &num) || num < 1) { lastError_ = "eglChooseConfig failed"; return false; }
    const EGLint pbuf[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE}; eglSurface_ = eglCreatePbufferSurface(eglDisplay_, eglConfig_, pbuf);
    const EGLint ctx[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE}; eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, ctx);
    if (eglContext_ == EGL_NO_CONTEXT || eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_) != EGL_TRUE) { lastError_ = "eglMakeCurrent failed"; return false; }
    XR_LOGI("DDDVR/OpenXRSession", "EGL context current on render thread");
    runtimeAvailable_ = true;
    return true;
}

bool OpenXrSession::createSession() { XrGraphicsBindingOpenGLESAndroidKHR gl{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR}; gl.display = eglDisplay_; gl.config = eglConfig_; gl.context = eglContext_; XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO}; sci.next = &gl; sci.systemId = systemId_; XrResult r = xrCreateSession(instance_, &sci, &session_); XR_LOGI("DDDVR/OpenXRSession", "xrCreateSession result=%d", r); if (r != XR_SUCCESS) { XR_LOGE("DDDVR/OpenXRCheck", "SESSION_FAIL"); lastError_ = "xrCreateSession failed: " + std::to_string(r); return false; } XR_LOGI("DDDVR/OpenXRCheck", "SESSION_OK"); return true; }
bool OpenXrSession::createReferenceSpace() { XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO}; rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL; rs.poseInReferenceSpace.orientation.w = 1.f; XrResult r = xrCreateReferenceSpace(session_, &rs, &appSpace_); XR_LOGI("DDDVR/OpenXRSession", "xrCreateReferenceSpace result=%d", r); if (r != XR_SUCCESS) { XR_LOGE("DDDVR/OpenXRCheck", "REFERENCE_SPACE_FAIL"); lastError_ = "xrCreateReferenceSpace failed: " + std::to_string(r); return false; } XR_LOGI("DDDVR/OpenXRCheck", "REFERENCE_SPACE_OK"); return true; }
bool OpenXrSession::begin() { XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO}; bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO; XrResult r = xrBeginSession(session_, &bi); XR_LOGI("DDDVR/OpenXRSession", "xrBeginSession result=%d", r); return r == XR_SUCCESS; }
bool OpenXrSession::end() { XrResult r = xrEndSession(session_); XR_LOGI("DDDVR/OpenXRSession", "xrEndSession result=%d", r); return r == XR_SUCCESS; }
void OpenXrSession::pollEvents() {
    if (instance_ == XR_NULL_HANDLE) return;
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(instance_, &ev) == XR_SUCCESS) {
        XR_LOGI("DDDVR/OpenXRSession", "xrPollEvent: type=%d", ev.type);
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto* changed = reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
            state_ = changed->state;
            XR_LOGI("DDDVR/OpenXRSession", "XrEventDataSessionStateChanged state=%s", sessionStateToString(state_));
        }
        ev = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

void OpenXrSession::shutdown() {
    if (appSpace_ != XR_NULL_HANDLE) { xrDestroySpace(appSpace_); appSpace_ = XR_NULL_HANDLE; }
    if (session_ != XR_NULL_HANDLE) { xrDestroySession(session_); session_ = XR_NULL_HANDLE; }
    if (instance_ != XR_NULL_HANDLE) { xrDestroyInstance(instance_); instance_ = XR_NULL_HANDLE; }
    if (eglDisplay_ != EGL_NO_DISPLAY) { eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); if (eglSurface_ != EGL_NO_SURFACE) eglDestroySurface(eglDisplay_, eglSurface_); if (eglContext_ != EGL_NO_CONTEXT) eglDestroyContext(eglDisplay_, eglContext_); eglTerminate(eglDisplay_); }
}
