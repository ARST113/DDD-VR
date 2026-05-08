#include <jni.h>
#include <memory>
#include "../openxr/OpenXrApp.h"
#include "../openxr/OpenXrLoader.h"
#include "../util/XrLog.h"

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    dddvr::openxr::setJavaVm(vm);
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jlong JNICALL
Java_top_rootu_dddvr_xr_bridge_OpenXrBridge_nativeCreate(JNIEnv* env, jobject, jobject activity, jobject appContext, jobject) {
    JavaVM* vm = nullptr;
    if (env != nullptr && env->GetJavaVM(&vm) == JNI_OK && vm != nullptr) {
        dddvr::openxr::setJavaVm(vm);
    } else {
        XR_LOGE("DDDVR/OpenXRLoader", "nativeCreate failed to get JavaVM");
    }

    if (!dddvr::openxr::setApplicationContext(env, appContext)) {
        XR_LOGE("DDDVR/OpenXRLoader", "nativeCreate failed to set application context");
    }
    if (!dddvr::openxr::setApplicationActivity(env, activity)) {
        XR_LOGE("DDDVR/OpenXRLoader", "nativeCreate failed to set application activity");
    }

    auto* app = new OpenXrApp();
    if (!app->initialize()) {
        XR_LOGE("DDDVR/OpenXR", "failed to initialize app reason=%s", app->lastError().c_str());
        delete app;
        return 0;
    }
    return reinterpret_cast<jlong>(app);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_top_rootu_dddvr_xr_bridge_OpenXrBridge_nativeStart(JNIEnv*, jobject, jlong handle) {
    auto* app = reinterpret_cast<OpenXrApp*>(handle);
    if (!app) return JNI_FALSE;
    if (!app->start()) {
        XR_LOGE("DDDVR/OpenXR", "failed to start app");
        return JNI_FALSE;
    }
    return JNI_TRUE;
}
extern "C" JNIEXPORT void JNICALL
Java_top_rootu_dddvr_xr_bridge_OpenXrBridge_nativeResume(JNIEnv*, jobject, jlong handle) { auto* app = reinterpret_cast<OpenXrApp*>(handle); if (app) app->resume(); }
extern "C" JNIEXPORT void JNICALL
Java_top_rootu_dddvr_xr_bridge_OpenXrBridge_nativePause(JNIEnv*, jobject, jlong handle) { auto* app = reinterpret_cast<OpenXrApp*>(handle); if (app) app->pause(); }
extern "C" JNIEXPORT void JNICALL
Java_top_rootu_dddvr_xr_bridge_OpenXrBridge_nativeDestroy(JNIEnv*, jobject, jlong handle) { auto* app = reinterpret_cast<OpenXrApp*>(handle); if (!app) return; app->destroy(); delete app; }
