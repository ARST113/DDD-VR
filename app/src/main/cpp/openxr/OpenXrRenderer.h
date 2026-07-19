#pragma once
#include "OpenXrPlatform.h"
#include "OpenXrInput.h"
#include "../video/ExternalOesVideoTexture.h"
#include "../video/FfmpegVideoTexture.h"
#include "../gl/CinemaScreenRenderer.h"
#include "../ui/VrPicoGui.h"
#include "../ui/VrPlayerPanel.h"
#include "../ui/VrRayInteractor.h"
#include <GLES3/gl3.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

enum class OpenXrStereoMode {
    Mono,
    Sbs,
    SbsReversed,
    Ou,
    OuReversed,
    VrCamV1,
    VrCamV2
};

enum class OpenXrScreenModeNative {
    Flat,
    Curved,
    Vr180,
    Vr360
};

enum class OpenXrStereoPackingNative {
    Full,
    Half
};

struct OpenXrRenderConfig {
    OpenXrStereoMode stereoMode = OpenXrStereoMode::Mono;
    OpenXrStereoPackingNative stereoPacking = OpenXrStereoPackingNative::Full;
    OpenXrScreenModeNative screenMode = OpenXrScreenModeNative::Flat;
    bool swapEyes = false;
    float screenDistanceMeters = 3.5f;
    float screenWidthMeters = 4.5f;
    float screenCurveRadians = 0.45f;
};

class OpenXrRenderer {
public:
    bool initialize(const OpenXrRenderConfig& config);
    void setVideoFrameState(const float* transformMatrix, bool hasVideo);
    void setVideoSize(int width, int height, float pixelWidthHeightRatio);
    void setDisplayAspectRatio(float aspectRatio);
    bool uploadFfmpegVideoFrame(const FfmpegVideoFrame& frame);
    bool importFfmpegHardwareBufferFrame(FfmpegHardwareBufferFrame&& frame);
    void updateFfmpegSurfaceMetadata(const FfmpegVideoFrame& frame);
    void setFfmpegVideoEnabled(bool enabled);
    void setUiState(
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
    );
    void setPlayerUiState(const VrPlayerUiState& state);
    void setPointerRays(const OpenXrPointerRay rays[2]);
    void updateUiInteraction(const OpenXrPointerRay rays[2], const bool triggerPressed[2], bool active);
    int activeUiPointerHand() const { return activeUiPointerHand_; }
    bool consumeUiInputAction(OpenXrInputActionCode* outAction);
    bool consumeUiTimelineSeek(int* outProgressPermille);
    bool consumeUiAudioTrackSelection(int* outTrackIndex);
    bool consumePlayerPanelAction(VrPlayerPanelAction* outAction);
    void setPlayerHoverTarget(CinemaUiHoverTarget target);
    bool updateScreenGrab(
        bool active,
        const XrPosef& gripPose,
        float rayDistanceDeltaMeters,
        bool directTranslationMode = false
    );
    bool seekProgressFromPointer(const XrPosef& aimPose, int* outProgressPermille);
    CinemaUiHoverTarget playerHoverTarget(const XrPosef& aimPose) const;
    void adjustScreenYaw(float deltaRadians);
    void adjustScreenDistance(float deltaMeters);
    void adjustScreenCurve(float deltaRadians);
    void resetScreenPlacement();
    void renderEye(int eye, int width, int height, const XrView& view);
    unsigned int videoTextureId() const { return video_.id(); }
private:
    void applyScreenPlacement();
    void updateCenterFromYawDistance();
    void clampScreenCenter();
    VrUiPlane targetUiPlane() const;
    VrUiPlane currentScreenPlane() const;
    VrRayHit screenHitTest(const XrPosef& aimPose, int hand) const;
    void updateUiPlane(float deltaSeconds);
    void updateUiTexture();
    VrUiPlane currentUiPlane() const;
    void queuePlayerPanelActions();
    void renderUiCursor(const float* mvp, const VrRayHit& hit, const VrUiPlane& plane);
    CinemaUvRect uvRectForEye(int eye) const;
    float screenHeightMeters() const;
    OpenXrRenderConfig config_;
    ExternalOesVideoTexture video_;
    FfmpegVideoTexture ffmpegVideo_;
    FfmpegVideoTextureSet ffmpegSurfaceMetadata_{};
    bool ffmpegVideoEnabled_ = false;
    bool ffmpegVideoFrameSeen_ = false;
    CinemaScreenRenderer screen_;
    VrPicoGui uiBackend_;
    VrPlayerPanel playerPanel_;
    VrRayInteractor rayInteractor_;
    OpenXrPointerRay pointerRays_[2];
    VrRayHit uiRayHits_[2];
    VrRayHit screenRayHits_[2];
    VrRayHit activeUiHit_;
    VrUiPlane uiPlane_;
    bool uiPlaneInitialized_ = false;
    int activeUiPointerHand_ = -1;
    bool uiPrimaryPressed_ = false;
    bool pendingUiPlayPause_ = false;
    bool pendingUiSeekBack_ = false;
    bool pendingUiSeekForward_ = false;
    bool pendingUiExit_ = false;
    bool pendingUiTimelineSeek_ = false;
    bool pendingUiAudioTrackSelected_ = false;
    std::vector<VrPlayerPanelAction> pendingPlayerPanelActions_;
    int pendingUiTimelineProgressPermille_ = 0;
    int pendingUiAudioTrackIndex_ = -1;
    int lastUiTimelineQueuedProgressPermille_ = -1;
    std::chrono::steady_clock::time_point lastUiTimelineSeekQueued_{};
    float screenYawRadians_ = 0.f;
    float screenDistanceMeters_ = 3.5f;
    float screenCenterX_ = 0.f;
    float screenCenterY_ = 0.f;
    float screenCenterZ_ = -3.5f;
    float screenCurveRadians_ = 0.45f;
    int videoWidth_ = 0;
    int videoHeight_ = 0;
    float pixelWidthHeightRatio_ = 1.f;
    float originalDisplayAspectRatio_ = 16.f / 9.f;
    float displayAspectRatioOverride_ = 0.f;
    float displayAspectRatio_ = 16.f / 9.f;
    float uiPanelOffsetX_ = 0.f;
    float uiPanelOffsetY_ = 0.f;
    float uiPanelDragStartPixelX_ = 0.f;
    float uiPanelDragStartPixelY_ = 0.f;
    float uiPanelDragStartOffsetX_ = 0.f;
    float uiPanelDragStartOffsetY_ = 0.f;
    float uiPanelDragStartWorldX_ = 0.f;
    float uiPanelDragStartWorldY_ = 0.f;
    float uiPanelDragStartWorldZ_ = 0.f;
    bool screenGrabActive_ = false;
    bool uiPanelDragCandidateActive_ = false;
    bool uiPanelDragActive_ = false;
    bool uiClosePressed_[2]{false, false};
    int uiPanelDragCandidateHand_ = -1;
    int uiPanelDragHand_ = -1;
    XrPosef grabStartPose_{{0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 0.f}};
    float grabStartRayDistanceMeters_ = 3.5f;
    float grabStartOffsetX_ = 0.f;
    float grabStartOffsetY_ = 0.f;
    float grabStartOffsetZ_ = 0.f;
    float grabStartCenterX_ = 0.f;
    float grabStartCenterY_ = 0.f;
    float grabStartCenterZ_ = -3.5f;
    float grabStartYawRadians_ = 0.f;
    bool screenGrabDirectMode_ = false;
    int screenHighlightFrameBudget_ = 0;
    float videoTransform_[16] = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f
    };
    bool hasVideoFrame_ = false;
    std::mutex playerPanelMutex_;
    std::atomic<bool> uiVisible_{true};
    std::atomic<bool> uiModalOpen_{false};
    std::atomic<int> uiProgressPermille_{0};
    std::atomic<bool> uiPlaying_{false};
    int uiAutoHideFrameBudget_ = 0;
    CinemaUiHoverTarget uiHoverTarget_ = CinemaUiHoverTarget::None;
    std::chrono::steady_clock::time_point lastUiFrameTime_{};
};
