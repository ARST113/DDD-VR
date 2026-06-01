#include <jni.h>
#include <memory>
#include <string>
#include <vector>
#include "../openxr/OpenXrApp.h"
#include "../openxr/OpenXrLoader.h"
#include "../util/XrLog.h"

namespace {
std::string toString(JNIEnv* env, jstring value) {
    if (env == nullptr || value == nullptr) return {};
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) return {};
    std::string out(chars);
    env->ReleaseStringUTFChars(value, chars);
    return out;
}

std::vector<std::string> toStringVector(JNIEnv* env, jobjectArray values) {
    std::vector<std::string> out;
    if (env == nullptr || values == nullptr) return out;
    const jsize count = env->GetArrayLength(values);
    out.reserve(static_cast<size_t>(count));
    for (jsize i = 0; i < count; ++i) {
        auto value = reinterpret_cast<jstring>(env->GetObjectArrayElement(values, i));
        out.push_back(toString(env, value));
        if (value != nullptr) env->DeleteLocalRef(value);
    }
    return out;
}

OpenXrStereoMode parseStereoMode(const std::string& name) {
    if (name == "SBS") return OpenXrStereoMode::Sbs;
    if (name == "SBS_REVERSED") return OpenXrStereoMode::SbsReversed;
    if (name == "OU") return OpenXrStereoMode::Ou;
    if (name == "OU_REVERSED") return OpenXrStereoMode::OuReversed;
    return OpenXrStereoMode::Mono;
}

OpenXrRenderConfig parseRenderConfig(JNIEnv* env, jobject config) {
    OpenXrRenderConfig out{};
    if (env == nullptr || config == nullptr) return out;
    jclass configClass = env->GetObjectClass(config);
    jmethodID getStereoMode = env->GetMethodID(configClass, "getStereoMode", "()Ltop/rootu/dddvr/vr/stereo/StereoInputMode;");
    jmethodID getSwapEyes = env->GetMethodID(configClass, "getSwapEyes", "()Z");
    jmethodID getScreenDistance = env->GetMethodID(configClass, "getScreenDistanceMeters", "()F");
    jmethodID getScreenWidth = env->GetMethodID(configClass, "getScreenWidthMeters", "()F");
    jmethodID getScreenCurve = env->GetMethodID(configClass, "getScreenCurveRadians", "()F");
    if (getStereoMode != nullptr) {
        jobject stereoMode = env->CallObjectMethod(config, getStereoMode);
        jclass enumClass = env->FindClass("java/lang/Enum");
        jmethodID nameMethod = enumClass != nullptr ? env->GetMethodID(enumClass, "name", "()Ljava/lang/String;") : nullptr;
        if (stereoMode != nullptr && nameMethod != nullptr) {
            jstring name = reinterpret_cast<jstring>(env->CallObjectMethod(stereoMode, nameMethod));
            out.stereoMode = parseStereoMode(toString(env, name));
            if (name != nullptr) env->DeleteLocalRef(name);
        }
        if (enumClass != nullptr) env->DeleteLocalRef(enumClass);
        if (stereoMode != nullptr) env->DeleteLocalRef(stereoMode);
    }
    if (getSwapEyes != nullptr) out.swapEyes = env->CallBooleanMethod(config, getSwapEyes) == JNI_TRUE;
    if (getScreenDistance != nullptr) out.screenDistanceMeters = env->CallFloatMethod(config, getScreenDistance);
    if (getScreenWidth != nullptr) out.screenWidthMeters = env->CallFloatMethod(config, getScreenWidth);
    if (getScreenCurve != nullptr) out.screenCurveRadians = env->CallFloatMethod(config, getScreenCurve);
    env->DeleteLocalRef(configClass);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return OpenXrRenderConfig{};
    }
    if (out.screenDistanceMeters <= 0.f) out.screenDistanceMeters = 3.5f;
    if (out.screenWidthMeters <= 0.f) out.screenWidthMeters = 4.5f;
    if (out.screenCurveRadians < 0.f) out.screenCurveRadians = 0.f;
    return out;
}
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    dddvr::openxr::setJavaVm(vm);
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jlong JNICALL
Java_top_rootu_dddvr_xr_bridge_OpenXrBridge_nativeCreate(JNIEnv* env, jobject bridge, jobject activity, jobject appContext, jobject config) {
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

    const OpenXrRenderConfig renderConfig = parseRenderConfig(env, config);
    XR_LOGI("DDDVR/OpenXR", "nativeCreate renderConfig stereo=%d swap=%d width=%.2f distance=%.2f curve=%.2f",
            (int)renderConfig.stereoMode, renderConfig.swapEyes ? 1 : 0,
            renderConfig.screenWidthMeters, renderConfig.screenDistanceMeters, renderConfig.screenCurveRadians);
    auto* app = new OpenXrApp(renderConfig);
    app->setJavaBridge(env, bridge);
    if (!app->initialize()) { delete app; return 0; }
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
Java_top_rootu_dddvr_xr_bridge_OpenXrBridge_nativeResume(JNIEnv*, jobject, jlong handle) { XR_LOGI("DDDVR/OpenXR", "nativeResume called"); auto* app = reinterpret_cast<OpenXrApp*>(handle); if (app) app->resume(); }
extern "C" JNIEXPORT void JNICALL
Java_top_rootu_dddvr_xr_bridge_OpenXrBridge_nativePause(JNIEnv*, jobject, jlong handle) { XR_LOGI("DDDVR/OpenXR", "nativePause called"); auto* app = reinterpret_cast<OpenXrApp*>(handle); if (app) app->pause(); }
extern "C" JNIEXPORT void JNICALL
Java_top_rootu_dddvr_xr_bridge_OpenXrBridge_nativeSetVideoSize(JNIEnv*, jobject, jlong handle, jint width, jint height) { auto* app = reinterpret_cast<OpenXrApp*>(handle); if (app) app->setVideoSize(width, height); }
extern "C" JNIEXPORT void JNICALL
Java_top_rootu_dddvr_xr_bridge_OpenXrBridge_nativeSetUiState(
    JNIEnv* env,
    jobject,
    jlong handle,
    jboolean visible,
    jboolean playing,
    jboolean buffering,
    jlong positionMs,
    jlong durationMs,
    jlong bufferedPositionMs,
    jstring title,
    jstring stereoModeLabel,
    jstring audioTrackLabel,
    jobjectArray audioTrackLabels,
    jint selectedAudioTrackIndex
) {
    auto* app = reinterpret_cast<OpenXrApp*>(handle);
    if (app) {
        app->setUiState(
            visible == JNI_TRUE,
            playing == JNI_TRUE,
            buffering == JNI_TRUE,
            static_cast<int64_t>(positionMs),
            static_cast<int64_t>(durationMs),
            static_cast<int64_t>(bufferedPositionMs),
            toString(env, title),
            toString(env, stereoModeLabel),
            toString(env, audioTrackLabel),
            toStringVector(env, audioTrackLabels),
            static_cast<int>(selectedAudioTrackIndex)
        );
    }
}
extern "C" JNIEXPORT void JNICALL
Java_top_rootu_dddvr_xr_bridge_OpenXrBridge_nativeDestroy(JNIEnv*, jobject, jlong handle) { XR_LOGI("DDDVR/OpenXR", "nativeDestroy called"); auto* app = reinterpret_cast<OpenXrApp*>(handle); if (!app) return; app->destroy(); delete app; }
