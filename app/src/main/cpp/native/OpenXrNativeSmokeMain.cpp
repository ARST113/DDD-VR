#include <jni.h>
#include <android_native_app_glue.h>

#include "../openxr/OpenXrApp.h"
#include "../openxr/OpenXrLoader.h"
#include "../util/XrLog.h"

void android_main(android_app* app) {
    app_dummy();
    XR_LOGI("DDDVR/OpenXRNativeSmoke", "NATIVE_SMOKE_ANDROID_MAIN_BEGIN app=%p", app);
    if (!app || !app->activity) {
        XR_LOGE("DDDVR/OpenXRNativeSmoke", "NATIVE_SMOKE_NO_ACTIVITY");
        return;
    }

    JavaVM* vm = app->activity->vm;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (vm && vm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
        attached = true;
        dddvr::openxr::setJavaVm(vm);
        if (!dddvr::openxr::setApplicationActivity(env, app->activity->clazz)) {
            XR_LOGE("DDDVR/OpenXRNativeSmoke", "NATIVE_SMOKE_SET_ACTIVITY_FAILED");
        }
        if (!dddvr::openxr::setApplicationContext(env, app->activity->clazz)) {
            XR_LOGE("DDDVR/OpenXRNativeSmoke", "NATIVE_SMOKE_SET_CONTEXT_FAILED");
        }
    } else {
        XR_LOGE("DDDVR/OpenXRNativeSmoke", "NATIVE_SMOKE_ATTACH_THREAD_FAILED");
    }

    OpenXrApp xrApp;
    XR_LOGI("DDDVR/OpenXRNativeSmoke", "NATIVE_SMOKE_INITIALIZE_BEGIN");
    const bool initOk = xrApp.initialize();
    XR_LOGI("DDDVR/OpenXRNativeSmoke", "NATIVE_SMOKE_INITIALIZE_END ok=%d", initOk ? 1 : 0);
    if (!initOk) {
        XR_LOGE("DDDVR/OpenXRNativeSmoke", "CURRENT_BLOCKER NATIVE_SMOKE_INITIALIZE_FAILED");
        if (attached) vm->DetachCurrentThread();
        return;
    }

    XR_LOGI("DDDVR/OpenXRNativeSmoke", "NATIVE_SMOKE_START_BEGIN");
    const bool startOk = xrApp.start();
    XR_LOGI("DDDVR/OpenXRNativeSmoke", "NATIVE_SMOKE_START_END ok=%d", startOk ? 1 : 0);
    if (!startOk) {
        XR_LOGE("DDDVR/OpenXRNativeSmoke", "CURRENT_BLOCKER NATIVE_SMOKE_START_FAILED");
        xrApp.destroy();
        if (attached) vm->DetachCurrentThread();
        return;
    }

    bool running = true;
    while (running) {
        int events = 0;
        android_poll_source* source = nullptr;
        ALooper_pollOnce(0, nullptr, &events, reinterpret_cast<void**>(&source));
        if (source) source->process(app, source);
        if (app->destroyRequested) {
            XR_LOGI("DDDVR/OpenXRNativeSmoke", "NATIVE_SMOKE_DESTROY_REQUESTED");
            running = false;
        }
    }

    XR_LOGI("DDDVR/OpenXRNativeSmoke", "NATIVE_SMOKE_DESTROY_BEGIN");
    xrApp.destroy();
    XR_LOGI("DDDVR/OpenXRNativeSmoke", "NATIVE_SMOKE_DESTROY_END");
    if (attached) vm->DetachCurrentThread();
}
