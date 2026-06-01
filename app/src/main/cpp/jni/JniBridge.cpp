#include <jni.h>
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
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

jmethodID findMethod(JNIEnv* env, jclass clazz, const char* name, const char* signature) {
    if (env == nullptr || clazz == nullptr) return nullptr;
    jmethodID method = env->GetMethodID(clazz, name, signature);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return method;
}

bool getBoolean(JNIEnv* env, jobject object, const char* getter, bool fallback = false) {
    if (env == nullptr || object == nullptr) return fallback;
    jclass clazz = env->GetObjectClass(object);
    jmethodID method = findMethod(env, clazz, getter, "()Z");
    const bool value = method != nullptr ? env->CallBooleanMethod(object, method) == JNI_TRUE : fallback;
    if (clazz != nullptr) env->DeleteLocalRef(clazz);
    return value;
}

int32_t getInt(JNIEnv* env, jobject object, const char* getter, int32_t fallback = 0) {
    if (env == nullptr || object == nullptr) return fallback;
    jclass clazz = env->GetObjectClass(object);
    jmethodID method = findMethod(env, clazz, getter, "()I");
    const int32_t value = method != nullptr ? static_cast<int32_t>(env->CallIntMethod(object, method)) : fallback;
    if (clazz != nullptr) env->DeleteLocalRef(clazz);
    return value;
}

int64_t getLong(JNIEnv* env, jobject object, const char* getter, int64_t fallback = 0) {
    if (env == nullptr || object == nullptr) return fallback;
    jclass clazz = env->GetObjectClass(object);
    jmethodID method = findMethod(env, clazz, getter, "()J");
    const int64_t value = method != nullptr ? static_cast<int64_t>(env->CallLongMethod(object, method)) : fallback;
    if (clazz != nullptr) env->DeleteLocalRef(clazz);
    return value;
}

float getFloat(JNIEnv* env, jobject object, const char* getter, float fallback = 0.f) {
    if (env == nullptr || object == nullptr) return fallback;
    jclass clazz = env->GetObjectClass(object);
    jmethodID method = findMethod(env, clazz, getter, "()F");
    const float value = method != nullptr ? static_cast<float>(env->CallFloatMethod(object, method)) : fallback;
    if (clazz != nullptr) env->DeleteLocalRef(clazz);
    return value;
}

std::string getString(JNIEnv* env, jobject object, const char* getter) {
    if (env == nullptr || object == nullptr) return {};
    jclass clazz = env->GetObjectClass(object);
    jmethodID method = findMethod(env, clazz, getter, "()Ljava/lang/String;");
    jstring value = method != nullptr ? reinterpret_cast<jstring>(env->CallObjectMethod(object, method)) : nullptr;
    std::string out = toString(env, value);
    if (value != nullptr) env->DeleteLocalRef(value);
    if (clazz != nullptr) env->DeleteLocalRef(clazz);
    return out;
}

jobject getObject(JNIEnv* env, jobject object, const char* getter, const char* signature) {
    if (env == nullptr || object == nullptr) return nullptr;
    jclass clazz = env->GetObjectClass(object);
    jmethodID method = findMethod(env, clazz, getter, signature);
    jobject value = method != nullptr ? env->CallObjectMethod(object, method) : nullptr;
    if (clazz != nullptr) env->DeleteLocalRef(clazz);
    return value;
}

VrPlayerModal modalFromInt(int value) {
    switch (value) {
        case 1: return VrPlayerModal::Playlist;
        case 2: return VrPlayerModal::Settings;
        default: return VrPlayerModal::None;
    }
}

VrSettingsTab settingsTabFromInt(int value) {
    switch (value) {
        case 1: return VrSettingsTab::Subtitles;
        case 2: return VrSettingsTab::Audio;
        case 0:
        default:
            return VrSettingsTab::Display;
    }
}

std::vector<VrTrackRow> parseTrackRows(JNIEnv* env, jobject list) {
    std::vector<VrTrackRow> rows;
    if (env == nullptr || list == nullptr) return rows;
    jclass listClass = env->FindClass("java/util/List");
    jmethodID sizeMethod = findMethod(env, listClass, "size", "()I");
    jmethodID getMethod = findMethod(env, listClass, "get", "(I)Ljava/lang/Object;");
    if (sizeMethod == nullptr || getMethod == nullptr) {
        if (listClass != nullptr) env->DeleteLocalRef(listClass);
        return rows;
    }
    const jint count = env->CallIntMethod(list, sizeMethod);
    rows.reserve(static_cast<size_t>(std::max(0, static_cast<int>(count))));
    for (jint i = 0; i < count; ++i) {
        jobject rowObject = env->CallObjectMethod(list, getMethod, i);
        if (rowObject == nullptr) continue;
        VrTrackRow row{};
        row.id = getString(env, rowObject, "getId");
        row.title = getString(env, rowObject, "getTitle");
        row.subtitle = getString(env, rowObject, "getSubtitle");
        row.selected = getBoolean(env, rowObject, "getSelected");
        row.enabled = getBoolean(env, rowObject, "getEnabled", true);
        rows.push_back(std::move(row));
        env->DeleteLocalRef(rowObject);
    }
    if (listClass != nullptr) env->DeleteLocalRef(listClass);
    return rows;
}

std::vector<VrPlaylistRow> parsePlaylistRows(JNIEnv* env, jobject list) {
    std::vector<VrPlaylistRow> rows;
    if (env == nullptr || list == nullptr) return rows;
    jclass listClass = env->FindClass("java/util/List");
    jmethodID sizeMethod = findMethod(env, listClass, "size", "()I");
    jmethodID getMethod = findMethod(env, listClass, "get", "(I)Ljava/lang/Object;");
    if (sizeMethod == nullptr || getMethod == nullptr) {
        if (listClass != nullptr) env->DeleteLocalRef(listClass);
        return rows;
    }
    const jint count = env->CallIntMethod(list, sizeMethod);
    rows.reserve(static_cast<size_t>(std::max(0, static_cast<int>(count))));
    for (jint i = 0; i < count; ++i) {
        jobject rowObject = env->CallObjectMethod(list, getMethod, i);
        if (rowObject == nullptr) continue;
        VrPlaylistRow row{};
        row.id = getString(env, rowObject, "getId");
        row.title = getString(env, rowObject, "getTitle");
        row.subtitle = getString(env, rowObject, "getSubtitle");
        row.selected = getBoolean(env, rowObject, "getSelected");
        rows.push_back(std::move(row));
        env->DeleteLocalRef(rowObject);
    }
    if (listClass != nullptr) env->DeleteLocalRef(listClass);
    return rows;
}

VrPlayerUiState parsePlayerUiState(JNIEnv* env, jobject state) {
    VrPlayerUiState out{};
    if (env == nullptr || state == nullptr) return out;
    out.visible = getBoolean(env, state, "getVisible", true);
    out.pinned = getBoolean(env, state, "getPinned");
    out.playing = getBoolean(env, state, "getPlaying");
    out.buffering = getBoolean(env, state, "getBuffering");
    out.muted = getBoolean(env, state, "getMuted");
    out.positionMs = getLong(env, state, "getPositionMs");
    out.durationMs = getLong(env, state, "getDurationMs");
    out.bufferedPositionMs = getLong(env, state, "getBufferedPositionMs");
    out.title = getString(env, state, "getTitle");
    out.projectionModeLabel = getString(env, state, "getProjectionModeLabel");
    out.audioTrackLabel = getString(env, state, "getAudioTrackLabel");
    out.subtitleTrackLabel = getString(env, state, "getSubtitleTrackLabel");
    out.activeModal = modalFromInt(getInt(env, state, "getActiveModal"));
    out.activeSettingsTab = settingsTabFromInt(getInt(env, state, "getActiveSettingsTab"));
    out.hoverTarget = static_cast<VrHoverTarget>(getInt(env, state, "getHoverTarget"));

    jobject display = getObject(env, state, "getDisplay", "()Ltop/rootu/dddvr/xr/ui/OpenXrDisplayUiState;");
    if (display != nullptr) {
        out.display.aspectRatio = getString(env, display, "getAspectRatio");
        out.display.playbackSpeed = getFloat(env, display, "getPlaybackSpeed", 1.0f);
        out.display.enhanceVideo = getBoolean(env, display, "getEnhanceVideo");
        out.display.brightness = getFloat(env, display, "getBrightness", 1.0f);
        env->DeleteLocalRef(display);
    }

    jobject subtitles = getObject(env, state, "getSubtitles", "()Ltop/rootu/dddvr/xr/ui/OpenXrSubtitlesUiState;");
    if (subtitles != nullptr) {
        out.subtitles.enabled = getBoolean(env, subtitles, "getEnabled");
        out.subtitles.delayMs = getInt(env, subtitles, "getDelayMs");
        out.subtitles.sizeLabel = getString(env, subtitles, "getSizeLabel");
        out.subtitles.positionLabel = getString(env, subtitles, "getPositionLabel");
        env->DeleteLocalRef(subtitles);
    }

    jobject audio = getObject(env, state, "getAudio", "()Ltop/rootu/dddvr/xr/ui/OpenXrAudioUiState;");
    if (audio != nullptr) {
        out.audio.delayMs = getInt(env, audio, "getDelayMs");
        out.audio.spatialAudio = getBoolean(env, audio, "getSpatialAudio");
        env->DeleteLocalRef(audio);
    }

    jobject playlistRows = getObject(env, state, "getPlaylistRows", "()Ljava/util/List;");
    out.playlistRows = parsePlaylistRows(env, playlistRows);
    if (playlistRows != nullptr) env->DeleteLocalRef(playlistRows);

    jobject audioTracks = getObject(env, state, "getAudioTracks", "()Ljava/util/List;");
    out.audioTracks = parseTrackRows(env, audioTracks);
    if (audioTracks != nullptr) env->DeleteLocalRef(audioTracks);

    jobject subtitleTracks = getObject(env, state, "getSubtitleTracks", "()Ljava/util/List;");
    out.subtitleTracks = parseTrackRows(env, subtitleTracks);
    if (subtitleTracks != nullptr) env->DeleteLocalRef(subtitleTracks);

    return out;
}

OpenXrStereoMode parseStereoMode(const std::string& name) {
    if (name == "SBS") return OpenXrStereoMode::Sbs;
    if (name == "SBS_REVERSED") return OpenXrStereoMode::SbsReversed;
    if (name == "OU") return OpenXrStereoMode::Ou;
    if (name == "OU_REVERSED") return OpenXrStereoMode::OuReversed;
    return OpenXrStereoMode::Mono;
}

OpenXrScreenModeNative parseScreenMode(const std::string& name) {
    if (name == "CURVED") return OpenXrScreenModeNative::Curved;
    if (name == "VR180") return OpenXrScreenModeNative::Vr180;
    if (name == "VR360") return OpenXrScreenModeNative::Vr360;
    return OpenXrScreenModeNative::Flat;
}

OpenXrRenderConfig parseRenderConfig(JNIEnv* env, jobject config) {
    OpenXrRenderConfig out{};
    if (env == nullptr || config == nullptr) return out;
    jclass configClass = env->GetObjectClass(config);
    jmethodID getStereoMode = env->GetMethodID(configClass, "getStereoMode", "()Ltop/rootu/dddvr/vr/stereo/StereoInputMode;");
    jmethodID getScreenMode = env->GetMethodID(configClass, "getScreenMode", "()Ltop/rootu/dddvr/xr/model/OpenXrScreenMode;");
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
    if (getScreenMode != nullptr) {
        jobject screenMode = env->CallObjectMethod(config, getScreenMode);
        jclass enumClass = env->FindClass("java/lang/Enum");
        jmethodID nameMethod = enumClass != nullptr ? env->GetMethodID(enumClass, "name", "()Ljava/lang/String;") : nullptr;
        if (screenMode != nullptr && nameMethod != nullptr) {
            jstring name = reinterpret_cast<jstring>(env->CallObjectMethod(screenMode, nameMethod));
            out.screenMode = parseScreenMode(toString(env, name));
            if (name != nullptr) env->DeleteLocalRef(name);
        }
        if (enumClass != nullptr) env->DeleteLocalRef(enumClass);
        if (screenMode != nullptr) env->DeleteLocalRef(screenMode);
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
Java_top_rootu_dddvr_xr_bridge_OpenXrBridge_nativeSetPlayerUiState(
    JNIEnv* env,
    jobject,
    jlong handle,
    jobject state
) {
    auto* app = reinterpret_cast<OpenXrApp*>(handle);
    if (app == nullptr || state == nullptr) return;
    VrPlayerUiState parsed = parsePlayerUiState(env, state);
    app->setPlayerUiState(parsed);
    if (env != nullptr && env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}
extern "C" JNIEXPORT void JNICALL
Java_top_rootu_dddvr_xr_bridge_OpenXrBridge_nativeDestroy(JNIEnv*, jobject, jlong handle) { XR_LOGI("DDDVR/OpenXR", "nativeDestroy called"); auto* app = reinterpret_cast<OpenXrApp*>(handle); if (!app) return; app->destroy(); delete app; }
