#pragma once

#include "OpenXrPlatform.h"

#include <chrono>
#include <functional>

enum class OpenXrInputActionCode {
    None = 0,
    PlayPause = 1,
    SeekBack = 2,
    SeekForward = 3,
    Recenter = 4,
    ShowMenu = 5,
    Exit = 6,
    ScreenYawLeft = 7,
    ScreenYawRight = 8,
    ScreenCloser = 9,
    ScreenFarther = 10,
    ScreenCurveLess = 11,
    ScreenCurveMore = 12
};

struct OpenXrPointerRay {
    bool active = false;
    XrPosef pose{{0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 0.f}};
};

struct OpenXrFrameControls {
    float screenYawDeltaRadians = 0.f;
    float screenDistanceDeltaMeters = 0.f;
    float screenCurveDeltaRadians = 0.f;
    int seekProgressPointerHand = -1;
    bool seekProgressFromTrigger = false;
};

class OpenXrInput {
public:
    using ActionCallback = std::function<void(OpenXrInputActionCode)>;

    bool initialize(XrInstance instance, XrSession session, ActionCallback callback);
    void sync();
    void locatePointerRays(XrSpace baseSpace, XrTime time, OpenXrPointerRay outRays[2]) const;
    void locateGripPoses(XrSpace baseSpace, XrTime time, OpenXrPointerRay outPoses[2]) const;
    OpenXrFrameControls consumeFrameControls();
    int activeGrabHand() const;
    bool anyGrabPressed() const;
    bool triggerPressed(int hand) const;
    bool shouldShowPointerRays() const;
    void markGrabMotionConsumed();
    void markTriggerTimelineConsumed(int hand);
    void markTriggerConsumedByUi(int hand);
    void destroy();

private:
    bool createAction(XrActionType type, const char* name, const char* localizedName, XrAction* action);
    bool createAimSpaces();
    bool createGripSpaces();
    void suggestBindings(const char* interactionProfile, const XrActionSuggestedBinding* bindings, uint32_t count);
    bool stringToPath(const char* path, XrPath* out) const;
    void pollBooleanAction(XrAction action, OpenXrInputActionCode code);
    void pollTrigger();
    void pollSqueeze();
    void pollThumbstick();
    void pollThumbstickClick();
    void logInteractionProfiles();
    bool shouldEmitTriggerTap(int hand, std::chrono::steady_clock::time_point now) const;
    void emit(OpenXrInputActionCode code);

    XrInstance instance_{XR_NULL_HANDLE};
    XrSession session_{XR_NULL_HANDLE};
    XrActionSet actionSet_{XR_NULL_HANDLE};
    XrAction playPauseAction_{XR_NULL_HANDLE};
    XrAction recenterAction_{XR_NULL_HANDLE};
    XrAction showMenuAction_{XR_NULL_HANDLE};
    XrAction exitAction_{XR_NULL_HANDLE};
    XrAction triggerAction_{XR_NULL_HANDLE};
    XrAction triggerClickAction_{XR_NULL_HANDLE};
    XrAction squeezeAction_{XR_NULL_HANDLE};
    XrAction squeezeClickAction_{XR_NULL_HANDLE};
    XrAction aimPoseAction_{XR_NULL_HANDLE};
    XrAction gripPoseAction_{XR_NULL_HANDLE};
    XrAction thumbstickAction_{XR_NULL_HANDLE};
    XrAction thumbstickClickAction_{XR_NULL_HANDLE};
    XrSpace aimSpaces_[2]{XR_NULL_HANDLE, XR_NULL_HANDLE};
    XrSpace gripSpaces_[2]{XR_NULL_HANDLE, XR_NULL_HANDLE};
    XrPath handSubactionPaths_[2]{XR_NULL_PATH, XR_NULL_PATH};
    ActionCallback callback_;
    int activeGrabHand_ = -1;
    int thumbstickXDirection_[2]{0, 0};
    int thumbstickYDirection_[2]{0, 0};
    bool triggerPressed_[2]{false, false};
    bool squeezePressed_[2]{false, false};
    bool thumbstickClickPressed_[2]{false, false};
    bool triggerConsumedByTimeline_[2]{false, false};
    bool triggerConsumedByUi_[2]{false, false};
    bool squeezeConsumedByMotion_[2]{false, false};
    std::chrono::steady_clock::time_point triggerPressedAt_[2]{};
    std::chrono::steady_clock::time_point lastTriggerTapEmit_[2]{};
    std::chrono::steady_clock::time_point lastTimelineSeekEmit_[2]{};
    std::chrono::steady_clock::time_point lastTriggerTimelineSeekEmit_[2]{};
    std::chrono::steady_clock::time_point lastThumbstickEmit_{};
    std::chrono::steady_clock::time_point lastFrameControlsTime_{};
    std::chrono::steady_clock::time_point lastPlayPauseEmit_{};
    std::chrono::steady_clock::time_point lastPointerActivity_{};
    XrPath currentInteractionProfiles_[2]{XR_NULL_PATH, XR_NULL_PATH};
    OpenXrFrameControls pendingFrameControls_{};
    bool initialized_ = false;
};
