#include "OpenXrRenderer.h"
#include "../util/XrLog.h"
#include <array>
#include <cmath>
#include <cstring>

namespace {
constexpr float kMinScreenDistanceMeters = 1.5f;
constexpr float kMaxScreenDistanceMeters = 8.0f;
constexpr float kMinScreenHeightMeters = -1.5f;
constexpr float kMaxScreenHeightMeters = 1.8f;
constexpr float kGrabFollowSmoothing = 0.48f;
constexpr float kGrabConsumedMoveThresholdMeters = 0.14f;
constexpr float kGrabConsumedYawThresholdRadians = 0.07f;
constexpr int kUiAutoHideFrames = 180;
constexpr float kUiPlaneOffsetMeters = 0.12f;
constexpr float kUiPanelWidthScale = 1.28f;
constexpr float kUiPanelYOffsetMeters = -0.36f;
constexpr float kUiPanelHeightMeters = 0.32f;
constexpr float kUiProgressYOffsetMeters = -0.105f;
constexpr float kUiProgressWidthScale = 0.74f;
constexpr float kUiPlayButtonWidthMeters = 0.34f;
constexpr float kUiPlayButtonHeightMeters = 0.18f;
constexpr float kUiPlayButtonYOffsetMeters = 0.055f;

std::array<float, 16> multiply(const std::array<float, 16>& a, const std::array<float, 16>& b) {
    std::array<float, 16> out{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            out[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
    return out;
}

std::array<float, 16> projectionFromFov(const XrFovf& fov) {
    const float tanLeft = std::tan(fov.angleLeft);
    const float tanRight = std::tan(fov.angleRight);
    const float tanDown = std::tan(fov.angleDown);
    const float tanUp = std::tan(fov.angleUp);
    const float tanWidth = tanRight - tanLeft;
    const float tanHeight = tanUp - tanDown;
    const float nearZ = 0.05f;
    const float farZ = 100.f;
    std::array<float, 16> m{};
    m[0] = 2.f / tanWidth;
    m[5] = 2.f / tanHeight;
    m[8] = (tanRight + tanLeft) / tanWidth;
    m[9] = (tanUp + tanDown) / tanHeight;
    m[10] = -(farZ + nearZ) / (farZ - nearZ);
    m[11] = -1.f;
    m[14] = -(2.f * farZ * nearZ) / (farZ - nearZ);
    return m;
}

std::array<float, 16> viewFromPose(const XrPosef& pose) {
    const auto& q = pose.orientation;
    const auto& p = pose.position;
    const float x2 = q.x + q.x;
    const float y2 = q.y + q.y;
    const float z2 = q.z + q.z;
    const float xx = q.x * x2;
    const float xy = q.x * y2;
    const float xz = q.x * z2;
    const float yy = q.y * y2;
    const float yz = q.y * z2;
    const float zz = q.z * z2;
    const float wx = q.w * x2;
    const float wy = q.w * y2;
    const float wz = q.w * z2;

    std::array<float, 16> m{};
    m[0] = 1.f - yy - zz;
    m[1] = xy - wz;
    m[2] = xz + wy;
    m[4] = xy + wz;
    m[5] = 1.f - xx - zz;
    m[6] = yz - wx;
    m[8] = xz - wy;
    m[9] = yz + wx;
    m[10] = 1.f - xx - yy;
    m[12] = -(m[0] * p.x + m[4] * p.y + m[8] * p.z);
    m[13] = -(m[1] * p.x + m[5] * p.y + m[9] * p.z);
    m[14] = -(m[2] * p.x + m[6] * p.y + m[10] * p.z);
    m[15] = 1.f;
    return m;
}

std::array<float, 3> rotateByQuat(const XrQuaternionf& q, float vx, float vy, float vz) {
    const float tx = 2.f * (q.y * vz - q.z * vy);
    const float ty = 2.f * (q.z * vx - q.x * vz);
    const float tz = 2.f * (q.x * vy - q.y * vx);
    return {
        vx + q.w * tx + (q.y * tz - q.z * ty),
        vy + q.w * ty + (q.z * tx - q.x * tz),
        vz + q.w * tz + (q.x * ty - q.y * tx)
    };
}

float clampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

float normalizeRadians(float radians) {
    while (radians > 3.14159265f) radians -= 6.28318531f;
    while (radians < -3.14159265f) radians += 6.28318531f;
    return radians;
}

float horizontalDistance(float x, float z) {
    return std::sqrt(x * x + z * z);
}

float aimYawFromPose(const XrPosef& pose) {
    const auto direction = rotateByQuat(pose.orientation, 0.f, 0.f, -1.f);
    return std::atan2(direction[0], -direction[2]);
}
}

bool OpenXrRenderer::initialize(const OpenXrRenderConfig& config){
    config_ = config;
    screenDistanceMeters_ = config_.screenDistanceMeters;
    screenCenterX_ = 0.f;
    screenCenterY_ = 0.f;
    screenCenterZ_ = -screenDistanceMeters_;
    screenCurveRadians_ = config_.screenCurveRadians;
    video_.create();
    screen_.initialize(config_.screenWidthMeters, screenDistanceMeters_, screenCurveRadians_);
    XR_LOGI("DDDVR/OpenXRRenderer","renderer initialized stereo=%d swap=%d width=%.2f distance=%.2f curve=%.2f",
            (int)config_.stereoMode, config_.swapEyes ? 1 : 0,
            config_.screenWidthMeters, screenDistanceMeters_, screenCurveRadians_);
    return true;
}

void OpenXrRenderer::setVideoFrameState(const float* transformMatrix, bool hasVideo) {
    if (transformMatrix != nullptr) {
        std::memcpy(videoTransform_, transformMatrix, sizeof(videoTransform_));
    }
    hasVideoFrame_ = hasVideoFrame_ || hasVideo;
}

void OpenXrRenderer::setUiState(bool visible, int progressPermille, bool playing) {
    uiVisible_.store(visible);
    if (progressPermille < 0) progressPermille = 0;
    if (progressPermille > 1000) progressPermille = 1000;
    uiProgressPermille_.store(progressPermille);
    uiPlaying_.store(playing);
}

void OpenXrRenderer::setPointerRays(const OpenXrPointerRay rays[2]) {
    if (rays == nullptr) {
        pointerRays_[0].active = false;
        pointerRays_[1].active = false;
        return;
    }
    pointerRays_[0] = rays[0];
    pointerRays_[1] = rays[1];
}

void OpenXrRenderer::setPlayerHoverTarget(CinemaUiHoverTarget target) {
    if (target != uiHoverTarget_) {
        XR_LOGI("DDDVR/OpenXRInput", "XR_UI_HOVER target=%d", static_cast<int>(target));
    }
    uiHoverTarget_ = target;
    if (target != CinemaUiHoverTarget::None) {
        uiAutoHideFrameBudget_ = kUiAutoHideFrames;
    }
}

bool OpenXrRenderer::updateScreenGrab(bool active, const XrPosef& gripPose, float rayDistanceDeltaMeters) {
    if (!active) {
        if (screenGrabActive_) {
            XR_LOGI("DDDVR/OpenXRRenderer", "XR_SCREEN_GRAB_END");
        }
        screenGrabActive_ = false;
        return false;
    }

    auto direction = rotateByQuat(gripPose.orientation, 0.f, 0.f, -1.f);
    const float directionLength = std::sqrt(
        direction[0] * direction[0] +
        direction[1] * direction[1] +
        direction[2] * direction[2]
    );
    if (directionLength > 0.001f) {
        direction[0] /= directionLength;
        direction[1] /= directionLength;
        direction[2] /= directionLength;
    }

    if (!screenGrabActive_) {
        screenGrabActive_ = true;
        grabStartPose_ = gripPose;
        grabStartCenterX_ = screenCenterX_;
        grabStartCenterY_ = screenCenterY_;
        grabStartCenterZ_ = screenCenterZ_;
        const float toCenterX = screenCenterX_ - gripPose.position.x;
        const float toCenterY = screenCenterY_ - gripPose.position.y;
        const float toCenterZ = screenCenterZ_ - gripPose.position.z;
        grabStartRayDistanceMeters_ =
            toCenterX * direction[0] +
            toCenterY * direction[1] +
            toCenterZ * direction[2];
        grabStartRayDistanceMeters_ = clampFloat(
            grabStartRayDistanceMeters_,
            kMinScreenDistanceMeters,
            kMaxScreenDistanceMeters
        );
        grabStartOffsetX_ = screenCenterX_ - (gripPose.position.x + direction[0] * grabStartRayDistanceMeters_);
        grabStartOffsetY_ = screenCenterY_ - (gripPose.position.y + direction[1] * grabStartRayDistanceMeters_);
        grabStartOffsetZ_ = screenCenterZ_ - (gripPose.position.z + direction[2] * grabStartRayDistanceMeters_);
        XR_LOGI("DDDVR/OpenXRRenderer", "XR_SCREEN_GRAB_BEGIN x=%.2f y=%.2f z=%.2f rayDistance=%.2f",
                screenCenterX_, screenCenterY_, screenCenterZ_, grabStartRayDistanceMeters_);
        return false;
    }

    if (std::fabs(rayDistanceDeltaMeters) > 0.0001f) {
        grabStartRayDistanceMeters_ = clampFloat(
            grabStartRayDistanceMeters_ + rayDistanceDeltaMeters,
            kMinScreenDistanceMeters,
            kMaxScreenDistanceMeters
        );
        screenHighlightFrameBudget_ = 30;
        static uint32_t grabDistanceLogCount = 0;
        grabDistanceLogCount += 1;
        if (grabDistanceLogCount <= 8 || grabDistanceLogCount % 30 == 0) {
            XR_LOGI("DDDVR/OpenXRRenderer", "XR_SCREEN_GRAB_DISTANCE rayDistance=%.2f delta=%.3f",
                    grabStartRayDistanceMeters_, rayDistanceDeltaMeters);
        }
    }

    const float targetX = gripPose.position.x + direction[0] * grabStartRayDistanceMeters_ + grabStartOffsetX_;
    const float targetY = gripPose.position.y + direction[1] * grabStartRayDistanceMeters_ + grabStartOffsetY_;
    const float targetZ = gripPose.position.z + direction[2] * grabStartRayDistanceMeters_ + grabStartOffsetZ_;
    const float targetYaw = std::atan2(targetX, -targetZ);

    const float moveFromStartX = targetX - grabStartCenterX_;
    const float moveFromStartY = targetY - grabStartCenterY_;
    const float moveFromStartZ = targetZ - grabStartCenterZ_;
    const float movedMeters = std::sqrt(
        moveFromStartX * moveFromStartX +
        moveFromStartY * moveFromStartY +
        moveFromStartZ * moveFromStartZ
    );
    const bool consumed = movedMeters > kGrabConsumedMoveThresholdMeters ||
        std::fabs(normalizeRadians(targetYaw - screenYawRadians_)) > kGrabConsumedYawThresholdRadians;

    screenCenterX_ += (targetX - screenCenterX_) * kGrabFollowSmoothing;
    screenCenterY_ += (targetY - screenCenterY_) * kGrabFollowSmoothing;
    screenCenterZ_ += (targetZ - screenCenterZ_) * kGrabFollowSmoothing;
    screenYawRadians_ += normalizeRadians(targetYaw - screenYawRadians_) * kGrabFollowSmoothing;
    screenYawRadians_ = normalizeRadians(screenYawRadians_);
    clampScreenCenter();
    applyScreenPlacement();

    static uint32_t grabLogCount = 0;
    grabLogCount += 1;
    if (grabLogCount <= 5 || grabLogCount % 45 == 0) {
        XR_LOGI("DDDVR/OpenXRRenderer", "XR_SCREEN_GRAB_MOVE x=%.2f y=%.2f z=%.2f yaw=%.3f consumed=%d",
                screenCenterX_, screenCenterY_, screenCenterZ_, screenYawRadians_, consumed ? 1 : 0);
    }
    return consumed;
}

bool OpenXrRenderer::seekProgressFromPointer(const XrPosef& aimPose, int* outProgressPermille) {
    if (outProgressPermille == nullptr) return false;
    auto direction = rotateByQuat(aimPose.orientation, 0.f, 0.f, -1.f);
    const float directionLength = std::sqrt(
        direction[0] * direction[0] +
        direction[1] * direction[1] +
        direction[2] * direction[2]
    );
    if (directionLength <= 0.001f) return false;
    direction[0] /= directionLength;
    direction[1] /= directionLength;
    direction[2] /= directionLength;

    const float c = std::cos(screenYawRadians_);
    const float s = std::sin(screenYawRadians_);
    const float normalX = -s;
    const float normalZ = c;
    const float denom = direction[0] * normalX + direction[2] * normalZ;
    if (std::fabs(denom) <= 0.001f) return false;

    const float planeX = screenCenterX_ + normalX * kUiPlaneOffsetMeters;
    const float planeZ = screenCenterZ_ + normalZ * kUiPlaneOffsetMeters;
    const float toPlaneX = planeX - aimPose.position.x;
    const float toPlaneZ = planeZ - aimPose.position.z;
    const float t = (toPlaneX * normalX + toPlaneZ * normalZ) / denom;
    if (t <= 0.f) return false;

    const float hitX = aimPose.position.x + direction[0] * t;
    const float hitY = aimPose.position.y + direction[1] * t;
    const float hitZ = aimPose.position.z + direction[2] * t;
    const float localX = (hitX - screenCenterX_) * c + (hitZ - screenCenterZ_) * s;
    const float localY = hitY - screenCenterY_;

    const float halfWidth = config_.screenWidthMeters * 0.5f;
    const float halfHeight = config_.screenWidthMeters * (9.f / 16.f) * 0.5f;
    const float progressWidth = halfWidth * kUiPanelWidthScale * kUiProgressWidthScale;
    const float progressHalfWidth = progressWidth * 0.5f;
    const float progressCenterY = -halfHeight + kUiPanelYOffsetMeters + kUiProgressYOffsetMeters;
    const float progressHitHalfHeight = 0.060f;

    if (std::fabs(localY - progressCenterY) > progressHitHalfHeight ||
        localX < -progressHalfWidth ||
        localX > progressHalfWidth) {
        XR_LOGI("DDDVR/OpenXRInput", "XR_TIMELINE_SEEK_MISS localX=%.2f localY=%.2f progressY=%.2f",
                localX, localY, progressCenterY);
        return false;
    }

    int permille = static_cast<int>(((localX + progressHalfWidth) / progressWidth) * 1000.f + 0.5f);
    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    *outProgressPermille = permille;
    screenHighlightFrameBudget_ = 45;
    XR_LOGI("DDDVR/OpenXRInput", "XR_TIMELINE_SEEK_HIT permille=%d localX=%.2f localY=%.2f",
            permille, localX, localY);
    return true;
}

CinemaUiHoverTarget OpenXrRenderer::playerHoverTarget(const XrPosef& aimPose) const {
    auto direction = rotateByQuat(aimPose.orientation, 0.f, 0.f, -1.f);
    const float directionLength = std::sqrt(
        direction[0] * direction[0] +
        direction[1] * direction[1] +
        direction[2] * direction[2]
    );
    if (directionLength <= 0.001f) return CinemaUiHoverTarget::None;
    direction[0] /= directionLength;
    direction[1] /= directionLength;
    direction[2] /= directionLength;

    const float c = std::cos(screenYawRadians_);
    const float s = std::sin(screenYawRadians_);
    const float normalX = -s;
    const float normalZ = c;
    const float denom = direction[0] * normalX + direction[2] * normalZ;
    if (std::fabs(denom) <= 0.001f) return CinemaUiHoverTarget::None;

    auto targetForPlane = [&](float localZ, bool includeVideo) {
        const float planeX = screenCenterX_ + normalX * localZ;
        const float planeZ = screenCenterZ_ + normalZ * localZ;
        const float toPlaneX = planeX - aimPose.position.x;
        const float toPlaneZ = planeZ - aimPose.position.z;
        const float t = (toPlaneX * normalX + toPlaneZ * normalZ) / denom;
        if (t <= 0.f) return CinemaUiHoverTarget::None;

        const float hitX = aimPose.position.x + direction[0] * t;
        const float hitY = aimPose.position.y + direction[1] * t;
        const float hitZ = aimPose.position.z + direction[2] * t;
        const float localX = (hitX - screenCenterX_) * c + (hitZ - screenCenterZ_) * s;
        const float localY = hitY - screenCenterY_;

        const float halfWidth = config_.screenWidthMeters * 0.5f;
        const float halfHeight = config_.screenWidthMeters * (9.f / 16.f) * 0.5f;
        if (includeVideo &&
            localX >= -halfWidth && localX <= halfWidth &&
            localY >= -halfHeight && localY <= halfHeight) {
            return CinemaUiHoverTarget::Video;
        }

        const float panelW = halfWidth * kUiPanelWidthScale;
        const float panelHalfWidth = panelW * 0.5f;
        const float panelCenterY = -halfHeight + kUiPanelYOffsetMeters;
        const bool overPanel =
            localX >= -panelHalfWidth && localX <= panelHalfWidth &&
            localY >= panelCenterY - kUiPanelHeightMeters * 0.5f &&
            localY <= panelCenterY + kUiPanelHeightMeters * 0.5f;
        if (!overPanel) return CinemaUiHoverTarget::None;

        const float progressWidth = panelW * kUiProgressWidthScale;
        const float progressHalfWidth = progressWidth * 0.5f;
        const float progressCenterY = panelCenterY + kUiProgressYOffsetMeters;
        if (localX >= -progressHalfWidth && localX <= progressHalfWidth &&
            std::fabs(localY - progressCenterY) <= 0.060f) {
            return CinemaUiHoverTarget::Progress;
        }

        const float playCenterY = panelCenterY + kUiPlayButtonYOffsetMeters;
        if (std::fabs(localX) <= kUiPlayButtonWidthMeters * 0.5f &&
            std::fabs(localY - playCenterY) <= kUiPlayButtonHeightMeters * 0.5f) {
            return CinemaUiHoverTarget::PlayPause;
        }

        return CinemaUiHoverTarget::Panel;
    };

    const CinemaUiHoverTarget uiTarget = targetForPlane(kUiPlaneOffsetMeters, false);
    if (uiTarget != CinemaUiHoverTarget::None) return uiTarget;
    return targetForPlane(0.f, true);
}

void OpenXrRenderer::adjustScreenYaw(float deltaRadians) {
    screenYawRadians_ += deltaRadians;
    screenYawRadians_ = normalizeRadians(screenYawRadians_);
    updateCenterFromYawDistance();
    applyScreenPlacement();
}

void OpenXrRenderer::adjustScreenDistance(float deltaMeters) {
    screenDistanceMeters_ += deltaMeters;
    screenDistanceMeters_ = clampFloat(screenDistanceMeters_, kMinScreenDistanceMeters, kMaxScreenDistanceMeters);
    updateCenterFromYawDistance();
    applyScreenPlacement();
}

void OpenXrRenderer::adjustScreenCurve(float deltaRadians) {
    screenCurveRadians_ += deltaRadians;
    if (screenCurveRadians_ < 0.0f) screenCurveRadians_ = 0.0f;
    if (screenCurveRadians_ > 1.2f) screenCurveRadians_ = 1.2f;
    applyScreenPlacement();
}

void OpenXrRenderer::resetScreenPlacement() {
    screenYawRadians_ = 0.f;
    screenDistanceMeters_ = config_.screenDistanceMeters;
    screenCenterX_ = 0.f;
    screenCenterY_ = 0.f;
    screenCenterZ_ = -screenDistanceMeters_;
    screenCurveRadians_ = config_.screenCurveRadians;
    screenGrabActive_ = false;
    applyScreenPlacement();
}

void OpenXrRenderer::applyScreenPlacement() {
    screen_.setPlacement(screenYawRadians_, screenCenterX_, screenCenterY_, screenCenterZ_, screenCurveRadians_);
    static uint32_t placementLogCount = 0;
    placementLogCount += 1;
    if (placementLogCount <= 5 || placementLogCount % 60 == 0) {
        XR_LOGI("DDDVR/OpenXRRenderer", "XR_SCREEN_PLACEMENT x=%.2f y=%.2f z=%.2f yaw=%.3f distance=%.2f curve=%.2f",
                screenCenterX_, screenCenterY_, screenCenterZ_, screenYawRadians_, screenDistanceMeters_, screenCurveRadians_);
    }
}

void OpenXrRenderer::updateCenterFromYawDistance() {
    screenCenterX_ = std::sin(screenYawRadians_) * screenDistanceMeters_;
    screenCenterZ_ = -std::cos(screenYawRadians_) * screenDistanceMeters_;
}

void OpenXrRenderer::clampScreenCenter() {
    screenCenterY_ = clampFloat(screenCenterY_, kMinScreenHeightMeters, kMaxScreenHeightMeters);
    float distance = horizontalDistance(screenCenterX_, screenCenterZ_);
    if (distance < 0.001f) {
        screenCenterX_ = 0.f;
        screenCenterZ_ = -kMinScreenDistanceMeters;
        distance = kMinScreenDistanceMeters;
    }
    if (distance < kMinScreenDistanceMeters || distance > kMaxScreenDistanceMeters) {
        const float clamped = clampFloat(distance, kMinScreenDistanceMeters, kMaxScreenDistanceMeters);
        const float scale = clamped / distance;
        screenCenterX_ *= scale;
        screenCenterZ_ *= scale;
        distance = clamped;
    }
    screenDistanceMeters_ = distance;
}

void OpenXrRenderer::renderEye(int eye, int width, int height, const XrView& view){
    glViewport(0,0,width,height);
    glClearColor(0.f,0.f,0.f,1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const auto projection = projectionFromFov(view.fov);
    const auto viewMatrix = viewFromPose(view.pose);
    const auto mvp = multiply(projection, viewMatrix);
    if (eye == 0) {
        screen_.renderVideo(video_.id(), mvp.data(), videoTransform_, uvRectForEye(eye), hasVideoFrame_, 0.f, 0.f, 0.f);
    } else {
        screen_.renderVideo(video_.id(), mvp.data(), videoTransform_, uvRectForEye(eye), hasVideoFrame_, 0.f, 0.f, 0.f);
    }
    const bool shouldRenderUi = uiAutoHideFrameBudget_ > 0 || screenGrabActive_;
    if (shouldRenderUi) {
        const CinemaUiHoverTarget renderHoverTarget =
            uiAutoHideFrameBudget_ > 0 ? uiHoverTarget_ : CinemaUiHoverTarget::None;
        screen_.renderUiOverlay(mvp.data(), uiProgressPermille_.load(), uiPlaying_.load(), renderHoverTarget);
        if (eye == 1 && uiAutoHideFrameBudget_ > 0 && !screenGrabActive_) {
            uiAutoHideFrameBudget_ -= 1;
            if (uiAutoHideFrameBudget_ == 0) {
                uiHoverTarget_ = CinemaUiHoverTarget::None;
            }
        }
    }
    if (screenGrabActive_ || screenHighlightFrameBudget_ > 0) {
        screen_.renderGrabHighlight(mvp.data());
        if (eye == 1 && screenHighlightFrameBudget_ > 0) {
            screenHighlightFrameBudget_ -= 1;
        }
    }
    for (const auto& ray : pointerRays_) {
        if (!ray.active) continue;
        const auto direction = rotateByQuat(ray.pose.orientation, 0.f, 0.f, -1.f);
        const float start[3] = {
            ray.pose.position.x,
            ray.pose.position.y,
            ray.pose.position.z
        };
        const float end[3] = {
            start[0] + direction[0] * 8.f,
            start[1] + direction[1] * 8.f,
            start[2] + direction[2] * 8.f
        };
        screen_.renderRay(mvp.data(), start, end, 0.1f, 0.85f, 1.0f);
    }
}

CinemaUvRect OpenXrRenderer::uvRectForEye(int eye) const {
    int actualEye = config_.swapEyes ? 1 - eye : eye;
    switch (config_.stereoMode) {
        case OpenXrStereoMode::Sbs:
            return actualEye == 0 ? CinemaUvRect{0.f, 0.f, 0.5f, 1.f} : CinemaUvRect{0.5f, 0.f, 0.5f, 1.f};
        case OpenXrStereoMode::SbsReversed:
            return actualEye == 0 ? CinemaUvRect{0.5f, 0.f, 0.5f, 1.f} : CinemaUvRect{0.f, 0.f, 0.5f, 1.f};
        case OpenXrStereoMode::Ou:
            return actualEye == 0 ? CinemaUvRect{0.f, 0.f, 1.f, 0.5f} : CinemaUvRect{0.f, 0.5f, 1.f, 0.5f};
        case OpenXrStereoMode::OuReversed:
            return actualEye == 0 ? CinemaUvRect{0.f, 0.5f, 1.f, 0.5f} : CinemaUvRect{0.f, 0.f, 1.f, 0.5f};
        case OpenXrStereoMode::Mono:
        default:
            return CinemaUvRect{};
    }
}
