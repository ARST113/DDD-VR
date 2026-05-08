#include "OpenXrLoader.h"

#include "../util/XrLog.h"

namespace {
JavaVM* g_javaVm = nullptr;
jobject g_applicationContext = nullptr;
}

namespace dddvr::openxr {

void setJavaVm(JavaVM* javaVm) {
    g_javaVm = javaVm;
    XR_LOGI("DDDVR/OpenXRLoader", "JNI_OnLoad captured JavaVM vm=%p", static_cast<void*>(g_javaVm));
}

bool setApplicationContext(JNIEnv* env, jobject context) {
    if (env == nullptr || context == nullptr) {
        XR_LOGE("DDDVR/OpenXRLoader", "setApplicationContext invalid env/context");
        return false;
    }
    if (g_applicationContext != nullptr) {
        env->DeleteGlobalRef(g_applicationContext);
        g_applicationContext = nullptr;
    }
    g_applicationContext = env->NewGlobalRef(context);
    return g_applicationContext != nullptr;
}

bool initializeLoader() {
    if (g_javaVm == nullptr || g_applicationContext == nullptr) {
        XR_LOGE("DDDVR/OpenXRLoader", "initializeLoader missing vm/context vm=%p ctx=%p", static_cast<void*>(g_javaVm), g_applicationContext);
        return false;
    }

    PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR = nullptr;
    const XrResult getProcResult = xrGetInstanceProcAddr(
        XR_NULL_HANDLE,
        "xrInitializeLoaderKHR",
        reinterpret_cast<PFN_xrVoidFunction*>(&xrInitializeLoaderKHR));
    XR_LOGI("DDDVR/OpenXRLoader", "xrGetInstanceProcAddr(xrInitializeLoaderKHR)=%d fn=%p", getProcResult, reinterpret_cast<void*>(xrInitializeLoaderKHR));
    if (getProcResult != XR_SUCCESS || xrInitializeLoaderKHR == nullptr) {
        return false;
    }

    XrLoaderInitInfoAndroidKHR initInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    initInfo.applicationVM = g_javaVm;
    initInfo.applicationContext = g_applicationContext;
    const XrResult initResult = xrInitializeLoaderKHR(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&initInfo));
    XR_LOGI("DDDVR/OpenXRLoader", "xrInitializeLoaderKHR result=%d", initResult);
    return initResult == XR_SUCCESS;
}

} // namespace dddvr::openxr
