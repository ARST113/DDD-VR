#include <jni.h>
#include <memory>
#include "../openxr/OpenXrApp.h"
#include "../util/XrLog.h"

extern "C" JNIEXPORT jlong JNICALL
Java_top_rootu_dddvr_xr_bridge_OpenXrBridge_nativeCreate(JNIEnv*, jobject, jobject, jobject) {
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
