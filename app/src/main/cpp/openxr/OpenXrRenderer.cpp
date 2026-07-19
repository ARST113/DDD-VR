#include "OpenXrRenderer.h"
#include "../video/DolbyRpuParser.h"
#include "../video/AndroidImageApi.h"
#include "../ui/VrPlayerTheme.h"
#include "../util/XrLog.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

namespace {
constexpr float kMinScreenDistanceMeters = 1.5f;
constexpr float kMaxScreenDistanceMeters = 8.0f;
constexpr float kMinScreenHeightMeters = -1.5f;
constexpr float kMaxScreenHeightMeters = 1.8f;
constexpr float kGrabFollowSmoothing = 0.86f;
constexpr float kDirectGrabFollowSmoothing = 0.72f;
constexpr float kDirectGrabTranslationScale = 1.8f;
constexpr float kGrabConsumedMoveThresholdMeters = 0.14f;
constexpr float kDirectGrabConsumedMoveThresholdMeters = 0.045f;
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
constexpr int kImGuiUiTextureWidth = 1600;
constexpr int kImGuiUiTextureHeight = 520;
constexpr float kImGuiPanelWidthScale = 0.60f;
constexpr float kImGuiPanelMaxWidthMeters = 2.70f;
constexpr float kImGuiPanelHeightMeters = 0.88f;
constexpr float kImGuiPanelModalHeightMeters = 1.18f;
constexpr float kImGuiPanelDownScale = 0.64f;
constexpr float kImGuiPanelModalDownScale = 0.46f;
constexpr float kImGuiPanelForwardOffsetMeters = 0.10f;
constexpr float kImGuiPanelModalForwardOffsetMeters = 0.16f;
constexpr float kImGuiPanelFollowResponse = 14.0f;
constexpr float kUiPanelMaxOffsetXMeters = 0.65f;
constexpr float kUiPanelMaxOffsetYMeters = 0.42f;
constexpr float kUiPanelDragStartThresholdMeters = 0.018f;
constexpr bool kDetachedUiPanelDragEnabled = false;
constexpr float kAudioModalWidthPixels = 880.0f;

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

bool insideRect(float x, float y, float minX, float minY, float maxX, float maxY, float padding) {
    return x >= minX - padding &&
        x <= maxX + padding &&
        y >= minY - padding &&
        y <= maxY + padding;
}

float interactiveModalWidthPixels() {
    return std::max(VrPlayerTheme::ModalWidth, kAudioModalWidthPixels);
}

bool isPlayerUiModalClosePixelForWidth(float x, float y, float modalWidth) {
    const float canvasX = static_cast<float>(kImGuiUiTextureWidth);
    const float canvasY = static_cast<float>(kImGuiUiTextureHeight);
    const float centerX = canvasX * 0.5f;
    const float barTop = canvasY - VrPlayerTheme::MainBarHeight - 34.f;
    const float modalGap = 18.f;
    const float modalTopMargin = 18.f;
    const float availableModalHeight = std::max(220.f, barTop - modalGap - modalTopMargin);
    const float modalHeight = std::min(VrPlayerTheme::ModalMaxHeight, availableModalHeight);
    const float modalMaxX = centerX + modalWidth * 0.5f;
    const float modalMinY = barTop - modalGap - modalHeight;
    return insideRect(
        x,
        y,
        modalMaxX - 118.f,
        modalMinY - 6.f,
        modalMaxX + 26.f,
        modalMinY + 104.f,
        0.f
    );
}

bool isPlayerUiModalClosePixel(float x, float y, bool modalOpen) {
    if (!modalOpen) return false;
    const bool expectedCloseRect =
        isPlayerUiModalClosePixelForWidth(x, y, VrPlayerTheme::ModalWidth) ||
        isPlayerUiModalClosePixelForWidth(x, y, kAudioModalWidthPixels);
    if (expectedCloseRect) return true;

    const float canvasX = static_cast<float>(kImGuiUiTextureWidth);
    const float canvasY = static_cast<float>(kImGuiUiTextureHeight);
    const float centerX = canvasX * 0.5f;
    const float barTop = canvasY - VrPlayerTheme::MainBarHeight - 34.f;
    const float modalGap = 18.f;
    const float modalTopMargin = 18.f;
    const float availableModalHeight = std::max(220.f, barTop - modalGap - modalTopMargin);
    const float modalHeight = std::min(VrPlayerTheme::ModalMaxHeight, availableModalHeight);
    const float modalMinY = barTop - modalGap - modalHeight;
    return insideRect(
        x,
        y,
        centerX + 58.f,
        modalMinY - 18.f,
        centerX + 335.f,
        modalMinY + 72.f,
        0.f
    );
}

VrRayHit hitUiPlaneUnbounded(
    const XrPosef& aimPose,
    const VrUiPlane& plane,
    int hand,
    int textureWidth,
    int textureHeight
) {
    VrRayHit out{};
    out.hand = hand;
    const auto direction = rotateByQuat(aimPose.orientation, 0.f, 0.f, -1.f);
    const float directionLength = std::sqrt(
        direction[0] * direction[0] +
        direction[1] * direction[1] +
        direction[2] * direction[2]
    );
    if (directionLength <= 0.001f) return out;
    const float dirX = direction[0] / directionLength;
    const float dirY = direction[1] / directionLength;
    const float dirZ = direction[2] / directionLength;
    const float denom =
        dirX * plane.normal.x +
        dirY * plane.normal.y +
        dirZ * plane.normal.z;
    if (std::fabs(denom) <= 0.001f) return out;
    const float toPlaneX = plane.center.x - aimPose.position.x;
    const float toPlaneY = plane.center.y - aimPose.position.y;
    const float toPlaneZ = plane.center.z - aimPose.position.z;
    const float t =
        (toPlaneX * plane.normal.x +
         toPlaneY * plane.normal.y +
         toPlaneZ * plane.normal.z) / denom;
    if (t <= 0.f) return out;

    const float hitX = aimPose.position.x + dirX * t;
    const float hitY = aimPose.position.y + dirY * t;
    const float hitZ = aimPose.position.z + dirZ * t;
    const float deltaX = hitX - plane.center.x;
    const float deltaY = hitY - plane.center.y;
    const float deltaZ = hitZ - plane.center.z;
    const float localX =
        deltaX * plane.right.x +
        deltaY * plane.right.y +
        deltaZ * plane.right.z;
    const float localY =
        deltaX * plane.up.x +
        deltaY * plane.up.y +
        deltaZ * plane.up.z;

    out.hit = true;
    out.worldX = hitX;
    out.worldY = hitY;
    out.worldZ = hitZ;
    out.pixelX = clampFloat(
        (localX / plane.widthMeters + 0.5f) * static_cast<float>(textureWidth),
        0.f,
        static_cast<float>(textureWidth)
    );
    out.pixelY = clampFloat(
        (0.5f - localY / plane.heightMeters) * static_cast<float>(textureHeight),
        0.f,
        static_cast<float>(textureHeight)
    );
    return out;
}

bool isPlayerUiPixelInteractive(float x, float y, bool modalOpen) {
    if (x < 0.f || y < 0.f ||
        x > static_cast<float>(kImGuiUiTextureWidth) ||
        y > static_cast<float>(kImGuiUiTextureHeight)) {
        return false;
    }

    const float canvasX = static_cast<float>(kImGuiUiTextureWidth);
    const float canvasY = static_cast<float>(kImGuiUiTextureHeight);
    const float centerX = canvasX * 0.5f;
    const float barWidth = std::min(VrPlayerTheme::MainBarWidth, canvasX - 120.f);
    const float barTop = modalOpen
        ? canvasY - VrPlayerTheme::MainBarHeight - 34.f
        : canvasY - 190.f;
    const float barMinX = centerX - barWidth * 0.5f;
    const float barMaxX = centerX + barWidth * 0.5f;
    const float barMaxY = barTop + VrPlayerTheme::MainBarHeight;
    if (insideRect(x, y, barMinX, barTop, barMaxX, barMaxY, 8.f)) {
        return true;
    }

    const float handleHeight = modalOpen ? 20.f : VrPlayerTheme::DragHandleHeight;
    const float handleTop = std::min(
        barMaxY + (modalOpen ? 6.f : 14.f),
        canvasY - handleHeight - 8.f
    );
    const float handleMinX = centerX - VrPlayerTheme::DragHandleWidth * 0.5f;
    const float handleMaxX = centerX + VrPlayerTheme::DragHandleWidth * 0.5f;
    if (insideRect(x, y, handleMinX, handleTop, handleMaxX, handleTop + handleHeight, 10.f)) {
        return true;
    }

    if (modalOpen) {
        const float modalGap = 18.f;
        const float modalTopMargin = 18.f;
        const float availableModalHeight = std::max(220.f, barTop - modalGap - modalTopMargin);
        const float modalHeight = std::min(VrPlayerTheme::ModalMaxHeight, availableModalHeight);
        const float modalWidth = interactiveModalWidthPixels();
        const float modalMinX = centerX - modalWidth * 0.5f;
        const float modalMaxX = centerX + modalWidth * 0.5f;
        const float modalMinY = barTop - modalGap - modalHeight;
        const float modalMaxY = barTop - modalGap;
        if (insideRect(x, y, modalMinX, modalMinY, modalMaxX, modalMaxY, 10.f)) {
            return true;
        }
    }

    return false;
}

bool isPlayerUiDragHandlePixel(float x, float y, bool modalOpen) {
    if (!kDetachedUiPanelDragEnabled) return false;

    const float canvasX = static_cast<float>(kImGuiUiTextureWidth);
    const float canvasY = static_cast<float>(kImGuiUiTextureHeight);
    const float centerX = canvasX * 0.5f;
    const float barWidth = std::min(VrPlayerTheme::MainBarWidth, canvasX - 120.f);
    const float barTop = modalOpen
        ? canvasY - VrPlayerTheme::MainBarHeight - 34.f
        : canvasY - 190.f;
    const float barMaxY = barTop + VrPlayerTheme::MainBarHeight;
    const float handleHeight = modalOpen ? 20.f : VrPlayerTheme::DragHandleHeight;
    const float handleTop = std::min(
        barMaxY + (modalOpen ? 6.f : 14.f),
        canvasY - handleHeight - 8.f
    );
    const float handleMinX = centerX - VrPlayerTheme::DragHandleWidth * 0.5f;
    const float handleMaxX = centerX + VrPlayerTheme::DragHandleWidth * 0.5f;
    const bool overHandle = insideRect(
        x,
        y,
        handleMinX - 96.f,
        handleTop - 6.f,
        handleMaxX + 96.f,
        handleTop + handleHeight + 44.f,
        0.f
    );
    if (overHandle) return true;

    const float barMinX = centerX - barWidth * 0.5f;
    const float barMaxX = centerX + barWidth * 0.5f;
    const bool overPanel = insideRect(
        x,
        y,
        barMinX,
        barTop,
        barMaxX,
        barMaxY,
        10.f
    );
    if (overPanel) return true;

    if (modalOpen) {
        const float modalGap = 18.f;
        const float modalTopMargin = 18.f;
        const float availableModalHeight = std::max(220.f, barTop - modalGap - modalTopMargin);
        const float modalHeight = std::min(VrPlayerTheme::ModalMaxHeight, availableModalHeight);
        if (isPlayerUiModalClosePixel(x, y, modalOpen)) {
            return false;
        }
        const float modalWidth = interactiveModalWidthPixels();
        const float modalMinX = centerX - modalWidth * 0.5f;
        const float modalMaxX = centerX + modalWidth * 0.5f;
        const float modalMinY = barTop - modalGap - modalHeight;
        const float modalMaxY = barTop - modalGap;
        if (insideRect(x, y, modalMinX, modalMinY, modalMaxX, modalMaxY, 10.f)) {
            return true;
        }
    }

    return false;
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
    if (uiBackend_.initialize(kImGuiUiTextureWidth, kImGuiUiTextureHeight)) {
        VrPlayerUiState state{};
        state.visible = true;
        state.playing = false;
        state.title = "DDD-VR OpenXR Player";
        state.projectionModeLabel = "2D";
        state.durationMs = 1000;
        uiModalOpen_.store(false);
        std::lock_guard<std::mutex> lock(playerPanelMutex_);
        playerPanel_.setState(state);
    } else {
        XR_LOGE("DDDVR/OpenXRUi", "CURRENT_BLOCKER XR_UI_BACKEND_INIT_FAILED");
    }
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

void OpenXrRenderer::setVideoSize(int width, int height, float pixelWidthHeightRatio) {
    if (width <= 0 || height <= 0) return;
    videoWidth_ = width;
    videoHeight_ = height;
    pixelWidthHeightRatio_ = std::isfinite(pixelWidthHeightRatio) && pixelWidthHeightRatio > 0.f
        ? pixelWidthHeightRatio
        : 1.f;

    float aspectRatio =
        (static_cast<float>(videoWidth_) * pixelWidthHeightRatio_) /
        static_cast<float>(videoHeight_);
    const bool fullPacking = config_.stereoPacking == OpenXrStereoPackingNative::Full;
    const bool sideBySide =
        config_.stereoMode == OpenXrStereoMode::Sbs ||
        config_.stereoMode == OpenXrStereoMode::SbsReversed ||
        config_.stereoMode == OpenXrStereoMode::VrCamV1 ||
        config_.stereoMode == OpenXrStereoMode::VrCamV2;
    const bool overUnder =
        config_.stereoMode == OpenXrStereoMode::Ou ||
        config_.stereoMode == OpenXrStereoMode::OuReversed;
    if (fullPacking && sideBySide) {
        aspectRatio *= 0.5f;
    } else if (fullPacking && overUnder) {
        aspectRatio *= 2.f;
    }

    originalDisplayAspectRatio_ = std::clamp(aspectRatio, 0.75f, 3.20f);
    displayAspectRatio_ = displayAspectRatioOverride_ > 0.f
        ? displayAspectRatioOverride_
        : originalDisplayAspectRatio_;
    screen_.setAspectRatio(displayAspectRatio_);
    XR_LOGI(
        "DDDVR/OpenXRVideo",
        "XR_VIDEO_ASPECT source=%dx%d pixelRatio=%.5f stereo=%d packing=%s original=%.5f override=%.5f applied=%.5f",
        videoWidth_,
        videoHeight_,
        pixelWidthHeightRatio_,
        static_cast<int>(config_.stereoMode),
        fullPacking ? "full" : "half",
        originalDisplayAspectRatio_,
        displayAspectRatioOverride_,
        displayAspectRatio_
    );
}

void OpenXrRenderer::setDisplayAspectRatio(float aspectRatio) {
    displayAspectRatioOverride_ = std::isfinite(aspectRatio) && aspectRatio > 0.f
        ? std::clamp(aspectRatio, 0.75f, 3.20f)
        : 0.f;
    displayAspectRatio_ = displayAspectRatioOverride_ > 0.f
        ? displayAspectRatioOverride_
        : originalDisplayAspectRatio_;
    screen_.setAspectRatio(displayAspectRatio_);
    XR_LOGI(
        "DDDVR/OpenXRVideo",
        "XR_VIDEO_ASPECT_OVERRIDE requested=%.5f original=%.5f applied=%.5f",
        aspectRatio,
        originalDisplayAspectRatio_,
        displayAspectRatio_
    );
}

float OpenXrRenderer::screenHeightMeters() const {
    return config_.screenWidthMeters / std::max(displayAspectRatio_, 0.75f);
}

bool OpenXrRenderer::uploadFfmpegVideoFrame(const FfmpegVideoFrame& frame) {
    if (!ffmpegVideoEnabled_) return false;
    const bool uploaded = ffmpegVideo_.upload(frame);
    if (uploaded) {
        if (!ffmpegVideoFrameSeen_) {
            XR_LOGI(
                "DDDVR/FFmpegVideo",
                "FFMPEG_VIDEO_FIRST_FRAME width=%d height=%d fmt=%d transfer=%d primaries=%d range=%d",
                frame.width,
                frame.height,
                static_cast<int>(frame.pixelFormat),
                static_cast<int>(frame.transfer),
                static_cast<int>(frame.primaries),
                static_cast<int>(frame.range)
            );
        }
        ffmpegVideoFrameSeen_ = true;
    }
    return uploaded;
}

bool OpenXrRenderer::importFfmpegHardwareBufferFrame(
    FfmpegHardwareBufferFrame&& frame
) {
    if (!ffmpegVideoEnabled_) {
        if (frame.image != nullptr) dddvr::androidimage::imageDelete(frame.image);
        return false;
    }
    const int width = frame.width;
    const int height = frame.height;
    const auto transfer = frame.transfer;
    const auto primaries = frame.primaries;
    const auto range = frame.range;
    const bool imported = ffmpegVideo_.importHardwareBuffer(std::move(frame));
    if (imported) {
        if (!ffmpegVideoFrameSeen_) {
            XR_LOGI(
                "DDDVR/FFmpegVideo",
                "FFMPEG_VIDEO_FIRST_AHB_FRAME width=%d height=%d transfer=%d primaries=%d range=%d",
                width,
                height,
                static_cast<int>(transfer),
                static_cast<int>(primaries),
                static_cast<int>(range)
            );
        }
        ffmpegVideoFrameSeen_ = true;
    }
    return imported;
}

void OpenXrRenderer::updateFfmpegSurfaceMetadata(const FfmpegVideoFrame& frame) {
    if (!ffmpegVideoEnabled_) return;

    const int previousKey = static_cast<int>(ffmpegSurfaceMetadata_.transfer)
        | (static_cast<int>(ffmpegSurfaceMetadata_.primaries) << 4)
        | (static_cast<int>(ffmpegSurfaceMetadata_.range) << 8)
        | (ffmpegSurfaceMetadata_.dolbyProfile << 12);
    const uint64_t previousHash = ffmpegSurfaceMetadata_.dolbyMetadata
        ? ffmpegSurfaceMetadata_.dolbyMetadata->mappingHash
        : 0;

    ffmpegSurfaceMetadata_.width = frame.width;
    ffmpegSurfaceMetadata_.height = frame.height;
    ffmpegSurfaceMetadata_.pixelFormat = FfmpegVideoPixelFormat::P010;
    ffmpegSurfaceMetadata_.transfer = frame.transfer;
    ffmpegSurfaceMetadata_.primaries = frame.primaries;
    ffmpegSurfaceMetadata_.range = frame.range == FfmpegVideoColorRange::Unknown
        ? FfmpegVideoColorRange::Limited
        : frame.range;
    ffmpegSurfaceMetadata_.dolbyProfile = frame.dolbyProfile;
    ffmpegSurfaceMetadata_.dolbyVision = frame.dolbyVision || frame.dolbyProfile > 0;
    ffmpegSurfaceMetadata_.dolbyMetadata = frame.dolbyMetadata;
    ffmpegSurfaceMetadata_.external = true;
    ffmpegSurfaceMetadata_.valid = true;

    const int currentKey = static_cast<int>(ffmpegSurfaceMetadata_.transfer)
        | (static_cast<int>(ffmpegSurfaceMetadata_.primaries) << 4)
        | (static_cast<int>(ffmpegSurfaceMetadata_.range) << 8)
        | (ffmpegSurfaceMetadata_.dolbyProfile << 12);
    const uint64_t currentHash = ffmpegSurfaceMetadata_.dolbyMetadata
        ? ffmpegSurfaceMetadata_.dolbyMetadata->mappingHash
        : 0;
    if (currentKey != previousKey || currentHash != previousHash) {
        const uint64_t currentRevision = ffmpegSurfaceMetadata_.dolbyMetadata
            ? ffmpegSurfaceMetadata_.dolbyMetadata->revision
            : 0;
        XR_LOGI(
            "DDDVR/OpenXRColor",
            "XR_SURFACE_COLOR_METADATA size=%dx%d transfer=%d primaries=%d range=%d dolby=%d doviProfile=%d doviMetadata=%d revision=%llu hash=%llu",
            frame.width,
            frame.height,
            static_cast<int>(ffmpegSurfaceMetadata_.transfer),
            static_cast<int>(ffmpegSurfaceMetadata_.primaries),
            static_cast<int>(ffmpegSurfaceMetadata_.range),
            ffmpegSurfaceMetadata_.dolbyVision ? 1 : 0,
            ffmpegSurfaceMetadata_.dolbyProfile,
            ffmpegSurfaceMetadata_.dolbyMetadata ? 1 : 0,
            static_cast<unsigned long long>(currentRevision),
            static_cast<unsigned long long>(currentHash)
        );
    }
}

void OpenXrRenderer::setFfmpegVideoEnabled(bool enabled) {
    if (ffmpegVideoEnabled_ == enabled) return;
    ffmpegVideoEnabled_ = enabled;
    ffmpegVideoFrameSeen_ = false;
    ffmpegSurfaceMetadata_ = {};
    if (!enabled) {
        ffmpegVideo_.destroy();
    }
    XR_LOGI("DDDVR/FFmpegVideo", "FFMPEG_VIDEO_RENDER_PATH enabled=%d", enabled ? 1 : 0);
}

void OpenXrRenderer::setUiState(
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
    if (durationMs < 0) durationMs = 0;
    if (positionMs < 0) positionMs = 0;
    if (durationMs > 0 && positionMs > durationMs) positionMs = durationMs;
    if (bufferedPositionMs < 0) bufferedPositionMs = 0;
    if (durationMs > 0 && bufferedPositionMs > durationMs) bufferedPositionMs = durationMs;
    const int progressPermille = durationMs > 0
        ? static_cast<int>((positionMs * 1000) / durationMs)
        : 0;
    uiVisible_.store(visible);
    uiProgressPermille_.store(progressPermille);
    uiPlaying_.store(playing);

    VrPlayerUiState state{};
    state.visible = visible;
    state.playing = playing;
    state.buffering = buffering;
    state.positionMs = positionMs;
    state.durationMs = durationMs;
    state.bufferedPositionMs = bufferedPositionMs;
    state.title = title;
    state.projectionModeLabel = stereoModeLabel;
    state.audioTrackLabel = audioTrackLabel;
    state.audioTrackLabels = audioTrackLabels;
    state.selectedAudioTrackIndex = selectedAudioTrackIndex;
    screen_.setColorControls(1.f, 1.f, 1.f, 1.f);
    screen_.setHdrLookEnabled(false);
    for (size_t i = 0; i < audioTrackLabels.size(); ++i) {
        VrTrackRow row{};
        row.id = "legacy_audio:" + std::to_string(i);
        row.title = audioTrackLabels[i];
        row.selected = static_cast<int>(i) == selectedAudioTrackIndex;
        state.audioTracks.push_back(std::move(row));
    }
    uiModalOpen_.store(state.activeModal != VrPlayerModal::None);
    std::lock_guard<std::mutex> lock(playerPanelMutex_);
    playerPanel_.setState(state);
}

void OpenXrRenderer::setPlayerUiState(const VrPlayerUiState& state) {
    uiVisible_.store(state.visible);
    uiPlaying_.store(state.playing);
    uiModalOpen_.store(state.activeModal != VrPlayerModal::None);
    const float brightness = std::clamp(state.display.brightness, 0.45f, 1.45f);
    screen_.setHdrLookEnabled(state.display.hdrVideo);
    const bool dimmedProfile = state.display.enhanceVideo && brightness < 0.95f;
    if (state.display.hdrVideo) {
        screen_.setColorControls(brightness, 1.00f, 1.04f, 1.02f);
    } else if (dimmedProfile) {
        screen_.setColorControls(brightness, 1.02f, 1.09f, 1.10f);
    } else if (state.display.enhanceVideo) {
        screen_.setColorControls(brightness, 1.06f, 1.10f, 1.01f);
    } else {
        screen_.setColorControls(brightness, 1.f, 1.f, 1.f);
    }
    const int progressPermille = state.durationMs > 0
        ? static_cast<int>((std::clamp<int64_t>(state.positionMs, 0, state.durationMs) * 1000) / state.durationMs)
        : 0;
    uiProgressPermille_.store(std::clamp(progressPermille, 0, 1000));
    std::lock_guard<std::mutex> lock(playerPanelMutex_);
    playerPanel_.setState(state);
}

void OpenXrRenderer::setPointerRays(const OpenXrPointerRay rays[2]) {
    if (rays == nullptr) {
        pointerRays_[0].active = false;
        pointerRays_[1].active = false;
        uiRayHits_[0] = {};
        uiRayHits_[1] = {};
        screenRayHits_[0] = {};
        screenRayHits_[1] = {};
        activeUiHit_ = {};
        activeUiPointerHand_ = -1;
        uiPanelDragCandidateActive_ = false;
        uiPanelDragCandidateHand_ = -1;
        uiPanelDragActive_ = false;
        uiPanelDragHand_ = -1;
        return;
    }
    pointerRays_[0] = rays[0];
    pointerRays_[1] = rays[1];
}

void OpenXrRenderer::updateUiInteraction(
    const OpenXrPointerRay rays[2],
    const bool triggerPressed[2],
    bool active
) {
    uiRayHits_[0] = {};
    uiRayHits_[1] = {};
    screenRayHits_[0] = {};
    screenRayHits_[1] = {};
    activeUiHit_ = {};
    activeUiPointerHand_ = -1;
    uiPrimaryPressed_ = false;
    if (!uiBackend_.initialized() || rays == nullptr || !active) {
        uiBackend_.setPointerVisible(false);
        uiBackend_.setPrimaryButton(false);
        uiPanelDragCandidateActive_ = false;
        uiPanelDragCandidateHand_ = -1;
        uiPanelDragActive_ = false;
        uiPanelDragHand_ = -1;
        return;
    }

    const VrUiPlane plane = currentUiPlane();
    VrRayHit rawUiHits[2]{};
    VrRayHit dragPlaneHits[2]{};
    const bool modalOpen = uiModalOpen_.load();
    for (int hand = 0; hand < 2; ++hand) {
        if (!rays[hand].active) continue;
        rawUiHits[hand] = rayInteractor_.hitTest(
            rays[hand].pose,
            plane,
            uiBackend_.width(),
            uiBackend_.height(),
            hand
        );
        dragPlaneHits[hand] = hitUiPlaneUnbounded(
            rays[hand].pose,
            plane,
            hand,
            uiBackend_.width(),
            uiBackend_.height()
        );
        uiRayHits_[hand] = rawUiHits[hand];
        if (uiRayHits_[hand].hit &&
            !isPlayerUiPixelInteractive(
                uiRayHits_[hand].pixelX,
                uiRayHits_[hand].pixelY,
                modalOpen
            )) {
            uiRayHits_[hand] = {};
        }
        screenRayHits_[hand] = screenHitTest(rays[hand].pose, hand);
    }

    if (triggerPressed != nullptr) {
        for (int hand = 0; hand < 2; ++hand) {
            const bool triggerDown = triggerPressed[hand];
            const bool closePressed =
                triggerDown &&
                rawUiHits[hand].hit &&
                isPlayerUiModalClosePixel(rawUiHits[hand].pixelX, rawUiHits[hand].pixelY, modalOpen);
            if (closePressed && !uiClosePressed_[hand]) {
                VrPlayerPanelAction action{};
                action.type = VrPlayerPanelActionType::CloseModal;
                pendingPlayerPanelActions_.push_back(std::move(action));
                uiAutoHideFrameBudget_ = kUiAutoHideFrames;
                XR_LOGI(
                    "DDDVR/OpenXRUi",
                    "XR_UI_ACTION modal_close_direct hand=%d x=%.1f y=%.1f",
                    hand,
                    rawUiHits[hand].pixelX,
                    rawUiHits[hand].pixelY
                );
            }
            // Latch the physical trigger, not the hover result. A trigger held
            // while opening a modal must not become a fresh close click on the
            // next frame when the modal hit boxes appear under the ray.
            uiClosePressed_[hand] = triggerDown;
        }
    } else {
        uiClosePressed_[0] = false;
        uiClosePressed_[1] = false;
    }

    int selectedHand = -1;
    if (uiPanelDragActive_) {
        const int hand = uiPanelDragHand_;
        const bool keepDragging = hand >= 0 &&
            hand < 2 &&
            rays[hand].active &&
            dragPlaneHits[hand].hit &&
            triggerPressed != nullptr &&
            triggerPressed[hand];
        if (keepDragging) {
            const float dxWorld = dragPlaneHits[hand].worldX - uiPanelDragStartWorldX_;
            const float dyWorld = dragPlaneHits[hand].worldY - uiPanelDragStartWorldY_;
            const float dzWorld = dragPlaneHits[hand].worldZ - uiPanelDragStartWorldZ_;
            const float localX =
                dxWorld * plane.right.x +
                dyWorld * plane.right.y +
                dzWorld * plane.right.z;
            const float localY =
                dxWorld * plane.up.x +
                dyWorld * plane.up.y +
                dzWorld * plane.up.z;
            uiPanelOffsetX_ = clampFloat(
                uiPanelDragStartOffsetX_ + localX,
                -kUiPanelMaxOffsetXMeters,
                kUiPanelMaxOffsetXMeters
            );
            uiPanelOffsetY_ = clampFloat(
                uiPanelDragStartOffsetY_ + localY,
                -kUiPanelMaxOffsetYMeters,
                kUiPanelMaxOffsetYMeters
            );
            selectedHand = hand;
            uiRayHits_[hand] = rawUiHits[hand].hit ? rawUiHits[hand] : dragPlaneHits[hand];
            uiAutoHideFrameBudget_ = kUiAutoHideFrames;
            static uint32_t dragMoveLogCount = 0;
            dragMoveLogCount += 1;
            if (dragMoveLogCount <= 6 || dragMoveLogCount % 30 == 0) {
                XR_LOGI(
                    "DDDVR/OpenXRUi",
                    "XR_UI_PANEL_DRAG_MOVE x=%.3f y=%.3f localX=%.3f localY=%.3f",
                    uiPanelOffsetX_,
                    uiPanelOffsetY_,
                    localX,
                    localY
                );
            }
        } else {
            XR_LOGI(
                "DDDVR/OpenXRUi",
                "XR_UI_PANEL_DRAG_END x=%.3f y=%.3f",
                uiPanelOffsetX_,
                uiPanelOffsetY_
            );
            uiPanelDragActive_ = false;
            uiPanelDragHand_ = -1;
            uiPanelDragCandidateActive_ = false;
            uiPanelDragCandidateHand_ = -1;
        }
    }
    if (!uiPanelDragActive_ && uiPanelDragCandidateActive_) {
        const int hand = uiPanelDragCandidateHand_;
        const bool keepCandidate = hand >= 0 &&
            hand < 2 &&
            rays[hand].active &&
            dragPlaneHits[hand].hit &&
            triggerPressed != nullptr &&
            triggerPressed[hand];
        if (keepCandidate) {
            const float dxWorld = dragPlaneHits[hand].worldX - uiPanelDragStartWorldX_;
            const float dyWorld = dragPlaneHits[hand].worldY - uiPanelDragStartWorldY_;
            const float dzWorld = dragPlaneHits[hand].worldZ - uiPanelDragStartWorldZ_;
            const float localX =
                dxWorld * plane.right.x +
                dyWorld * plane.right.y +
                dzWorld * plane.right.z;
            const float localY =
                dxWorld * plane.up.x +
                dyWorld * plane.up.y +
                dzWorld * plane.up.z;
            const float moved = std::sqrt(localX * localX + localY * localY);
            if (moved >= kUiPanelDragStartThresholdMeters) {
                uiPanelDragActive_ = true;
                uiPanelDragHand_ = hand;
                XR_LOGI(
                    "DDDVR/OpenXRUi",
                    "XR_UI_PANEL_DRAG_BEGIN hand=%d x=%.1f y=%.1f offsetX=%.3f offsetY=%.3f moved=%.3f",
                    hand,
                    uiPanelDragStartPixelX_,
                    uiPanelDragStartPixelY_,
                    uiPanelOffsetX_,
                    uiPanelOffsetY_,
                    moved
                );
            }
        } else {
            uiPanelDragCandidateActive_ = false;
            uiPanelDragCandidateHand_ = -1;
        }
    }
    if (!uiPanelDragActive_ && !uiPanelDragCandidateActive_ && triggerPressed != nullptr) {
        for (int hand = 0; hand < 2; ++hand) {
            if (!triggerPressed[hand] || !rawUiHits[hand].hit) continue;
            if (!isPlayerUiDragHandlePixel(rawUiHits[hand].pixelX, rawUiHits[hand].pixelY, modalOpen)) continue;
            uiPanelDragCandidateActive_ = true;
            uiPanelDragCandidateHand_ = hand;
            uiPanelDragStartPixelX_ = rawUiHits[hand].pixelX;
            uiPanelDragStartPixelY_ = rawUiHits[hand].pixelY;
            uiPanelDragStartOffsetX_ = uiPanelOffsetX_;
            uiPanelDragStartOffsetY_ = uiPanelOffsetY_;
            uiPanelDragStartWorldX_ = rawUiHits[hand].worldX;
            uiPanelDragStartWorldY_ = rawUiHits[hand].worldY;
            uiPanelDragStartWorldZ_ = rawUiHits[hand].worldZ;
            selectedHand = hand;
            uiRayHits_[hand] = rawUiHits[hand];
            uiAutoHideFrameBudget_ = kUiAutoHideFrames;
            XR_LOGI(
                "DDDVR/OpenXRUi",
                "XR_UI_PANEL_DRAG_CANDIDATE hand=%d x=%.1f y=%.1f offsetX=%.3f offsetY=%.3f",
                hand,
                rawUiHits[hand].pixelX,
                rawUiHits[hand].pixelY,
                uiPanelOffsetX_,
                uiPanelOffsetY_
            );
            break;
        }
    }

    for (int hand = 0; hand < 2; ++hand) {
        if (triggerPressed != nullptr && triggerPressed[hand] && uiRayHits_[hand].hit) {
            selectedHand = hand;
            break;
        }
    }
    if (selectedHand < 0 && uiRayHits_[1].hit) selectedHand = 1;
    if (selectedHand < 0 && uiRayHits_[0].hit) selectedHand = 0;

    if (selectedHand >= 0) {
        activeUiHit_ = uiRayHits_[selectedHand];
        activeUiPointerHand_ = selectedHand;
        const bool suppressUiPress =
            uiPanelDragActive_ &&
            selectedHand == uiPanelDragHand_ &&
            triggerPressed != nullptr &&
            triggerPressed[selectedHand];
        uiPrimaryPressed_ = triggerPressed != nullptr && triggerPressed[selectedHand] && !suppressUiPress;
        uiBackend_.setPointerVisible(true);
        uiBackend_.setPointerPixel(activeUiHit_.pixelX, activeUiHit_.pixelY);
        uiBackend_.setPrimaryButton(uiPrimaryPressed_);
        static uint32_t pointerHitLogCount = 0;
        pointerHitLogCount += 1;
        if (uiPrimaryPressed_ || pointerHitLogCount <= 8 || pointerHitLogCount % 90 == 0) {
            XR_LOGI(
                "DDDVR/OpenXRUi",
                "XR_UI_POINTER_HIT hand=%d x=%.1f y=%.1f pressed=%d",
                selectedHand,
                activeUiHit_.pixelX,
                activeUiHit_.pixelY,
                uiPrimaryPressed_ ? 1 : 0
            );
        }
    } else {
        uiBackend_.setPointerVisible(false);
        uiBackend_.setPrimaryButton(false);
    }
}

bool OpenXrRenderer::consumeUiInputAction(OpenXrInputActionCode* outAction) {
    if (outAction == nullptr) return false;
    if (pendingUiPlayPause_) {
        pendingUiPlayPause_ = false;
        *outAction = OpenXrInputActionCode::PlayPause;
        return true;
    }
    if (pendingUiSeekBack_) {
        pendingUiSeekBack_ = false;
        *outAction = OpenXrInputActionCode::SeekBack;
        return true;
    }
    if (pendingUiSeekForward_) {
        pendingUiSeekForward_ = false;
        *outAction = OpenXrInputActionCode::SeekForward;
        return true;
    }
    if (pendingUiExit_) {
        pendingUiExit_ = false;
        *outAction = OpenXrInputActionCode::Exit;
        return true;
    }
    return false;
}

bool OpenXrRenderer::consumeUiTimelineSeek(int* outProgressPermille) {
    if (!pendingUiTimelineSeek_ || outProgressPermille == nullptr) return false;
    pendingUiTimelineSeek_ = false;
    *outProgressPermille = pendingUiTimelineProgressPermille_;
    return true;
}

bool OpenXrRenderer::consumeUiAudioTrackSelection(int* outTrackIndex) {
    if (!pendingUiAudioTrackSelected_ || outTrackIndex == nullptr) return false;
    pendingUiAudioTrackSelected_ = false;
    *outTrackIndex = pendingUiAudioTrackIndex_;
    pendingUiAudioTrackIndex_ = -1;
    return true;
}

bool OpenXrRenderer::consumePlayerPanelAction(VrPlayerPanelAction* outAction) {
    if (outAction == nullptr || pendingPlayerPanelActions_.empty()) return false;
    *outAction = pendingPlayerPanelActions_.front();
    pendingPlayerPanelActions_.erase(pendingPlayerPanelActions_.begin());
    return true;
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

bool OpenXrRenderer::updateScreenGrab(
    bool active,
    const XrPosef& gripPose,
    float rayDistanceDeltaMeters,
    bool directTranslationMode
) {
    if (!active) {
        if (screenGrabActive_) {
            XR_LOGI("DDDVR/OpenXRRenderer", "XR_SCREEN_GRAB_END");
        }
        screenGrabActive_ = false;
        screenGrabDirectMode_ = false;
        return false;
    }

    if (screenGrabActive_ && screenGrabDirectMode_ != directTranslationMode) {
        XR_LOGI(
            "DDDVR/OpenXRRenderer",
            "XR_SCREEN_GRAB_MODE_SWITCH old=%d new=%d",
            screenGrabDirectMode_ ? 1 : 0,
            directTranslationMode ? 1 : 0
        );
        screenGrabActive_ = false;
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
        screenGrabDirectMode_ = directTranslationMode;
        grabStartPose_ = gripPose;
        grabStartCenterX_ = screenCenterX_;
        grabStartCenterY_ = screenCenterY_;
        grabStartCenterZ_ = screenCenterZ_;
        grabStartYawRadians_ = screenYawRadians_;
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
        if (screenGrabDirectMode_) {
            grabStartRayDistanceMeters_ = horizontalDistance(screenCenterX_, screenCenterZ_);
            grabStartRayDistanceMeters_ = clampFloat(
                grabStartRayDistanceMeters_,
                kMinScreenDistanceMeters,
                kMaxScreenDistanceMeters
            );
        }
        grabStartOffsetX_ = screenCenterX_ - (gripPose.position.x + direction[0] * grabStartRayDistanceMeters_);
        grabStartOffsetY_ = screenCenterY_ - (gripPose.position.y + direction[1] * grabStartRayDistanceMeters_);
        grabStartOffsetZ_ = screenCenterZ_ - (gripPose.position.z + direction[2] * grabStartRayDistanceMeters_);
        XR_LOGI(
            "DDDVR/OpenXRRenderer",
            "XR_SCREEN_GRAB_BEGIN mode=%s x=%.2f y=%.2f z=%.2f rayDistance=%.2f",
            screenGrabDirectMode_ ? "direct" : "ray",
            screenCenterX_,
            screenCenterY_,
            screenCenterZ_,
            grabStartRayDistanceMeters_
        );
        return false;
    }

    if (screenGrabDirectMode_) {
        float targetX = grabStartCenterX_ +
            (gripPose.position.x - grabStartPose_.position.x) * kDirectGrabTranslationScale;
        float targetY = grabStartCenterY_ +
            (gripPose.position.y - grabStartPose_.position.y) * kDirectGrabTranslationScale;
        float targetZ = grabStartCenterZ_ +
            (gripPose.position.z - grabStartPose_.position.z) * kDirectGrabTranslationScale;

        if (std::fabs(rayDistanceDeltaMeters) > 0.0001f) {
            grabStartRayDistanceMeters_ = clampFloat(
                grabStartRayDistanceMeters_ + rayDistanceDeltaMeters,
                kMinScreenDistanceMeters,
                kMaxScreenDistanceMeters
            );
            screenHighlightFrameBudget_ = 30;
            static uint32_t directDistanceLogCount = 0;
            directDistanceLogCount += 1;
            if (directDistanceLogCount <= 10 || directDistanceLogCount % 30 == 0) {
                XR_LOGI(
                    "DDDVR/OpenXRRenderer",
                    "XR_SCREEN_GRAB_DIRECT_DISTANCE distance=%.2f delta=%.3f",
                    grabStartRayDistanceMeters_,
                    rayDistanceDeltaMeters
                );
            }
        }
        const float targetDistance = horizontalDistance(targetX, targetZ);
        if (targetDistance > 0.001f) {
            const float scale = grabStartRayDistanceMeters_ / targetDistance;
            targetX *= scale;
            targetZ *= scale;
        }

        const float targetYaw = std::atan2(targetX, -targetZ);
        const float moveFromStartX = targetX - grabStartCenterX_;
        const float moveFromStartY = targetY - grabStartCenterY_;
        const float moveFromStartZ = targetZ - grabStartCenterZ_;
        const float movedMeters = std::sqrt(
            moveFromStartX * moveFromStartX +
            moveFromStartY * moveFromStartY +
            moveFromStartZ * moveFromStartZ
        );
        const bool consumed = movedMeters > kDirectGrabConsumedMoveThresholdMeters ||
            std::fabs(normalizeRadians(targetYaw - grabStartYawRadians_)) > kGrabConsumedYawThresholdRadians;

        screenCenterX_ += (targetX - screenCenterX_) * kDirectGrabFollowSmoothing;
        screenCenterY_ += (targetY - screenCenterY_) * kDirectGrabFollowSmoothing;
        screenCenterZ_ += (targetZ - screenCenterZ_) * kDirectGrabFollowSmoothing;
        screenYawRadians_ += normalizeRadians(targetYaw - screenYawRadians_) * kDirectGrabFollowSmoothing;
        screenYawRadians_ = normalizeRadians(screenYawRadians_);
        clampScreenCenter();
        applyScreenPlacement();

        static uint32_t directGrabLogCount = 0;
        directGrabLogCount += 1;
        if (directGrabLogCount <= 12 || directGrabLogCount % 30 == 0) {
            XR_LOGI(
                "DDDVR/OpenXRRenderer",
                "XR_SCREEN_GRAB_DIRECT_MOVE x=%.2f y=%.2f z=%.2f yaw=%.3f dx=%.2f dy=%.2f dz=%.2f consumed=%d",
                screenCenterX_,
                screenCenterY_,
                screenCenterZ_,
                screenYawRadians_,
                gripPose.position.x - grabStartPose_.position.x,
                gripPose.position.y - grabStartPose_.position.y,
                gripPose.position.z - grabStartPose_.position.z,
                consumed ? 1 : 0
            );
        }
        return consumed;
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
    const float halfHeight = screenHeightMeters() * 0.5f;
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
        const float halfHeight = screenHeightMeters() * 0.5f;
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
    const float currentDistance = horizontalDistance(screenCenterX_, screenCenterZ_);
    const float baseDistance = currentDistance > 0.001f ? currentDistance : screenDistanceMeters_;
    const float nextDistance = clampFloat(
        baseDistance + deltaMeters,
        kMinScreenDistanceMeters,
        kMaxScreenDistanceMeters
    );
    if (currentDistance > 0.001f) {
        const float scale = nextDistance / currentDistance;
        screenCenterX_ *= scale;
        screenCenterZ_ *= scale;
    } else {
        screenCenterX_ = std::sin(screenYawRadians_) * nextDistance;
        screenCenterZ_ = -std::cos(screenYawRadians_) * nextDistance;
    }
    screenDistanceMeters_ = nextDistance;
    applyScreenPlacement();
    static uint32_t distanceLogCount = 0;
    distanceLogCount += 1;
    if (distanceLogCount <= 10 || distanceLogCount % 30 == 0) {
        XR_LOGI(
            "DDDVR/OpenXRRenderer",
            "XR_SCREEN_DISTANCE x=%.2f z=%.2f distance=%.2f delta=%.3f",
            screenCenterX_,
            screenCenterZ_,
            screenDistanceMeters_,
            deltaMeters
        );
    }
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
    uiPanelOffsetX_ = 0.f;
    uiPanelOffsetY_ = 0.f;
    uiPanelDragActive_ = false;
    uiPanelDragHand_ = -1;
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

VrUiPlane OpenXrRenderer::targetUiPlane() const {
    const float screenHeight = screenHeightMeters();
    const float c = std::cos(screenYawRadians_);
    const float s = std::sin(screenYawRadians_);
    const bool modalOpen = uiModalOpen_.load();
    const float forwardOffset = modalOpen ? kImGuiPanelModalForwardOffsetMeters : kImGuiPanelForwardOffsetMeters;
    const float downScale = modalOpen ? kImGuiPanelModalDownScale : kImGuiPanelDownScale;
    VrUiPlane plane{};
    plane.right = {c, 0.f, s};
    plane.up = {0.f, 1.f, 0.f};
    plane.normal = {-s, 0.f, c};
    plane.center.x = screenCenterX_ + plane.normal.x * forwardOffset;
    plane.center.y = screenCenterY_ - screenHeight * downScale;
    plane.center.z = screenCenterZ_ + plane.normal.z * forwardOffset;
    plane.yawRadians = screenYawRadians_;
    const float desiredPanelWidth = config_.screenWidthMeters * kImGuiPanelWidthScale;
    plane.widthMeters = desiredPanelWidth < kImGuiPanelMaxWidthMeters
        ? desiredPanelWidth
        : kImGuiPanelMaxWidthMeters;
    plane.heightMeters = modalOpen ? kImGuiPanelModalHeightMeters : kImGuiPanelHeightMeters;
    plane.center.x += plane.right.x * uiPanelOffsetX_ + plane.up.x * uiPanelOffsetY_;
    plane.center.y += plane.right.y * uiPanelOffsetX_ + plane.up.y * uiPanelOffsetY_;
    plane.center.z += plane.right.z * uiPanelOffsetX_ + plane.up.z * uiPanelOffsetY_;
    return plane;
}

VrUiPlane OpenXrRenderer::currentScreenPlane() const {
    const float c = std::cos(screenYawRadians_);
    const float s = std::sin(screenYawRadians_);
    VrUiPlane plane{};
    plane.center = {screenCenterX_, screenCenterY_, screenCenterZ_};
    plane.right = {c, 0.f, s};
    plane.up = {0.f, 1.f, 0.f};
    plane.normal = {-s, 0.f, c};
    plane.yawRadians = screenYawRadians_;
    plane.widthMeters = config_.screenWidthMeters;
    plane.heightMeters = screenHeightMeters();
    return plane;
}

VrRayHit OpenXrRenderer::screenHitTest(const XrPosef& aimPose, int hand) const {
    VrRayHit out{};
    out.hand = hand;
    const VrUiPlane plane = currentScreenPlane();
    const auto direction = rotateByQuat(aimPose.orientation, 0.f, 0.f, -1.f);
    const float directionLength = std::sqrt(
        direction[0] * direction[0] +
        direction[1] * direction[1] +
        direction[2] * direction[2]
    );
    if (directionLength <= 0.001f) return out;
    const float dirX = direction[0] / directionLength;
    const float dirY = direction[1] / directionLength;
    const float dirZ = direction[2] / directionLength;
    const float denom =
        dirX * plane.normal.x +
        dirY * plane.normal.y +
        dirZ * plane.normal.z;
    if (std::fabs(denom) <= 0.001f) return out;
    const float toPlaneX = plane.center.x - aimPose.position.x;
    const float toPlaneY = plane.center.y - aimPose.position.y;
    const float toPlaneZ = plane.center.z - aimPose.position.z;
    const float t =
        (toPlaneX * plane.normal.x +
         toPlaneY * plane.normal.y +
         toPlaneZ * plane.normal.z) / denom;
    if (t <= 0.f) return out;

    const float hitX = aimPose.position.x + dirX * t;
    const float hitY = aimPose.position.y + dirY * t;
    const float hitZ = aimPose.position.z + dirZ * t;
    const float deltaX = hitX - plane.center.x;
    const float deltaY = hitY - plane.center.y;
    const float deltaZ = hitZ - plane.center.z;
    const float localX =
        deltaX * plane.right.x +
        deltaY * plane.right.y +
        deltaZ * plane.right.z;
    const float localY =
        deltaX * plane.up.x +
        deltaY * plane.up.y +
        deltaZ * plane.up.z;
    if (std::fabs(localX) > plane.widthMeters * 0.5f ||
        std::fabs(localY) > plane.heightMeters * 0.5f) {
        return out;
    }
    out.hit = true;
    out.worldX = hitX;
    out.worldY = hitY;
    out.worldZ = hitZ;
    out.pixelX = (localX / plane.widthMeters + 0.5f) * static_cast<float>(kImGuiUiTextureWidth);
    out.pixelY = (0.5f - localY / plane.heightMeters) * static_cast<float>(kImGuiUiTextureHeight);
    return out;
}

void OpenXrRenderer::updateUiPlane(float deltaSeconds) {
    const VrUiPlane target = targetUiPlane();
    if (!uiPlaneInitialized_) {
        uiPlane_ = target;
        uiPlaneInitialized_ = true;
        return;
    }
    if (deltaSeconds < 0.f) deltaSeconds = 0.f;
    if (deltaSeconds > 0.1f) deltaSeconds = 0.1f;
    const float response = screenGrabActive_ ? 22.0f : kImGuiPanelFollowResponse;
    const float k = 1.f - std::exp(-deltaSeconds * response);
    uiPlane_.center.x += (target.center.x - uiPlane_.center.x) * k;
    uiPlane_.center.y += (target.center.y - uiPlane_.center.y) * k;
    uiPlane_.center.z += (target.center.z - uiPlane_.center.z) * k;
    uiPlane_.yawRadians += normalizeRadians(target.yawRadians - uiPlane_.yawRadians) * k;
    uiPlane_.yawRadians = normalizeRadians(uiPlane_.yawRadians);
    const float c = std::cos(uiPlane_.yawRadians);
    const float s = std::sin(uiPlane_.yawRadians);
    uiPlane_.right = {c, 0.f, s};
    uiPlane_.up = {0.f, 1.f, 0.f};
    uiPlane_.normal = {-s, 0.f, c};
    uiPlane_.widthMeters = target.widthMeters;
    uiPlane_.heightMeters = target.heightMeters;
}

void OpenXrRenderer::updateUiTexture() {
    if (!uiBackend_.initialized()) return;
    const auto now = std::chrono::steady_clock::now();
    float deltaSeconds = 1.f / 60.f;
    if (lastUiFrameTime_.time_since_epoch().count() != 0) {
        deltaSeconds = std::chrono::duration<float>(now - lastUiFrameTime_).count();
    }
    lastUiFrameTime_ = now;
    updateUiPlane(deltaSeconds);
    uiBackend_.beginFrame(deltaSeconds);
    std::lock_guard<std::mutex> lock(playerPanelMutex_);
    playerPanel_.draw();
    queuePlayerPanelActions();
    uiBackend_.endFrame();
}

VrUiPlane OpenXrRenderer::currentUiPlane() const {
    return uiPlaneInitialized_ ? uiPlane_ : targetUiPlane();
}

void OpenXrRenderer::queuePlayerPanelActions() {
    if (playerPanel_.consumePlayPauseRequested()) {
        pendingUiPlayPause_ = true;
        XR_LOGI("DDDVR/OpenXRUi", "XR_UI_ACTION play_pause");
    }
    if (playerPanel_.consumeSeekBackRequested()) {
        pendingUiSeekBack_ = true;
        XR_LOGI("DDDVR/OpenXRUi", "XR_UI_ACTION seek_back");
    }
    if (playerPanel_.consumeSeekForwardRequested()) {
        pendingUiSeekForward_ = true;
        XR_LOGI("DDDVR/OpenXRUi", "XR_UI_ACTION seek_forward");
    }
    if (playerPanel_.consumeExitRequested()) {
        pendingUiExit_ = true;
        XR_LOGI("DDDVR/OpenXRUi", "XR_UI_ACTION exit");
    }
    int progressPermille = 0;
    if (playerPanel_.consumeTimelineSeekRequested(&progressPermille)) {
        if (progressPermille < 0) progressPermille = 0;
        if (progressPermille > 1000) progressPermille = 1000;
        const auto now = std::chrono::steady_clock::now();
        const bool firstSeek = lastUiTimelineSeekQueued_.time_since_epoch().count() == 0;
        const bool timeElapsed =
            firstSeek || now - lastUiTimelineSeekQueued_ >= std::chrono::milliseconds(90);
        const bool movedEnough =
            lastUiTimelineQueuedProgressPermille_ < 0 ||
            std::abs(progressPermille - lastUiTimelineQueuedProgressPermille_) >= 4;
        if (timeElapsed || movedEnough) {
            pendingUiTimelineProgressPermille_ = progressPermille;
            pendingUiTimelineSeek_ = true;
            lastUiTimelineQueuedProgressPermille_ = progressPermille;
            lastUiTimelineSeekQueued_ = now;
            XR_LOGI("DDDVR/OpenXRUi", "XR_UI_ACTION seek_to progress=%d", progressPermille);
        }
    }
    int audioTrackIndex = -1;
    if (playerPanel_.consumeAudioTrackSelected(&audioTrackIndex)) {
        pendingUiAudioTrackIndex_ = audioTrackIndex;
        pendingUiAudioTrackSelected_ = true;
        XR_LOGI("DDDVR/OpenXRUi", "XR_UI_ACTION audio_track index=%d", audioTrackIndex);
    }
    VrPlayerPanelAction action{};
    while (playerPanel_.consumeAction(&action)) {
        pendingPlayerPanelActions_.push_back(action);
        XR_LOGI(
            "DDDVR/OpenXRUi",
            "XR_UI_ACTION panel type=%d int=%d float=%.3f text=%s",
            static_cast<int>(action.type),
            action.intValue,
            action.floatValue,
            action.stringValue.c_str()
        );
    }
}

void OpenXrRenderer::renderUiCursor(const float* mvp, const VrRayHit& hit, const VrUiPlane& plane) {
    if (!hit.hit) return;
    const float size = uiPrimaryPressed_ ? 0.036f : 0.026f;
    const float center[3] = {hit.worldX, hit.worldY, hit.worldZ};
    const float right[3] = {plane.right.x, plane.right.y, plane.right.z};
    const float up[3] = {plane.up.x, plane.up.y, plane.up.z};
    screen_.renderCursorDot(mvp, center, right, up, size, uiPrimaryPressed_);
}

void OpenXrRenderer::renderEye(int eye, int width, int height, const XrView& view){
    glViewport(0,0,width,height);
    glClearColor(0.f,0.f,0.f,1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const auto projection = projectionFromFov(view.fov);
    const auto viewMatrix = viewFromPose(view.pose);
    const auto mvp = multiply(projection, viewMatrix);
    if (ffmpegVideoEnabled_ && ffmpegVideo_.hasFrame()) {
        const auto& ffmpegTextures = ffmpegVideo_.textureSet();
        if (ffmpegTextures.external && ffmpegTextures.externalTexture != 0) {
            static constexpr float kHardwareBufferTextureMatrix[16] = {
                1.f, 0.f, 0.f, 0.f,
                0.f, -1.f, 0.f, 0.f,
                0.f, 0.f, 1.f, 0.f,
                0.f, 1.f, 0.f, 1.f
            };
            screen_.renderVideo(
                ffmpegTextures.externalTexture,
                mvp.data(),
                kHardwareBufferTextureMatrix,
                uvRectForEye(eye),
                true,
                0.f,
                0.f,
                0.f,
                &ffmpegTextures
            );
        } else {
            screen_.renderFfmpegVideo(
                ffmpegTextures,
                mvp.data(),
                uvRectForEye(eye),
                true,
                0.f,
                0.f,
                0.f
            );
        }
    } else {
        const FfmpegVideoTextureSet* surfaceMetadata =
            ffmpegVideoEnabled_ && ffmpegSurfaceMetadata_.valid
                ? &ffmpegSurfaceMetadata_
                : nullptr;
        screen_.renderVideo(
            video_.id(),
            mvp.data(),
            videoTransform_,
            uvRectForEye(eye),
            hasVideoFrame_,
            0.f,
            0.f,
            0.f,
            surfaceMetadata
        );
    }
#if defined(DDDVR_LEGACY_PRIMITIVE_UI)
    {
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
    }
#else
    if (uiVisible_.load() && uiBackend_.initialized()) {
        if (eye == 0) {
            updateUiTexture();
        }
        uiBackend_.renderPanelQuad(mvp.data(), currentUiPlane());
    }
#endif
    if (screenGrabActive_ || screenHighlightFrameBudget_ > 0) {
        screen_.renderGrabHighlight(mvp.data());
        if (eye == 1 && screenHighlightFrameBudget_ > 0) {
            screenHighlightFrameBudget_ -= 1;
        }
    }
    for (int hand = 0; hand < 2; ++hand) {
        const auto& ray = pointerRays_[hand];
        if (!ray.active) continue;
        const auto direction = rotateByQuat(ray.pose.orientation, 0.f, 0.f, -1.f);
        const float start[3] = {
            ray.pose.position.x,
            ray.pose.position.y,
            ray.pose.position.z
        };
        const VrRayHit& rayHit = uiRayHits_[hand].hit ? uiRayHits_[hand] : screenRayHits_[hand];
        const float end[3] = {
            rayHit.hit ? rayHit.worldX : start[0] + direction[0] * 8.f,
            rayHit.hit ? rayHit.worldY : start[1] + direction[1] * 8.f,
            rayHit.hit ? rayHit.worldZ : start[2] + direction[2] * 8.f
        };
        const bool uiHit = uiRayHits_[hand].hit;
        screen_.renderRay(
            mvp.data(),
            start,
            end,
            uiHit ? 0.38f : 0.10f,
            uiHit ? 0.96f : 0.85f,
            1.0f
        );
        if (uiRayHits_[hand].hit) {
            renderUiCursor(mvp.data(), uiRayHits_[hand], currentUiPlane());
        } else if (screenRayHits_[hand].hit) {
            renderUiCursor(mvp.data(), screenRayHits_[hand], currentScreenPlane());
        }
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
        case OpenXrStereoMode::VrCamV1:
            return actualEye == 1 ? CinemaUvRect{0.0001f, 0.f, 0.5f, 1.f} : CinemaUvRect{0.5f, 0.f, 0.5f, 1.f};
        case OpenXrStereoMode::VrCamV2:
            return actualEye == 0 ? CinemaUvRect{0.0001f, 0.f, 0.5f, 1.f} : CinemaUvRect{0.5f, 0.f, 0.5f, 1.f};
        case OpenXrStereoMode::Mono:
        default:
            return CinemaUvRect{};
    }
}
