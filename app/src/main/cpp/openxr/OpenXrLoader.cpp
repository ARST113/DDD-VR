#include "OpenXrLoader.h"

#include "../util/XrLog.h"

namespace dddvr::openxr {

static JavaVM* g_javaVm = nullptr;
static jobject g_applicationContext = nullptr;
static const char* g_loaderStatus = "not_initialized";
static jobject g_applicationActivity = nullptr;

void setJavaVm(JavaVM* vm) {
    g_javaVm = vm;
    XR_LOGI("DDDVR/OpenXRLoader", "setJavaVm vm=%p", vm);
}

bool setApplicationContext(JNIEnv* env, jobject context) {
    if (env == nullptr || context == nullptr) {
        g_loaderStatus = "invalid_env_or_context";
        XR_LOGE("DDDVR/OpenXRLoader", "setApplicationContext failed env=%p ctx=%p", env, context);
        return false;
    }
    if (g_applicationContext != nullptr) {
        env->DeleteGlobalRef(g_applicationContext);
        g_applicationContext = nullptr;
    }
    g_applicationContext = env->NewGlobalRef(context);
    if (g_applicationContext == nullptr) {
        g_loaderStatus = "new_global_ref_failed";
        XR_LOGE("DDDVR/OpenXRLoader", "setApplicationContext failed to create global ref");
        return false;
    }
    g_loaderStatus = "application_context_set";
    XR_LOGI("DDDVR/OpenXRLoader", "setApplicationContext success ctx=%p", g_applicationContext);
    return true;
}

bool hasJavaVm() { return g_javaVm != nullptr; }
bool hasApplicationContext() { return g_applicationContext != nullptr; }

bool setApplicationActivity(JNIEnv* env, jobject activity) {
    if (env == nullptr || activity == nullptr) {
        XR_LOGE("DDDVR/OpenXRLoader", "setApplicationActivity failed env=%p activity=%p", env, activity);
        return false;
    }
    if (g_applicationActivity != nullptr) {
        env->DeleteGlobalRef(g_applicationActivity);
        g_applicationActivity = nullptr;
    }
    g_applicationActivity = env->NewGlobalRef(activity);
    XR_LOGI("DDDVR/OpenXRLoader", "setApplicationActivity success activity=%p", g_applicationActivity);
    return g_applicationActivity != nullptr;
}

bool hasApplicationActivity() { return g_applicationActivity != nullptr; }
JavaVM* javaVm() { return g_javaVm; }
jobject applicationActivity() { return g_applicationActivity; }

XrResult initializeLoader() {
    XR_LOGI("DDDVR/OpenXRLoader", "initializeLoader enter vm=%p ctx=%p", g_javaVm, g_applicationContext);
    if (g_javaVm == nullptr) {
        g_loaderStatus = "missing_java_vm";
        XR_LOGE("DDDVR/OpenXRLoader", "initializeLoader missing JavaVM");
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    if (g_applicationContext == nullptr) {
        g_loaderStatus = "missing_application_context";
        XR_LOGE("DDDVR/OpenXRLoader", "initializeLoader missing applicationContext");
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    PFN_xrInitializeLoaderKHR initializeLoaderFn = nullptr;
    XrResult getProcResult = xrGetInstanceProcAddr(
        XR_NULL_HANDLE,
        "xrInitializeLoaderKHR",
        reinterpret_cast<PFN_xrVoidFunction*>(&initializeLoaderFn)
    );
    XR_LOGI("DDDVR/OpenXRLoader", "xrGetInstanceProcAddr(xrInitializeLoaderKHR)=%d fn=%p", getProcResult, initializeLoaderFn);
    if (getProcResult != XR_SUCCESS || initializeLoaderFn == nullptr) {
        g_loaderStatus = "loader_function_unavailable";
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    XrLoaderInitInfoAndroidKHR initInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    initInfo.applicationVM = g_javaVm;
    initInfo.applicationContext = g_applicationContext;

    const XrResult initResult = initializeLoaderFn(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&initInfo));
    XR_LOGI("DDDVR/OpenXRLoader", "xrInitializeLoaderKHR result=%d", initResult);
    g_loaderStatus = initResult == XR_SUCCESS ? "loader_initialized" : "loader_init_failed";
    return initResult;
}

const char* loaderInitStatus() { return g_loaderStatus; }

} // namespace dddvr::openxr
