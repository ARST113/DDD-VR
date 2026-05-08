#include <jni.h>
#include <memory>
#include "../openxr/OpenXrApp.h"
#include "../openxr/OpenXrLoader.h"
#include "../util/XrLog.h"

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    dddvr::openxr::setJavaVm(vm);
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jlong JNICALL
Java_top_rootu_dddvr_xr_bridge_OpenXrBridge_nativeCreate(JNIEnv* env, jobject, jobject context, jobject) {
    if (!dddvr::openxr::setApplicationContext(env, context)) {
        XR_LOGE("DDDVR/OpenXRLoader", "Failed to set application context");
    }
    auto* app = new OpenXrApp();
    if (!app->initialize()) {
        XR_LOGE("DDDVR/OpenXR", "OpenXR runtime unavailable reason=%s", app->lastError().c_str());
    }
    return reinterpret_cast<jlong>(app);
}

extern "C" JNIEXPORT void JNICALL
Java_top_rootu_dddvr_xr_bridge_OpenXrBridge_nativeStart(JNIEnv*, jobject, jlong handle) {
    auto* app = reinterpret_cast<OpenXrApp*>(handle);
    if (!app) return;
    if (!app->start()) XR_LOGE("DDDVR/OpenXR", "failed to start app");
}
extern "C" JNIEXPORT void JNICALL
Java_top_rootu_dddvr_xr_bridge_OpenXrBridge_nativeResume(JNIEnv*, jobject, jlong handle) { auto* app = reinterpret_cast<OpenXrApp*>(handle); if (app) app->resume(); }
extern "C" JNIEXPORT void JNICALL
Java_top_rootu_dddvr_xr_bridge_OpenXrBridge_nativePause(JNIEnv*, jobject, jlong handle) { auto* app = reinterpret_cast<OpenXrApp*>(handle); if (app) app->pause(); }
extern "C" JNIEXPORT void JNICALL
Java_top_rootu_dddvr_xr_bridge_OpenXrBridge_nativeDestroy(JNIEnv*, jobject, jlong handle) { auto* app = reinterpret_cast<OpenXrApp*>(handle); if (!app) return; app->destroy(); delete app; }
