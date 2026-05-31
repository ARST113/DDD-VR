#pragma once
#include "OpenXrPlatform.h"
#include "OpenXrInput.h"
#include "../video/ExternalOesVideoTexture.h"
#include "../gl/CinemaScreenRenderer.h"
#include <GLES3/gl3.h>
#include <atomic>

enum class OpenXrStereoMode {
    Mono,
    Sbs,
    SbsReversed,
    Ou,
    OuReversed
};

struct OpenXrRenderConfig {
    OpenXrStereoMode stereoMode = OpenXrStereoMode::Mono;
    bool swapEyes = false;
    float screenDistanceMeters = 3.5f;
    float screenWidthMeters = 4.5f;
    float screenCurveRadians = 0.45f;
};

class OpenXrRenderer {
public:
    bool initialize(const OpenXrRenderConfig& config);
    void setVideoFrameState(const float* transformMatrix, bool hasVideo);
    void setUiState(bool visible, int progressPermille, bool playing);
    void setPointerRays(const OpenXrPointerRay rays[2]);
    void setPlayerHoverTarget(CinemaUiHoverTarget target);
    bool updateScreenGrab(bool active, const XrPosef& gripPose, float rayDistanceDeltaMeters);
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
    CinemaUvRect uvRectForEye(int eye) const;
    OpenXrRenderConfig config_;
    ExternalOesVideoTexture video_;
    CinemaScreenRenderer screen_;
    OpenXrPointerRay pointerRays_[2];
    float screenYawRadians_ = 0.f;
    float screenDistanceMeters_ = 3.5f;
    float screenCenterX_ = 0.f;
    float screenCenterY_ = 0.f;
    float screenCenterZ_ = -3.5f;
    float screenCurveRadians_ = 0.45f;
    bool screenGrabActive_ = false;
    XrPosef grabStartPose_{{0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 0.f}};
    float grabStartRayDistanceMeters_ = 3.5f;
    float grabStartOffsetX_ = 0.f;
    float grabStartOffsetY_ = 0.f;
    float grabStartOffsetZ_ = 0.f;
    float grabStartCenterX_ = 0.f;
    float grabStartCenterY_ = 0.f;
    float grabStartCenterZ_ = -3.5f;
    int screenHighlightFrameBudget_ = 0;
    float videoTransform_[16] = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f
    };
    bool hasVideoFrame_ = false;
    std::atomic<bool> uiVisible_{true};
    std::atomic<int> uiProgressPermille_{0};
    std::atomic<bool> uiPlaying_{false};
    int uiAutoHideFrameBudget_ = 0;
    CinemaUiHoverTarget uiHoverTarget_ = CinemaUiHoverTarget::None;
};
