#include "OpenXrApp.h"
#include "OpenXrLoader.h"
#include "../util/XrLog.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
#if defined(DDDVR_LEGACY_PRIMITIVE_UI)
int hoverPriority(CinemaUiHoverTarget target) {
    switch (target) {
        case CinemaUiHoverTarget::Progress:
            return 4;
        case CinemaUiHoverTarget::PlayPause:
            return 3;
        case CinemaUiHoverTarget::Panel:
            return 2;
        case CinemaUiHoverTarget::Video:
            return 1;
        case CinemaUiHoverTarget::None:
        default:
            return 0;
    }
}
#endif

constexpr auto kFfmpegVideoMinUploadInterval = std::chrono::milliseconds(32);
constexpr GLenum kFramebufferSrgbExt = 0x8DB9;
}

OpenXrApp::~OpenXrApp() {
    releaseJavaRefs();
}

void OpenXrApp::setJavaBridge(JNIEnv* env, jobject bridge) {
    if (env == nullptr || bridge == nullptr) {
        XR_LOGW("DDDVR/OpenXRVideo", "setJavaBridge skipped env=%p bridge=%p", env, bridge);
        return;
    }
    releaseJavaRefs();

    javaBridgeRef_ = env->NewGlobalRef(bridge);
    jclass bridgeClass = env->GetObjectClass(bridge);
    bridgeOnVideoSurfaceReady_ = env->GetMethodID(
        bridgeClass,
        "onVideoSurfaceReadyFromNative",
        "(Landroid/view/Surface;)V"
    );
    bridgeOnInputAction_ = env->GetMethodID(
        bridgeClass,
        "onInputActionFromNative",
        "(I)V"
    );
    bridgeOnTimelineSeek_ = env->GetMethodID(
        bridgeClass,
        "onTimelineSeekFromNative",
        "(I)V"
    );
    bridgeOnAudioTrackSelected_ = env->GetMethodID(
        bridgeClass,
        "onAudioTrackSelectedFromNative",
        "(I)V"
    );
    bridgeOnPlayerUiAction_ = env->GetMethodID(
        bridgeClass,
        "onPlayerUiActionFromNative",
        "(IIFLjava/lang/String;)V"
    );
    env->DeleteLocalRef(bridgeClass);

    jclass localVideoSurfaceClass = env->FindClass("top/rootu/dddvr/xr/bridge/OpenXrVideoSurface");
    if (localVideoSurfaceClass != nullptr) {
        videoSurfaceClass_ = reinterpret_cast<jclass>(env->NewGlobalRef(localVideoSurfaceClass));
        env->DeleteLocalRef(localVideoSurfaceClass);
    }
    if (videoSurfaceClass_ != nullptr) {
        videoSurfaceCtor_ = env->GetMethodID(videoSurfaceClass_, "<init>", "(I)V");
        videoSurfaceGetSurface_ = env->GetMethodID(videoSurfaceClass_, "getSurface", "()Landroid/view/Surface;");
        videoSurfaceUpdateTexImage_ = env->GetMethodID(videoSurfaceClass_, "updateTexImage", "([F)Z");
        videoSurfaceTimestampNs_ = env->GetMethodID(videoSurfaceClass_, "timestampNs", "()J");
        videoSurfaceSetDefaultBufferSize_ = env->GetMethodID(videoSurfaceClass_, "setDefaultBufferSize", "(II)V");
        videoSurfaceRelease_ = env->GetMethodID(videoSurfaceClass_, "release", "()V");
    }

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }

    const bool ok = javaBridgeRef_ != nullptr &&
        videoSurfaceClass_ != nullptr &&
        bridgeOnVideoSurfaceReady_ != nullptr &&
        bridgeOnInputAction_ != nullptr &&
        bridgeOnTimelineSeek_ != nullptr &&
        bridgeOnAudioTrackSelected_ != nullptr &&
        bridgeOnPlayerUiAction_ != nullptr &&
        videoSurfaceCtor_ != nullptr &&
        videoSurfaceGetSurface_ != nullptr &&
        videoSurfaceUpdateTexImage_ != nullptr &&
        videoSurfaceTimestampNs_ != nullptr &&
        videoSurfaceSetDefaultBufferSize_ != nullptr &&
        videoSurfaceRelease_ != nullptr;
    XR_LOGI("DDDVR/OpenXRVideo", "java bridge configured ok=%d", ok ? 1 : 0);
}

void OpenXrApp::setVideoSize(int32_t width, int32_t height, float pixelWidthHeightRatio) {
    if (width <= 0 || height <= 0) return;
    pendingVideoWidth_ = width;
    pendingVideoHeight_ = height;
    pendingVideoPixelWidthHeightRatio_ = std::isfinite(pixelWidthHeightRatio) && pixelWidthHeightRatio > 0.f
        ? pixelWidthHeightRatio
        : 1.f;
    XR_LOGI(
        "DDDVR/OpenXRVideo",
        "XR_VIDEO_SIZE_PENDING width=%d height=%d pixelRatio=%.5f",
        width,
        height,
        pendingVideoPixelWidthHeightRatio_.load()
    );
}

void OpenXrApp::setDisplayAspectRatio(float aspectRatio) {
    pendingDisplayAspectRatio_ = std::isfinite(aspectRatio) && aspectRatio > 0.f
        ? aspectRatio
        : 0.f;
    XR_LOGI("DDDVR/OpenXRVideo", "XR_VIDEO_ASPECT_OVERRIDE_PENDING aspect=%.5f", pendingDisplayAspectRatio_.load());
}

void OpenXrApp::setUiState(
    bool visible,
    bool playing,
    bool buffering,
    int64_t positionMs,
    int64_t durationMs,
    int64_t bufferedPositionMs,
    const std::string& title,
    const std::string& stereoModeLabel,
    const std::string& audioTrackLabel,
    const std::vector<std::string>& audioTrackLabels,
    int selectedAudioTrackIndex
) {
    renderer_.setUiState(
        visible,
        playing,
        buffering,
        positionMs,
        durationMs,
        bufferedPositionMs,
        title,
        stereoModeLabel,
        audioTrackLabel,
        audioTrackLabels,
        selectedAudioTrackIndex
    );
}

void OpenXrApp::setPlayerUiState(const VrPlayerUiState& state) {
    pendingHdrColorSpaceIntent_.store(state.display.hdrVideo ? 1 : 0);
    renderer_.setPlayerUiState(state);
}

void OpenXrApp::startFfmpegVideoSource(const std::string& uri, int64_t startPositionMs) {
    {
        std::lock_guard<std::mutex> lock(ffmpegVideoMutex_);
        pendingFfmpegVideoUri_ = uri;
        pendingFfmpegVideoStartMs_ = std::max<int64_t>(0, startPositionMs);
    }
    pendingFfmpegStopRequest_.store(false);
    pendingFfmpegVideoRequest_.store(true);
    XR_LOGI("DDDVR/FFmpegVideo", "FFMPEG_VIDEO_REQUEST_START startMs=%lld uri=%s", (long long)startPositionMs, uri.c_str());
}

void OpenXrApp::stopFfmpegVideoSource() {
    pendingFfmpegVideoRequest_.store(false);
    pendingFfmpegStopRequest_.store(true);
    XR_LOGI("DDDVR/FFmpegVideo", "FFMPEG_VIDEO_REQUEST_STOP");
}

void OpenXrApp::setFfmpegPlaybackState(bool playing, int64_t positionMs, bool forceSeek) {
    ffmpegVideoDecoder_.setPlaybackState(playing, positionMs, forceSeek);
}

void OpenXrApp::setFfmpegMuted(bool muted) {
    ffmpegVideoDecoder_.setMuted(muted);
}

bool OpenXrApp::selectFfmpegAudioTrack(const std::string& trackId) {
    return ffmpegVideoDecoder_.selectAudioTrack(trackId);
}

FfmpegPlaybackSnapshot OpenXrApp::ffmpegPlaybackSnapshot() const {
    return ffmpegVideoDecoder_.playbackSnapshot();
}

std::vector<FfmpegAudioTrackInfo> OpenXrApp::ffmpegAudioTracks() const {
    return ffmpegVideoDecoder_.audioTracks();
}

bool OpenXrApp::initialize() { XR_LOGI("DDDVR/OpenXR", "OpenXrApp created; init deferred to render thread"); return true; }

bool OpenXrApp::start() {
    pendingStart_ = true;
    if (running_) return true;
    running_ = true;
    sessionRunning_ = false;
    initOk_ = false;
    initialized_ = false;
    firstFrameSubmitted_ = false;
    exitRequested_ = false;
    restartRequested_ = false;
    stoppedBySeethroughOrFocusLoss_ = false;
    frameCount_ = 0;
    frameCountBeforeStop_ = 0;
    beginSessionCount_ = 0;
    endSessionCount_ = 0;
    fboOkSeen_ = false;
    startTime_ = std::chrono::steady_clock::now();
    lastWaitLog_ = startTime_;
    XR_LOGI("DDDVR/OpenXR", "OpenXrApp::start requested (pending)");
    thread_ = std::thread(&OpenXrApp::loop, this);
    XR_LOGI("DDDVR/OpenXR", "OpenXrApp::start returned without waiting for XR_READY");
    return true;
}

bool OpenXrApp::initOnRenderThread() {
    XR_LOGI("DDDVR/OpenXR", "OpenXrApp initializeOnRenderThread begin");
    auto fail = [this](const char* phase, const std::string& reason) { lastError_ = reason; XR_LOGE("DDDVR/OpenXR", "CURRENT_BLOCKER: %s failed: %s", phase, reason.c_str()); return false; };
    if (!session_.initializeLoaderAndInstance()) return fail("InstanceReady", session_.lastError());
    if (!session_.prepareGraphics()) return fail("GraphicsReady", session_.lastError());
    if (!session_.createSession()) return fail("SessionReady", session_.lastError());
    input_.initialize(session_.instance(), session_.session(), [this](OpenXrInputActionCode code) {
        dispatchInputActionOnRenderThread(code);
    });
    if (!session_.createReferenceSpace()) return fail("SpaceReady", session_.lastError());
    renderer_.initialize(renderConfig_);
    createVideoSurfaceOnRenderThread();
    if (!swapchain_.create(session_.session(), (int32_t)session_.recommendedWidth(), (int32_t)session_.recommendedHeight())) return fail("SwapchainReady", "swapchain create failed");
    return true;
}

void OpenXrApp::loop() {
    initOk_ = initOnRenderThread();
    if (!initOk_) {
        XR_LOGE("DDDVR/OpenXR", "OpenXrApp::loop initialization failed: %s", lastError_.c_str());
        XR_LOGI("DDDVR/OpenXR", "stopAndJoinThread reason=init_failed");
        XR_LOGI("DDDVR/OpenXR", "failed initialization cleanup");
        swapchain_.destroy();
        input_.destroy();
        session_.shutdown();
        running_ = false;
        return;
    }
    initialized_ = true;

    std::vector<XrView> views(2, {XR_TYPE_VIEW});
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    bool loggedInvalidLoopState = false;
    uint64_t frameCounter = 0;
    OpenXrPointerRay pointerRays[2]{};
    OpenXrPointerRay gripPoses[2]{};
    OpenXrPointerRay previousPointerRays[2]{};
    std::chrono::steady_clock::time_point lastPointerMotionTime{};
    std::chrono::steady_clock::time_point lastFfmpegVideoUploadTime{};
    auto renderStatsStart = std::chrono::steady_clock::now();
    uint64_t renderStatsFrames = 0;
    uint64_t renderStatsLayerFrames = 0;
    double renderStatsCpuMs = 0.0;
    double renderStatsMaxCpuMs = 0.0;
    uint64_t droppedFfmpegVideoUploads = 0;

    while (running_) {
        if (!initialized_) {
            if (!loggedInvalidLoopState) {
                XR_LOGE("DDDVR/OpenXR", "OpenXrApp::loop invalid state: initialized=false");
                loggedInvalidLoopState = true;
            }
        frameCounter++;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        session_.pollEvents();
        const XrSessionState state = session_.currentState();
        if (!sessionRunning_ && state == XR_SESSION_STATE_READY) {
            XR_LOGI("DDDVR/OpenXRSession", "XR_SESSION_READY_AFTER_STOPPING");
            sessionRunning_ = session_.begin();
            XR_LOGI("DDDVR/OpenXRSession", "sessionRunning=%s", sessionRunning_ ? "true" : "false");
            if (sessionRunning_ && pendingStart_) {
                pendingStart_ = false;
                XR_LOGI("DDDVR/OpenXR", "pendingStart consumed");
                XR_LOGI("DDDVR/OpenXR", "OpenXrApp started");
            }
            if (sessionRunning_) {
                beginSessionCount_ += 1;
                XR_LOGI("DDDVR/OpenXR", "CURRENT_STATE XR_SESSION_RESUMED_OR_STARTED");
                if (stoppedBySeethroughOrFocusLoss_) {
                    XR_LOGI("DDDVR/OpenXR", "CURRENT_STATE XR_SESSION_RESUMED_AFTER_SEETHROUGH");
                } else {
                    XR_LOGI("DDDVR/OpenXR", "XR_SESSION_RESUMED_AFTER_STOPPING");
                }
            }
        }
        if (state == XR_SESSION_STATE_STOPPING && sessionRunning_) {
            XR_LOGI("DDDVR/OpenXRSession", "XR_SESSION_STOPPING_NORMAL frameCount=%llu", (unsigned long long)frameCount_);
            XR_LOGI("DDDVR/OpenXR", "XR_SESSION_STOPPED_BY_RUNTIME");
            const bool endOk = session_.end();
            if (endOk) {
                sessionRunning_ = false;
                endSessionCount_ += 1;
                frameCountBeforeStop_ = frameCount_;
                stoppedBySeethroughOrFocusLoss_ = true;
                XR_LOGI("DDDVR/OpenXR", "XR_SESSION_ENDED_OK frameCountBeforeStop=%llu", (unsigned long long)frameCountBeforeStop_);
                XR_LOGI("DDDVR/OpenXR", "XR_SESSION_WAITING_FOR_READY_AFTER_STOPPING");
                XR_LOGI("DDDVR/OpenXR", "CURRENT_STATE XR_SESSION_WAITING_FOR_READY_AFTER_STOPPING");
                XR_LOGI("DDDVR/OpenXR", "CURRENT_STATE XR_SESSION_STOPPED_BY_SEETHROUGH");
                XR_LOGI("DDDVR/OpenXR", "CURRENT_STATE XR_SESSION_WAITING_FOR_READY_AFTER_SEETHROUGH");
                if (frameCount_ > 0) XR_LOGI("DDDVR/OpenXR", "CURRENT_STATE XR_SESSION_STOPPED_AFTER_SUCCESSFUL_FRAME_LOOP");
            }
            continue;
        }
        if (state == XR_SESSION_STATE_EXITING) {
            XR_LOGI("DDDVR/OpenXR", "XR_SESSION_EXITING exitRequested=1 restartRequested=0");
            exitRequested_ = true;
            restartRequested_ = false;
            running_ = false;
            break;
        }
        if (state == XR_SESSION_STATE_LOSS_PENDING) {
            XR_LOGW("DDDVR/OpenXR", "XR_SESSION_LOSS_PENDING exitRequested=1 restartRequested=1");
            exitRequested_ = true;
            restartRequested_ = true;
            running_ = false;
            break;
        }
        if (!sessionRunning_) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastWaitLog_ > std::chrono::seconds(1)) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();
                XR_LOGI("DDDVR/OpenXR", "waiting READY state=%d elapsed=%lld running=%d initialized=%d sessionRunning=%d", session_.currentState(), (long long)elapsed, running_ ? 1 : 0, initialized_ ? 1 : 0, sessionRunning_ ? 1 : 0);
                if (stoppedBySeethroughOrFocusLoss_ || beginSessionCount_ > 0) {
                    XR_LOGI("DDDVR/OpenXR", "XR_SESSION_WAITING_FOR_RUNTIME_FOCUS state=%d beginCount=%llu endCount=%llu",
                            session_.currentState(), (unsigned long long)beginSessionCount_, (unsigned long long)endSessionCount_);
                } else {
                    if (session_.currentState() == XR_SESSION_STATE_IDLE && elapsed >= 12) {
                        XR_LOGE("DDDVR/OpenXR", "CURRENT_BLOCKER SESSION_STUCK_IDLE_BEFORE_FIRST_FRAME_LOOP");
                        XR_LOGE("DDDVR/OpenXR", "CURRENT_BLOCKER XR_SESSION_READY_EVENT_NOT_RECEIVED");
                        XR_LOGE("DDDVR/OpenXR", "CURRENT_BLOCKER XR_READY_NOT_RECEIVED");
                    }
                    if (elapsed >= 8 && !sessionRunning_ && session_.currentState() != XR_SESSION_STATE_READY) {
                        XR_LOGE("DDDVR/OpenXR", "CURRENT_BLOCKER XR_SESSION_READY_EVENT_NOT_RECEIVED state=%d", session_.currentState());
                    }
                    if (elapsed >= 8 && (session_.currentState() == XR_SESSION_STATE_VISIBLE || session_.currentState() == XR_SESSION_STATE_SYNCHRONIZED)) {
                        XR_LOGE("DDDVR/OpenXR", "CURRENT_BLOCKER XR_VISIBILITY_CHANGED_TO_FALSE state=%d", session_.currentState());
                    }
                }
                lastWaitLog_ = now;
            }
            XR_LOGI("DDDVR/OpenXR", "XR_RENDER_SKIPPED_SESSION_NOT_RUNNING state=%d", state);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        applyPendingXrColorSpaceOnRenderThread();
        applyPendingFfmpegVideoRequestOnRenderThread();
        OpenXrFrameControls controls{};
        bool grabControlActive = false;
        if (state == XR_SESSION_STATE_FOCUSED) {
            input_.sync();
            controls = input_.consumeFrameControls();
            grabControlActive = input_.activeGrabHand() >= 0;
            if (std::fabs(controls.screenYawDeltaRadians) > 0.0001f) {
                renderer_.adjustScreenYaw(controls.screenYawDeltaRadians);
            }
            if (!grabControlActive && std::fabs(controls.screenDistanceDeltaMeters) > 0.0001f) {
                renderer_.adjustScreenDistance(controls.screenDistanceDeltaMeters);
            }
            if (std::fabs(controls.screenCurveDeltaRadians) > 0.0001f) {
                renderer_.adjustScreenCurve(controls.screenCurveDeltaRadians);
            }
        }
        const bool shouldLogFrameCall = (frameCounter < 10 || frameCounter % 120 == 0);
        XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO}; XrFrameState fs{XR_TYPE_FRAME_STATE}; if (shouldLogFrameCall) XR_LOGI("DDDVR/OpenXRRenderer", "XR_CALL_BEGIN xrWaitFrame"); XrResult wr = xrWaitFrame(session_.session(), &wi, &fs); if (shouldLogFrameCall || wr != XR_SUCCESS) XR_LOGI("DDDVR/OpenXRRenderer", "XR_CALL_END xrWaitFrame result=%d", wr); if (wr != XR_SUCCESS) continue;
        const auto frameCpuStart = std::chrono::steady_clock::now();
        if (state == XR_SESSION_STATE_FOCUSED) {
            input_.locatePointerRays(session_.appSpace(), fs.predictedDisplayTime, pointerRays);
            input_.locateGripPoses(session_.appSpace(), fs.predictedDisplayTime, gripPoses);
            bool pointerMoved = false;
            for (int hand = 0; hand < 2; ++hand) {
                if (pointerRays[hand].active) {
                    if (!previousPointerRays[hand].active) {
                        pointerMoved = true;
                    } else {
                        const auto& nowPose = pointerRays[hand].pose;
                        const auto& prevPose = previousPointerRays[hand].pose;
                        const float dx = nowPose.position.x - prevPose.position.x;
                        const float dy = nowPose.position.y - prevPose.position.y;
                        const float dz = nowPose.position.z - prevPose.position.z;
                        const float positionDeltaSq = dx * dx + dy * dy + dz * dz;
                        const float orientationDot = std::fabs(
                            nowPose.orientation.x * prevPose.orientation.x +
                            nowPose.orientation.y * prevPose.orientation.y +
                            nowPose.orientation.z * prevPose.orientation.z +
                            nowPose.orientation.w * prevPose.orientation.w
                        );
                        if (positionDeltaSq > 0.0004f || orientationDot < 0.9995f) {
                            pointerMoved = true;
                        }
                    }
                }
                previousPointerRays[hand] = pointerRays[hand];
            }
            const auto pointerNow = std::chrono::steady_clock::now();
            if (pointerMoved) lastPointerMotionTime = pointerNow;
            const bool showRaysForMotion =
                lastPointerMotionTime.time_since_epoch().count() != 0 &&
                pointerNow - lastPointerMotionTime < std::chrono::milliseconds(1500);
            const bool raysActiveForUi = input_.shouldShowPointerRays() || showRaysForMotion;
            bool triggerPressed[2] = {
                input_.triggerPressed(0),
                input_.triggerPressed(1)
            };
            if (raysActiveForUi) {
                renderer_.setPointerRays(pointerRays);
                renderer_.updateUiInteraction(pointerRays, triggerPressed, true);
            } else {
                OpenXrPointerRay hiddenRays[2]{};
                renderer_.setPointerRays(hiddenRays);
                renderer_.updateUiInteraction(hiddenRays, triggerPressed, false);
            }
            const int uiHand = renderer_.activeUiPointerHand();
            if (uiHand >= 0 && triggerPressed[uiHand]) {
                input_.markTriggerConsumedByUi(uiHand);
            }
            OpenXrInputActionCode uiAction = OpenXrInputActionCode::None;
            while (renderer_.consumeUiInputAction(&uiAction)) {
                dispatchInputActionOnRenderThread(uiAction);
            }
            int uiProgressPermille = -1;
            while (renderer_.consumeUiTimelineSeek(&uiProgressPermille)) {
                if (uiHand >= 0) {
                    input_.markTriggerConsumedByUi(uiHand);
                    input_.markTriggerTimelineConsumed(uiHand);
                }
                dispatchTimelineSeekOnRenderThread(uiProgressPermille);
            }
            int uiAudioTrackIndex = -1;
            while (renderer_.consumeUiAudioTrackSelection(&uiAudioTrackIndex)) {
                if (uiHand >= 0) {
                    input_.markTriggerConsumedByUi(uiHand);
                }
                dispatchAudioTrackSelectedOnRenderThread(uiAudioTrackIndex);
            }
            VrPlayerPanelAction panelAction{};
            while (renderer_.consumePlayerPanelAction(&panelAction)) {
                if (uiHand >= 0) {
                    input_.markTriggerConsumedByUi(uiHand);
                }
                dispatchPlayerUiActionOnRenderThread(panelAction);
            }
#if defined(DDDVR_LEGACY_PRIMITIVE_UI)
            {
                CinemaUiHoverTarget hoverTarget = CinemaUiHoverTarget::None;
                if (raysActiveForUi) {
                    for (const auto& ray : pointerRays) {
                        if (!ray.active) continue;
                        const CinemaUiHoverTarget rayTarget = renderer_.playerHoverTarget(ray.pose);
                        if (hoverPriority(rayTarget) > hoverPriority(hoverTarget)) {
                            hoverTarget = rayTarget;
                            if (hoverTarget == CinemaUiHoverTarget::Progress) break;
                        }
                    }
                }
                renderer_.setPlayerHoverTarget(hoverTarget);
                if (controls.seekProgressPointerHand >= 0 && controls.seekProgressPointerHand < 2 &&
                    pointerRays[controls.seekProgressPointerHand].active) {
                    int progressPermille = -1;
                    if (renderer_.seekProgressFromPointer(pointerRays[controls.seekProgressPointerHand].pose, &progressPermille)) {
                        if (controls.seekProgressFromTrigger) {
                            input_.markTriggerTimelineConsumed(controls.seekProgressPointerHand);
                        }
                        dispatchTimelineSeekOnRenderThread(progressPermille);
                    }
                }
            }
#endif
            const int grabHand = input_.activeGrabHand();
            const OpenXrPointerRay* grabPose = nullptr;
            if (grabHand >= 0 && pointerRays[grabHand].active) {
                grabPose = &pointerRays[grabHand];
            } else if (grabHand >= 0 && gripPoses[grabHand].active) {
                grabPose = &gripPoses[grabHand];
            }
            XrPosef emptyPose{{0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 0.f}};
            const float grabDistanceDeltaMeters = grabHand >= 0
                ? controls.screenDistanceDeltaMeters
                : 0.f;
            const bool grabMoved = renderer_.updateScreenGrab(
                grabPose != nullptr,
                grabPose != nullptr ? grabPose->pose : emptyPose,
                grabDistanceDeltaMeters,
                false
            );
            if (grabMoved) {
                input_.markGrabMotionConsumed();
            }
        } else {
            pointerRays[0].active = false;
            pointerRays[1].active = false;
            renderer_.setPointerRays(pointerRays);
            bool triggerPressed[2] = {false, false};
            renderer_.updateUiInteraction(pointerRays, triggerPressed, false);
            XrPosef emptyPose{{0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 0.f}};
            renderer_.updateScreenGrab(false, emptyPose, 0.f);
        }
        XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO}; if (shouldLogFrameCall) XR_LOGI("DDDVR/OpenXRRenderer", "XR_CALL_BEGIN xrBeginFrame"); XrResult br = xrBeginFrame(session_.session(), &bi); if (shouldLogFrameCall || br != XR_SUCCESS) XR_LOGI("DDDVR/OpenXRRenderer", "XR_CALL_END xrBeginFrame result=%d", br); if (br != XR_SUCCESS) continue;
        XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO}; li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO; li.displayTime = fs.predictedDisplayTime; li.space = session_.appSpace(); XrViewState vs{XR_TYPE_VIEW_STATE}; uint32_t count = 0; XrResult lr = xrLocateViews(session_.session(), &li, &vs, (uint32_t)views.size(), &count, views.data()); if (frameCounter < 10 || frameCounter % 120 == 0 || lr != XR_SUCCESS) XR_LOGI("DDDVR/OpenXRRenderer", "xrLocateViews result=%d", lr);
        bool acquired = false; bool fboOk = false;
        FfmpegHardwareBufferFrame ffmpegHardwareFrame{};
        FfmpegVideoFrame ffmpegFrame{};
        bool ffmpegVideoUpdated = false;
        const bool videoUpdated = updateVideoSurfaceOnRenderThread();
        const int64_t surfacePtsUs = videoSurfaceTimestampNsValue_ >= 0
            ? videoSurfaceTimestampNsValue_ / 1000
            : -1;
        if (ffmpegVideoDecoder_.pollHardwareSurfaceMetadata(&ffmpegFrame, surfacePtsUs)) {
            renderer_.updateFfmpegSurfaceMetadata(ffmpegFrame);
            ffmpegFrame = {};
        }
        if (ffmpegVideoDecoder_.pollDirectFrame(&ffmpegFrame)) {
            ffmpegVideoUpdated = renderer_.uploadFfmpegVideoFrame(ffmpegFrame);
            ffmpegVideoDecoder_.releaseDirectFrame();
            if (ffmpegVideoUpdated) {
                lastFfmpegVideoUploadTime = std::chrono::steady_clock::now();
            }
        } else if (ffmpegVideoDecoder_.pollHardwareBufferFrame(&ffmpegHardwareFrame)) {
            ffmpegVideoUpdated = renderer_.importFfmpegHardwareBufferFrame(
                std::move(ffmpegHardwareFrame)
            );
            if (ffmpegVideoUpdated) {
                lastFfmpegVideoUploadTime = std::chrono::steady_clock::now();
            }
        } else if (ffmpegVideoDecoder_.pollFrame(&ffmpegFrame)) {
            const auto now = std::chrono::steady_clock::now();
            const bool uploadAllowed =
                lastFfmpegVideoUploadTime.time_since_epoch().count() == 0 ||
                now - lastFfmpegVideoUploadTime >= kFfmpegVideoMinUploadInterval;
            if (uploadAllowed) {
                ffmpegVideoUpdated = renderer_.uploadFfmpegVideoFrame(ffmpegFrame);
                if (ffmpegVideoUpdated) {
                    lastFfmpegVideoUploadTime = now;
                }
            } else {
                ++droppedFfmpegVideoUploads;
                if (droppedFfmpegVideoUploads == 1 || droppedFfmpegVideoUploads % 120 == 0) {
                    XR_LOGW(
                        "DDDVR/FFmpegVideo",
                        "FFMPEG_VIDEO_UPLOAD_DROPPED_FOR_UI count=%llu frame=%dx%d",
                        (unsigned long long)droppedFfmpegVideoUploads,
                        ffmpegFrame.width,
                        ffmpegFrame.height
                    );
                }
            }
        }
        renderer_.setVideoFrameState(videoTransform_, videoUpdated);
        if (ffmpegVideoUpdated) {
            renderer_.setVideoFrameState(nullptr, true);
        }
        if (lr == XR_SUCCESS && swapchain_.acquireImage()) { acquired = true; glBindFramebuffer(GL_FRAMEBUFFER, fbo); fboOk = true;
            static int srgbWriteControlSupported = -1;
            static bool srgbWriteControlLogged = false;
            if (swapchain_.format() == GL_SRGB8_ALPHA8) {
                if (srgbWriteControlSupported < 0) {
                    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
                    srgbWriteControlSupported = extensions != nullptr && std::strstr(extensions, "GL_EXT_sRGB_write_control") != nullptr ? 1 : 0;
                }
                if (srgbWriteControlSupported == 1) {
                    const bool enabledBefore = glIsEnabled(kFramebufferSrgbExt) == GL_TRUE;
                    // Default projection rendering uses normal sRGB writes.
                    // The video renderer temporarily disables this state for
                    // the exact 4XVR HDR pass and restores it before VR UI.
                    glEnable(kFramebufferSrgbExt);
                    if (!srgbWriteControlLogged) {
                        XR_LOGI(
                            "DDDVR/OpenXRColor",
                            "XR_SRGB_WRITE_CONTROL extension=1 mode=fourxvr_linear_to_srgb before=%d after=%d glError=0x%x",
                            enabledBefore ? 1 : 0,
                            glIsEnabled(kFramebufferSrgbExt) == GL_TRUE ? 1 : 0,
                            glGetError()
                        );
                        srgbWriteControlLogged = true;
                    }
                } else if (!srgbWriteControlLogged) {
                    XR_LOGI("DDDVR/OpenXRColor", "XR_SRGB_WRITE_CONTROL extension=0");
                    srgbWriteControlLogged = true;
                }
            }
            for (int eye=0; eye<2; ++eye){ auto pre=glGetError(); glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, swapchain_.activeColorTexture(),0,eye); auto fb=glCheckFramebufferStatus(GL_FRAMEBUFFER); auto post=glGetError(); if (frameCounter < 10 || frameCounter % 120 == 0 || fb!=GL_FRAMEBUFFER_COMPLETE || post!=GL_NO_ERROR) XR_LOGI("DDDVR/OpenXRRenderer","eye=%d imageArrayIndex=%d tex=%u viewport=%dx%d swap=%dx%d fbo status=0x%x glErrPre=0x%x glErrPost=0x%x",eye,eye,swapchain_.activeColorTexture(),swapchain_.width(),swapchain_.height(),swapchain_.width(),swapchain_.height(),fb,pre,post); if (fb==GL_FRAMEBUFFER_COMPLETE && !fboOkSeen_){ XR_LOGI("DDDVR/OpenXRCheck", "FBO_OK"); fboOkSeen_=true; }
            if (fb!=GL_FRAMEBUFFER_COMPLETE){ XR_LOGE("DDDVR/OpenXRCheck", "FBO_FAIL status=0x%x glErr=0x%x", fb, post); XR_LOGE("DDDVR/OpenXR","CURRENT_BLOCKER: FBO incomplete eye=%d status=0x%x glErr=0x%x",eye,fb,post); fboOk=false; break;} renderer_.renderEye(eye, swapchain_.width(), swapchain_.height(), views[eye]); } }
        XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO}; ei.displayTime = fs.predictedDisplayTime; ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE; ei.layerCount = 0;
        XrCompositionLayerProjectionView pv[2]{}; XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION}; const XrCompositionLayerBaseHeader* layers[1];
        if (lr==XR_SUCCESS && acquired && fboOk){ for(int eye=0;eye<2;++eye){pv[eye].type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;pv[eye].pose=views[eye].pose;pv[eye].fov=views[eye].fov;pv[eye].subImage.swapchain=swapchain_.handle();pv[eye].subImage.imageRect.extent={swapchain_.width(),swapchain_.height()};pv[eye].subImage.imageArrayIndex=eye;} layer.space=session_.appSpace();layer.viewCount=2;layer.views=pv;layers[0]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer); ei.layerCount=1; ei.layers=layers; }
        if (acquired) {
            swapchain_.releaseImage();
        }
        if (shouldLogFrameCall) XR_LOGI("DDDVR/OpenXRRenderer", "XR_CALL_BEGIN xrEndFrame");
        XrResult er = xrEndFrame(session_.session(), &ei); if (shouldLogFrameCall || er != XR_SUCCESS) XR_LOGI("DDDVR/OpenXRRenderer", "XR_CALL_END xrEndFrame result=%d", er);
        const auto frameCpuEnd = std::chrono::steady_clock::now();
        const double frameCpuMs = std::chrono::duration<double, std::milli>(
            frameCpuEnd - frameCpuStart
        ).count();
        renderStatsCpuMs += frameCpuMs;
        renderStatsMaxCpuMs = std::max(renderStatsMaxCpuMs, frameCpuMs);
        if (er == XR_SUCCESS) {
            ++renderStatsFrames;
            if (ei.layerCount > 0) ++renderStatsLayerFrames;
        }
        if (frameCpuEnd - renderStatsStart >= std::chrono::seconds(5)) {
            const double seconds = std::chrono::duration<double>(
                frameCpuEnd - renderStatsStart
            ).count();
            XR_LOGI(
                "DDDVR/OpenXRPerf",
                "XR_RENDER_STATS fps=%.2f submitted=%llu layerFrames=%llu avgCpuMs=%.2f maxCpuMs=%.2f videoSurfaceFpsLoggedSeparately=1",
                seconds > 0.0 ? static_cast<double>(renderStatsFrames) / seconds : 0.0,
                (unsigned long long)renderStatsFrames,
                (unsigned long long)renderStatsLayerFrames,
                renderStatsFrames > 0 ? renderStatsCpuMs / static_cast<double>(renderStatsFrames) : 0.0,
                renderStatsMaxCpuMs
            );
            renderStatsStart = frameCpuEnd;
            renderStatsFrames = 0;
            renderStatsLayerFrames = 0;
            renderStatsCpuMs = 0.0;
            renderStatsMaxCpuMs = 0.0;
        }
        if (er == XR_SUCCESS && !firstFrameSubmitted_) {
            XR_LOGI("DDDVR/OpenXRCheck", "FRAME_LOOP_OK");
            firstFrameSubmitted_ = true;
            XR_LOGI("DDDVR/OpenXR", "first successful xrEndFrame");
        }
        frameCount_ = ++frameCounter;
    }
    XR_LOGI("DDDVR/OpenXR", "XR_LIFECYCLE_SUMMARY initialized=%d sessionState=%d sessionRunning=%d frameCount=%llu frameCountBeforeStop=%llu beginSessionCount=%llu endSessionCount=%llu stoppedBySeethrough=%d resumedAfterSeethrough=%d exitRequested=%d restartRequested=%d",
            initialized_ ? 1 : 0, (int)session_.currentState(), sessionRunning_ ? 1 : 0,
            (unsigned long long)frameCount_, (unsigned long long)frameCountBeforeStop_,
            (unsigned long long)beginSessionCount_, (unsigned long long)endSessionCount_,
            stoppedBySeethroughOrFocusLoss_ ? 1 : 0, (stoppedBySeethroughOrFocusLoss_ && sessionRunning_) ? 1 : 0,
            exitRequested_ ? 1 : 0, restartRequested_ ? 1 : 0);
    renderer_.setFfmpegVideoEnabled(false);
    ffmpegVideoDecoder_.stop();
    XR_LOGI("DDDVR/OpenXR", "swapchain destroy reason=loop exit");
    releaseVideoSurfaceOnRenderThread();
    swapchain_.destroy();
    input_.destroy();
    XR_LOGI("DDDVR/OpenXRCheck", "SUMMARY loader=%d instance=%d system=%d gl=%d session=%d referenceSpace=%d swapchain=%d fbo=%d frameLoop=%d", 1,1,1,1,1,1,1, fboOkSeen_ ? 1 : 0, firstFrameSubmitted_ ? 1 : 0);
    XR_LOGI("DDDVR/OpenXR", "normal shutdown");
    XR_LOGI("DDDVR/OpenXR", "OpenXrSession destroy reason=loop exit");
    session_.shutdown();
}

void OpenXrApp::stopAndJoinThread(const char* reason){ XR_LOGI("DDDVR/OpenXR", "stopAndJoinThread reason=%s", reason); running_=false; if(thread_.joinable()) thread_.join(); sessionRunning_=false; }
void OpenXrApp::pause(){ XR_LOGI("DDDVR/OpenXR", "OpenXrApp::pause requested nonFatal=1"); androidPaused_ = true; }
void OpenXrApp::resume(){ XR_LOGI("DDDVR/OpenXR", "OpenXrApp::resume requested"); androidPaused_ = false; }
void OpenXrApp::destroy(){ XR_LOGI("DDDVR/OpenXR", "OpenXrApp::destroy requested"); ffmpegVideoDecoder_.stop(); stopAndJoinThread("destroy"); }

JNIEnv* OpenXrApp::attachCurrentThread(bool* didAttach) const {
    if (didAttach != nullptr) *didAttach = false;
    JavaVM* vm = dddvr::openxr::javaVm();
    if (vm == nullptr) return nullptr;
    JNIEnv* env = nullptr;
    const jint getEnvResult = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (getEnvResult == JNI_OK) return env;
    if (getEnvResult == JNI_EDETACHED && vm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
        if (didAttach != nullptr) *didAttach = true;
        return env;
    }
    return nullptr;
}

void OpenXrApp::detachCurrentThread(bool didAttach) const {
    if (didAttach) {
        JavaVM* vm = dddvr::openxr::javaVm();
        if (vm != nullptr) vm->DetachCurrentThread();
    }
}

void OpenXrApp::dispatchInputActionOnRenderThread(OpenXrInputActionCode code) {
    switch (code) {
        case OpenXrInputActionCode::ScreenYawLeft:
            renderer_.adjustScreenYaw(-0.08f);
            return;
        case OpenXrInputActionCode::ScreenYawRight:
            renderer_.adjustScreenYaw(0.08f);
            return;
        case OpenXrInputActionCode::ScreenCloser:
            renderer_.adjustScreenDistance(-0.15f);
            return;
        case OpenXrInputActionCode::ScreenFarther:
            renderer_.adjustScreenDistance(0.15f);
            return;
        case OpenXrInputActionCode::ScreenCurveLess:
            renderer_.adjustScreenCurve(-0.08f);
            return;
        case OpenXrInputActionCode::ScreenCurveMore:
            renderer_.adjustScreenCurve(0.08f);
            return;
        case OpenXrInputActionCode::Recenter:
            renderer_.resetScreenPlacement();
            break;
        default:
            break;
    }
    if (javaBridgeRef_ == nullptr || bridgeOnInputAction_ == nullptr) return;
    bool didAttach = false;
    JNIEnv* env = attachCurrentThread(&didAttach);
    if (env == nullptr) return;
    env->CallVoidMethod(javaBridgeRef_, bridgeOnInputAction_, static_cast<jint>(code));
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        XR_LOGE("DDDVR/OpenXRInput", "XR_INPUT_CALLBACK_FAILED code=%d", static_cast<int>(code));
    }
    detachCurrentThread(didAttach);
}

void OpenXrApp::dispatchTimelineSeekOnRenderThread(int32_t progressPermille) {
    if (javaBridgeRef_ == nullptr || bridgeOnTimelineSeek_ == nullptr) return;
    if (progressPermille < 0) progressPermille = 0;
    if (progressPermille > 1000) progressPermille = 1000;
    bool didAttach = false;
    JNIEnv* env = attachCurrentThread(&didAttach);
    if (env == nullptr) return;
    env->CallVoidMethod(javaBridgeRef_, bridgeOnTimelineSeek_, static_cast<jint>(progressPermille));
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        XR_LOGE("DDDVR/OpenXRInput", "XR_TIMELINE_SEEK_CALLBACK_FAILED progress=%d", progressPermille);
    } else {
        XR_LOGI("DDDVR/OpenXRInput", "XR_TIMELINE_SEEK_DISPATCH progress=%d", progressPermille);
    }
    detachCurrentThread(didAttach);
}

void OpenXrApp::dispatchAudioTrackSelectedOnRenderThread(int32_t trackIndex) {
    if (javaBridgeRef_ == nullptr || bridgeOnAudioTrackSelected_ == nullptr) return;
    if (trackIndex < 0) return;
    bool didAttach = false;
    JNIEnv* env = attachCurrentThread(&didAttach);
    if (env == nullptr) return;
    env->CallVoidMethod(javaBridgeRef_, bridgeOnAudioTrackSelected_, static_cast<jint>(trackIndex));
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        XR_LOGE("DDDVR/OpenXRInput", "XR_AUDIO_TRACK_CALLBACK_FAILED index=%d", trackIndex);
    } else {
        XR_LOGI("DDDVR/OpenXRInput", "XR_AUDIO_TRACK_DISPATCH index=%d", trackIndex);
    }
    detachCurrentThread(didAttach);
}

void OpenXrApp::dispatchPlayerUiActionOnRenderThread(const VrPlayerPanelAction& action) {
    if (javaBridgeRef_ == nullptr || bridgeOnPlayerUiAction_ == nullptr) return;
    bool didAttach = false;
    JNIEnv* env = attachCurrentThread(&didAttach);
    if (env == nullptr) return;
    jstring stringValue = env->NewStringUTF(action.stringValue.c_str());
    env->CallVoidMethod(
        javaBridgeRef_,
        bridgeOnPlayerUiAction_,
        static_cast<jint>(action.type),
        static_cast<jint>(action.intValue),
        static_cast<jfloat>(action.floatValue),
        stringValue
    );
    if (stringValue != nullptr) {
        env->DeleteLocalRef(stringValue);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        XR_LOGE("DDDVR/OpenXRInput", "XR_PLAYER_UI_ACTION_CALLBACK_FAILED type=%d", static_cast<int>(action.type));
    } else {
        XR_LOGI(
            "DDDVR/OpenXRInput",
            "XR_PLAYER_UI_ACTION_DISPATCH type=%d int=%d float=%.3f text=%s",
            static_cast<int>(action.type),
            action.intValue,
            action.floatValue,
            action.stringValue.c_str()
        );
    }
    detachCurrentThread(didAttach);
}

void OpenXrApp::applyPendingXrColorSpaceOnRenderThread() {
    const int32_t intent = pendingHdrColorSpaceIntent_.load();
    if (intent == appliedHdrColorSpaceIntent_) return;
    const bool hdrVideo = intent != 0;
    if (session_.setHdrColorSpace(hdrVideo)) {
        appliedHdrColorSpaceIntent_ = intent;
        XR_LOGI(
            "DDDVR/OpenXRColor",
            "XR_COLOR_SPACE_INTENT_APPLIED hdrVideo=%d intent=%d",
            hdrVideo ? 1 : 0,
            intent
        );
    }
}

void OpenXrApp::applyPendingFfmpegVideoRequestOnRenderThread() {
    if (pendingFfmpegStopRequest_.exchange(false)) {
        renderer_.setFfmpegVideoEnabled(false);
        ffmpegVideoDecoder_.stop();
        XR_LOGI("DDDVR/FFmpegVideo", "FFMPEG_VIDEO_STOP_APPLIED");
    }

    if (!pendingFfmpegVideoRequest_.exchange(false)) return;

    std::string uri;
    int64_t startMs = 0;
    {
        std::lock_guard<std::mutex> lock(ffmpegVideoMutex_);
        uri = pendingFfmpegVideoUri_;
        startMs = pendingFfmpegVideoStartMs_;
    }

    if (uri.empty()) {
        XR_LOGW("DDDVR/FFmpegVideo", "FFMPEG_VIDEO_START_SKIPPED empty_uri");
        return;
    }

    renderer_.setFfmpegVideoEnabled(false);
    const bool started = ffmpegVideoDecoder_.start(
        uri,
        startMs,
        dddvr::openxr::javaVm(),
        videoDecoderSurfaceRef_
    );
    renderer_.setFfmpegVideoEnabled(started);
    if (!started) {
        XR_LOGE("DDDVR/FFmpegVideo", "FFMPEG_VIDEO_START_FAILED error=%s", ffmpegVideoDecoder_.lastError().c_str());
    }
}

bool OpenXrApp::createVideoSurfaceOnRenderThread() {
    if (javaBridgeRef_ == nullptr || videoSurfaceClass_ == nullptr || videoSurfaceRef_ != nullptr) {
        XR_LOGI("DDDVR/OpenXRVideo", "XR_VIDEO_SURFACE_CREATE_SKIPPED bridge=%p class=%p surface=%p",
                javaBridgeRef_, videoSurfaceClass_, videoSurfaceRef_);
        return false;
    }

    bool didAttach = false;
    JNIEnv* env = attachCurrentThread(&didAttach);
    if (env == nullptr) {
        XR_LOGE("DDDVR/OpenXRVideo", "CURRENT_BLOCKER XR_VIDEO_SURFACE_NO_JNI_ENV");
        return false;
    }

    jobject localSurfaceHolder = env->NewObject(videoSurfaceClass_, videoSurfaceCtor_, (jint)renderer_.videoTextureId());
    if (env->ExceptionCheck() || localSurfaceHolder == nullptr) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        XR_LOGE("DDDVR/OpenXRVideo", "CURRENT_BLOCKER XR_VIDEO_SURFACE_CREATE_FAILED texture=%u", renderer_.videoTextureId());
        detachCurrentThread(didAttach);
        return false;
    }
    videoSurfaceRef_ = env->NewGlobalRef(localSurfaceHolder);
    env->DeleteLocalRef(localSurfaceHolder);

    jfloatArray localTransformArray = env->NewFloatArray(16);
    videoTransformArray_ = reinterpret_cast<jfloatArray>(env->NewGlobalRef(localTransformArray));
    env->SetFloatArrayRegion(videoTransformArray_, 0, 16, videoTransform_);
    env->DeleteLocalRef(localTransformArray);

    applyPendingVideoSizeOnRenderThread(env);

    jobject surface = env->CallObjectMethod(videoSurfaceRef_, videoSurfaceGetSurface_);
    if (env->ExceptionCheck() || surface == nullptr) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        XR_LOGE("DDDVR/OpenXRVideo", "CURRENT_BLOCKER XR_VIDEO_SURFACE_GET_SURFACE_FAILED");
        detachCurrentThread(didAttach);
        return false;
    }

    videoDecoderSurfaceRef_ = env->NewGlobalRef(surface);
    if (videoDecoderSurfaceRef_ == nullptr) {
        XR_LOGE("DDDVR/OpenXRVideo", "CURRENT_BLOCKER XR_VIDEO_DECODER_SURFACE_REF_FAILED");
        env->DeleteLocalRef(surface);
        detachCurrentThread(didAttach);
        return false;
    }

    env->CallVoidMethod(javaBridgeRef_, bridgeOnVideoSurfaceReady_, surface);
    env->DeleteLocalRef(surface);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        XR_LOGE("DDDVR/OpenXRVideo", "CURRENT_BLOCKER XR_VIDEO_SURFACE_CALLBACK_FAILED");
        detachCurrentThread(didAttach);
        return false;
    }

    XR_LOGI("DDDVR/OpenXRVideo", "XR_VIDEO_SURFACE_CREATED texture=%u", renderer_.videoTextureId());
    detachCurrentThread(didAttach);
    return true;
}

bool OpenXrApp::updateVideoSurfaceOnRenderThread() {
    if (videoSurfaceRef_ == nullptr || videoTransformArray_ == nullptr) return false;
    bool didAttach = false;
    JNIEnv* env = attachCurrentThread(&didAttach);
    if (env == nullptr) return false;

    applyPendingVideoSizeOnRenderThread(env);

    const jboolean updated = env->CallBooleanMethod(videoSurfaceRef_, videoSurfaceUpdateTexImage_, videoTransformArray_);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        detachCurrentThread(didAttach);
        return false;
    }

    if (updated == JNI_TRUE) {
        env->GetFloatArrayRegion(videoTransformArray_, 0, 16, videoTransform_);
        videoSurfaceTimestampNsValue_ = static_cast<int64_t>(
            env->CallLongMethod(videoSurfaceRef_, videoSurfaceTimestampNs_)
        );
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            videoSurfaceTimestampNsValue_ = -1;
        }
        videoFrameUpdateCount_ += 1;
        const auto now = std::chrono::steady_clock::now();
        if (videoFrameStatsStart_.time_since_epoch().count() == 0) {
            videoFrameStatsStart_ = now;
            videoFrameStatsBaseCount_ = videoFrameUpdateCount_;
        } else if (now - videoFrameStatsStart_ >= std::chrono::seconds(5)) {
            const double seconds = std::chrono::duration<double>(now - videoFrameStatsStart_).count();
            const uint64_t frames = videoFrameUpdateCount_ - videoFrameStatsBaseCount_;
            XR_LOGI(
                "DDDVR/OpenXRVideo",
                "XR_VIDEO_SURFACE_STATS fps=%.2f frames=%llu total=%llu",
                seconds > 0.0 ? static_cast<double>(frames) / seconds : 0.0,
                (unsigned long long)frames,
                (unsigned long long)videoFrameUpdateCount_
            );
            videoFrameStatsStart_ = now;
            videoFrameStatsBaseCount_ = videoFrameUpdateCount_;
        }
        if (!videoFrameSeen_) {
            XR_LOGI("DDDVR/OpenXRVideo", "XR_VIDEO_FIRST_FRAME texture=%u", renderer_.videoTextureId());
        } else if (videoFrameUpdateCount_ % 120 == 0) {
            XR_LOGI("DDDVR/OpenXRVideo", "XR_VIDEO_FRAME_UPDATE count=%llu", (unsigned long long)videoFrameUpdateCount_);
        }
        videoFrameSeen_ = true;
    }

    detachCurrentThread(didAttach);
    return updated == JNI_TRUE;
}

void OpenXrApp::applyPendingVideoSizeOnRenderThread(JNIEnv* env) {
    const int32_t width = pendingVideoWidth_.load();
    const int32_t height = pendingVideoHeight_.load();
    if (width <= 0 || height <= 0) return;
    const float pixelWidthHeightRatio = pendingVideoPixelWidthHeightRatio_.load();
    if (width != appliedRendererVideoWidth_ ||
        height != appliedRendererVideoHeight_ ||
        std::fabs(pixelWidthHeightRatio - appliedRendererPixelWidthHeightRatio_) > 0.0001f) {
        renderer_.setVideoSize(width, height, pixelWidthHeightRatio);
        appliedRendererVideoWidth_ = width;
        appliedRendererVideoHeight_ = height;
        appliedRendererPixelWidthHeightRatio_ = pixelWidthHeightRatio;
    }

    const float displayAspectRatio = pendingDisplayAspectRatio_.load();
    if (std::fabs(displayAspectRatio - appliedDisplayAspectRatio_) > 0.0001f) {
        renderer_.setDisplayAspectRatio(displayAspectRatio);
        appliedDisplayAspectRatio_ = displayAspectRatio;
    }

    if (env == nullptr || videoSurfaceRef_ == nullptr || videoSurfaceSetDefaultBufferSize_ == nullptr) return;
    if (width == appliedVideoWidth_ && height == appliedVideoHeight_) return;
    env->CallVoidMethod(videoSurfaceRef_, videoSurfaceSetDefaultBufferSize_, width, height);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        XR_LOGE("DDDVR/OpenXRVideo", "XR_VIDEO_SET_DEFAULT_BUFFER_FAILED width=%d height=%d", width, height);
        return;
    }
    appliedVideoWidth_ = width;
    appliedVideoHeight_ = height;
    XR_LOGI("DDDVR/OpenXRVideo", "XR_VIDEO_DEFAULT_BUFFER_APPLIED width=%d height=%d", width, height);
}

void OpenXrApp::releaseVideoSurfaceOnRenderThread() {
    if (videoSurfaceRef_ == nullptr && videoTransformArray_ == nullptr) return;
    bool didAttach = false;
    JNIEnv* env = attachCurrentThread(&didAttach);
    if (env != nullptr) {
        if (videoDecoderSurfaceRef_ != nullptr) {
            env->DeleteGlobalRef(videoDecoderSurfaceRef_);
            videoDecoderSurfaceRef_ = nullptr;
        }
        if (videoSurfaceRef_ != nullptr && videoSurfaceRelease_ != nullptr) {
            env->CallVoidMethod(videoSurfaceRef_, videoSurfaceRelease_);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            env->DeleteGlobalRef(videoSurfaceRef_);
            videoSurfaceRef_ = nullptr;
        }
        if (videoTransformArray_ != nullptr) {
            env->DeleteGlobalRef(videoTransformArray_);
            videoTransformArray_ = nullptr;
        }
    }
    detachCurrentThread(didAttach);
    videoSurfaceTimestampNsValue_ = -1;
    videoFrameSeen_ = false;
    videoFrameUpdateCount_ = 0;
    videoFrameStatsBaseCount_ = 0;
    videoFrameStatsStart_ = {};
}

void OpenXrApp::releaseJavaRefs() {
    bool didAttach = false;
    JNIEnv* env = attachCurrentThread(&didAttach);
    if (env == nullptr) return;

    if (videoDecoderSurfaceRef_ != nullptr) {
        env->DeleteGlobalRef(videoDecoderSurfaceRef_);
        videoDecoderSurfaceRef_ = nullptr;
    }
    if (videoSurfaceRef_ != nullptr) {
        if (videoSurfaceRelease_ != nullptr) {
            env->CallVoidMethod(videoSurfaceRef_, videoSurfaceRelease_);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        env->DeleteGlobalRef(videoSurfaceRef_);
        videoSurfaceRef_ = nullptr;
    }
    if (videoTransformArray_ != nullptr) {
        env->DeleteGlobalRef(videoTransformArray_);
        videoTransformArray_ = nullptr;
    }
    if (videoSurfaceClass_ != nullptr) {
        env->DeleteGlobalRef(videoSurfaceClass_);
        videoSurfaceClass_ = nullptr;
    }
    if (javaBridgeRef_ != nullptr) {
        env->DeleteGlobalRef(javaBridgeRef_);
        javaBridgeRef_ = nullptr;
    }
    bridgeOnVideoSurfaceReady_ = nullptr;
    bridgeOnInputAction_ = nullptr;
    bridgeOnTimelineSeek_ = nullptr;
    bridgeOnAudioTrackSelected_ = nullptr;
    bridgeOnPlayerUiAction_ = nullptr;
    videoSurfaceCtor_ = nullptr;
    videoSurfaceGetSurface_ = nullptr;
    videoSurfaceUpdateTexImage_ = nullptr;
    videoSurfaceTimestampNs_ = nullptr;
    videoSurfaceSetDefaultBufferSize_ = nullptr;
    videoSurfaceRelease_ = nullptr;
    detachCurrentThread(didAttach);
}
