#include "OpenXrInput.h"

#include "../util/XrLog.h"

#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace {
constexpr float kThumbstickPressThreshold = 0.65f;
constexpr float kThumbstickReleaseThreshold = 0.35f;
constexpr float kThumbstickMoveDeadzone = 0.18f;
constexpr float kTriggerPressThreshold = 0.72f;
constexpr float kTriggerReleaseThreshold = 0.22f;
constexpr float kSqueezePressThreshold = 0.55f;
constexpr float kSqueezeReleaseThreshold = 0.20f;
constexpr std::chrono::milliseconds kThumbstickRepeatDelay(350);
constexpr std::chrono::milliseconds kTimelineSeekDragInterval(90);
constexpr std::chrono::milliseconds kTriggerTapDebounce(550);
constexpr std::chrono::milliseconds kPlayPauseDebounce(300);
constexpr std::chrono::milliseconds kPointerIdleTimeout(1500);
constexpr float kYawSpeedRadiansPerSecond = 0.75f;
constexpr float kDistanceSpeedMetersPerSecond = 1.25f;

bool succeededOrUnsupported(XrResult result) {
    return result == XR_SUCCESS ||
           result == XR_ERROR_PATH_UNSUPPORTED ||
           result == XR_ERROR_VALIDATION_FAILURE;
}
}

bool OpenXrInput::initialize(XrInstance instance, XrSession session, ActionCallback callback) {
    instance_ = instance;
    session_ = session;
    callback_ = std::move(callback);
    if (instance_ == XR_NULL_HANDLE || session_ == XR_NULL_HANDLE) {
        XR_LOGW("DDDVR/OpenXRInput", "XR_INPUT_INIT_SKIPPED instance/session missing");
        return false;
    }

    if (!stringToPath("/user/hand/left", &handSubactionPaths_[0]) ||
        !stringToPath("/user/hand/right", &handSubactionPaths_[1])) {
        XR_LOGE("DDDVR/OpenXRInput", "CURRENT_BLOCKER XR_INPUT_HAND_PATHS_FAILED");
        return false;
    }

    XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strcpy(setInfo.actionSetName, "dddvr_player");
    std::strcpy(setInfo.localizedActionSetName, "DDD VR Player");
    setInfo.priority = 0;
    XrResult result = xrCreateActionSet(instance_, &setInfo, &actionSet_);
    if (result != XR_SUCCESS) {
        XR_LOGE("DDDVR/OpenXRInput", "CURRENT_BLOCKER XR_INPUT_ACTION_SET_FAILED result=%d", result);
        return false;
    }

    const bool actionsOk =
        createAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "play_pause", "Play or pause", &playPauseAction_) &&
        createAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "recenter", "Recenter", &recenterAction_) &&
        createAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "show_menu", "Show menu", &showMenuAction_) &&
        createAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "exit_player", "Exit player", &exitAction_) &&
        createAction(XR_ACTION_TYPE_FLOAT_INPUT, "trigger_play_pause", "Trigger play or pause", &triggerAction_) &&
        createAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "trigger_hold", "Trigger hold", &triggerClickAction_) &&
        createAction(XR_ACTION_TYPE_FLOAT_INPUT, "screen_grab_squeeze", "Screen grab side trigger", &squeezeAction_) &&
        createAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "screen_grab_squeeze_click", "Screen grab side trigger click", &squeezeClickAction_) &&
        createAction(XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Controller ray pose", &aimPoseAction_) &&
        createAction(XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Controller grip pose", &gripPoseAction_) &&
        createAction(XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Seek thumbstick", &thumbstickAction_) &&
        createAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_seek_progress", "Seek progress with pointer", &thumbstickClickAction_);
    if (!actionsOk) {
        destroy();
        return false;
    }

    auto bind = [this](XrAction action, const char* path) {
        XrPath xrPath = XR_NULL_PATH;
        if (!stringToPath(path, &xrPath)) return XrActionSuggestedBinding{action, XR_NULL_PATH};
        return XrActionSuggestedBinding{action, xrPath};
    };

    const XrActionSuggestedBinding simpleBindings[] = {
        bind(triggerClickAction_, "/user/hand/left/input/select/click"),
        bind(triggerClickAction_, "/user/hand/right/input/select/click"),
        bind(showMenuAction_, "/user/hand/left/input/menu/click"),
        bind(exitAction_, "/user/hand/right/input/menu/click"),
        bind(aimPoseAction_, "/user/hand/left/input/aim/pose"),
        bind(aimPoseAction_, "/user/hand/right/input/aim/pose"),
        bind(gripPoseAction_, "/user/hand/left/input/grip/pose"),
        bind(gripPoseAction_, "/user/hand/right/input/grip/pose")
    };
    suggestBindings("/interaction_profiles/khr/simple_controller", simpleBindings, 8);

    const XrActionSuggestedBinding touchBindings[] = {
        bind(showMenuAction_, "/user/hand/right/input/b/click"),
        bind(recenterAction_, "/user/hand/left/input/y/click"),
        bind(triggerAction_, "/user/hand/left/input/trigger/value"),
        bind(triggerAction_, "/user/hand/right/input/trigger/value"),
        bind(triggerClickAction_, "/user/hand/left/input/trigger/click"),
        bind(triggerClickAction_, "/user/hand/right/input/trigger/click"),
        bind(squeezeAction_, "/user/hand/left/input/squeeze/value"),
        bind(squeezeAction_, "/user/hand/right/input/squeeze/value"),
        bind(squeezeClickAction_, "/user/hand/left/input/squeeze/click"),
        bind(squeezeClickAction_, "/user/hand/right/input/squeeze/click"),
        bind(aimPoseAction_, "/user/hand/left/input/aim/pose"),
        bind(aimPoseAction_, "/user/hand/right/input/aim/pose"),
        bind(gripPoseAction_, "/user/hand/left/input/grip/pose"),
        bind(gripPoseAction_, "/user/hand/right/input/grip/pose"),
        bind(thumbstickAction_, "/user/hand/left/input/thumbstick"),
        bind(thumbstickAction_, "/user/hand/right/input/thumbstick"),
        bind(thumbstickClickAction_, "/user/hand/left/input/thumbstick/click"),
        bind(thumbstickClickAction_, "/user/hand/right/input/thumbstick/click")
    };
    suggestBindings("/interaction_profiles/oculus/touch_controller", touchBindings, 18);
    suggestBindings("/interaction_profiles/bytedance/pico4_controller", touchBindings, 18);
    suggestBindings("/interaction_profiles/bytedance/pico_neo3_controller", touchBindings, 18);
    suggestBindings("/interaction_profiles/pico/pico4_controller", touchBindings, 18);
    suggestBindings("/interaction_profiles/pico/neo3_controller", touchBindings, 18);

    XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &actionSet_;
    result = xrAttachSessionActionSets(session_, &attachInfo);
    if (result != XR_SUCCESS) {
        XR_LOGE("DDDVR/OpenXRInput", "CURRENT_BLOCKER XR_INPUT_ATTACH_FAILED result=%d", result);
        destroy();
        return false;
    }
    createAimSpaces();
    createGripSpaces();

    initialized_ = true;
    XR_LOGI("DDDVR/OpenXRInput", "XR_INPUT_READY");
    return true;
}

void OpenXrInput::sync() {
    if (!initialized_ || actionSet_ == XR_NULL_HANDLE || session_ == XR_NULL_HANDLE) return;

    XrActiveActionSet activeSet{};
    activeSet.actionSet = actionSet_;
    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeSet;
    const XrResult result = xrSyncActions(session_, &syncInfo);
    if (result == XR_SESSION_NOT_FOCUSED) return;
    if (XR_FAILED(result)) {
        static uint32_t failedSyncCount = 0;
        failedSyncCount += 1;
        if (failedSyncCount <= 5 || failedSyncCount % 120 == 0) {
            XR_LOGW("DDDVR/OpenXRInput", "XR_INPUT_SYNC_FAILED result=%d count=%u", result, failedSyncCount);
        }
        return;
    }
    if (result != XR_SUCCESS) return;

    pollBooleanAction(playPauseAction_, OpenXrInputActionCode::PlayPause);
    pollBooleanAction(recenterAction_, OpenXrInputActionCode::Recenter);
    pollBooleanAction(showMenuAction_, OpenXrInputActionCode::ShowMenu);
    pollBooleanAction(exitAction_, OpenXrInputActionCode::Exit);
    pollTrigger();
    pollSqueeze();
    pollThumbstickClick();
    pollThumbstick();
}

void OpenXrInput::destroy() {
    if (aimSpaces_[0] != XR_NULL_HANDLE) { xrDestroySpace(aimSpaces_[0]); aimSpaces_[0] = XR_NULL_HANDLE; }
    if (aimSpaces_[1] != XR_NULL_HANDLE) { xrDestroySpace(aimSpaces_[1]); aimSpaces_[1] = XR_NULL_HANDLE; }
    if (gripSpaces_[0] != XR_NULL_HANDLE) { xrDestroySpace(gripSpaces_[0]); gripSpaces_[0] = XR_NULL_HANDLE; }
    if (gripSpaces_[1] != XR_NULL_HANDLE) { xrDestroySpace(gripSpaces_[1]); gripSpaces_[1] = XR_NULL_HANDLE; }
    if (playPauseAction_ != XR_NULL_HANDLE) { xrDestroyAction(playPauseAction_); playPauseAction_ = XR_NULL_HANDLE; }
    if (recenterAction_ != XR_NULL_HANDLE) { xrDestroyAction(recenterAction_); recenterAction_ = XR_NULL_HANDLE; }
    if (showMenuAction_ != XR_NULL_HANDLE) { xrDestroyAction(showMenuAction_); showMenuAction_ = XR_NULL_HANDLE; }
    if (exitAction_ != XR_NULL_HANDLE) { xrDestroyAction(exitAction_); exitAction_ = XR_NULL_HANDLE; }
    if (triggerAction_ != XR_NULL_HANDLE) { xrDestroyAction(triggerAction_); triggerAction_ = XR_NULL_HANDLE; }
    if (triggerClickAction_ != XR_NULL_HANDLE) { xrDestroyAction(triggerClickAction_); triggerClickAction_ = XR_NULL_HANDLE; }
    if (squeezeAction_ != XR_NULL_HANDLE) { xrDestroyAction(squeezeAction_); squeezeAction_ = XR_NULL_HANDLE; }
    if (squeezeClickAction_ != XR_NULL_HANDLE) { xrDestroyAction(squeezeClickAction_); squeezeClickAction_ = XR_NULL_HANDLE; }
    if (aimPoseAction_ != XR_NULL_HANDLE) { xrDestroyAction(aimPoseAction_); aimPoseAction_ = XR_NULL_HANDLE; }
    if (gripPoseAction_ != XR_NULL_HANDLE) { xrDestroyAction(gripPoseAction_); gripPoseAction_ = XR_NULL_HANDLE; }
    if (thumbstickAction_ != XR_NULL_HANDLE) { xrDestroyAction(thumbstickAction_); thumbstickAction_ = XR_NULL_HANDLE; }
    if (thumbstickClickAction_ != XR_NULL_HANDLE) { xrDestroyAction(thumbstickClickAction_); thumbstickClickAction_ = XR_NULL_HANDLE; }
    if (actionSet_ != XR_NULL_HANDLE) { xrDestroyActionSet(actionSet_); actionSet_ = XR_NULL_HANDLE; }
    session_ = XR_NULL_HANDLE;
    initialized_ = false;
    activeGrabHand_ = -1;
    thumbstickXDirection_[0] = 0;
    thumbstickXDirection_[1] = 0;
    thumbstickYDirection_[0] = 0;
    thumbstickYDirection_[1] = 0;
    triggerPressed_[0] = false;
    triggerPressed_[1] = false;
    squeezePressed_[0] = false;
    squeezePressed_[1] = false;
    thumbstickClickPressed_[0] = false;
    thumbstickClickPressed_[1] = false;
    triggerConsumedByTimeline_[0] = false;
    triggerConsumedByTimeline_[1] = false;
    triggerConsumedByUi_[0] = false;
    triggerConsumedByUi_[1] = false;
    squeezeConsumedByMotion_[0] = false;
    squeezeConsumedByMotion_[1] = false;
    triggerPressedAt_[0] = {};
    triggerPressedAt_[1] = {};
    lastTriggerTapEmit_[0] = {};
    lastTriggerTapEmit_[1] = {};
    lastTimelineSeekEmit_[0] = {};
    lastTimelineSeekEmit_[1] = {};
    lastTriggerTimelineSeekEmit_[0] = {};
    lastTriggerTimelineSeekEmit_[1] = {};
    lastPlayPauseEmit_ = {};
    lastPointerActivity_ = {};
    pendingFrameControls_ = {};
    lastFrameControlsTime_ = {};
}

void OpenXrInput::locatePointerRays(XrSpace baseSpace, XrTime time, OpenXrPointerRay outRays[2]) const {
    if (outRays == nullptr) return;
    for (int hand = 0; hand < 2; ++hand) {
        outRays[hand].active = false;
        if (!initialized_ || aimSpaces_[hand] == XR_NULL_HANDLE || baseSpace == XR_NULL_HANDLE) continue;
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        const XrResult result = xrLocateSpace(aimSpaces_[hand], baseSpace, time, &location);
        const bool valid = result == XR_SUCCESS &&
            (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
            (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
        if (!valid) continue;
        outRays[hand].active = true;
        outRays[hand].pose = location.pose;
    }
}

void OpenXrInput::locateGripPoses(XrSpace baseSpace, XrTime time, OpenXrPointerRay outPoses[2]) const {
    if (outPoses == nullptr) return;
    for (int hand = 0; hand < 2; ++hand) {
        outPoses[hand].active = false;
        if (!initialized_ || gripSpaces_[hand] == XR_NULL_HANDLE || baseSpace == XR_NULL_HANDLE) continue;
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        const XrResult result = xrLocateSpace(gripSpaces_[hand], baseSpace, time, &location);
        const bool valid = result == XR_SUCCESS &&
            (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
            (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
        if (!valid) continue;
        outPoses[hand].active = true;
        outPoses[hand].pose = location.pose;
    }
}

OpenXrFrameControls OpenXrInput::consumeFrameControls() {
    OpenXrFrameControls out = pendingFrameControls_;
    pendingFrameControls_ = {};
    return out;
}

bool OpenXrInput::createAction(XrActionType type, const char* name, const char* localizedName, XrAction* action) {
    XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
    actionInfo.actionType = type;
    std::strcpy(actionInfo.actionName, name);
    std::strcpy(actionInfo.localizedActionName, localizedName);
    actionInfo.countSubactionPaths = 2;
    actionInfo.subactionPaths = handSubactionPaths_;
    const XrResult result = xrCreateAction(actionSet_, &actionInfo, action);
    if (result != XR_SUCCESS) {
        XR_LOGE("DDDVR/OpenXRInput", "CURRENT_BLOCKER XR_INPUT_ACTION_FAILED name=%s result=%d", name, result);
        return false;
    }
    return true;
}

bool OpenXrInput::createAimSpaces() {
    if (aimPoseAction_ == XR_NULL_HANDLE || session_ == XR_NULL_HANDLE) return false;
    bool createdAny = false;
    for (int hand = 0; hand < 2; ++hand) {
        XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        spaceInfo.action = aimPoseAction_;
        spaceInfo.subactionPath = handSubactionPaths_[hand];
        spaceInfo.poseInActionSpace.orientation.w = 1.f;
        const XrResult result = xrCreateActionSpace(session_, &spaceInfo, &aimSpaces_[hand]);
        if (result == XR_SUCCESS) {
            createdAny = true;
        } else {
            aimSpaces_[hand] = XR_NULL_HANDLE;
            XR_LOGW("DDDVR/OpenXRInput", "XR_INPUT_AIM_SPACE_FAILED hand=%d result=%d", hand, result);
        }
    }
    if (createdAny) XR_LOGI("DDDVR/OpenXRInput", "XR_INPUT_AIM_READY");
    return createdAny;
}

bool OpenXrInput::createGripSpaces() {
    if (gripPoseAction_ == XR_NULL_HANDLE || session_ == XR_NULL_HANDLE) return false;
    bool createdAny = false;
    for (int hand = 0; hand < 2; ++hand) {
        XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        spaceInfo.action = gripPoseAction_;
        spaceInfo.subactionPath = handSubactionPaths_[hand];
        spaceInfo.poseInActionSpace.orientation.w = 1.f;
        const XrResult result = xrCreateActionSpace(session_, &spaceInfo, &gripSpaces_[hand]);
        if (result == XR_SUCCESS) {
            createdAny = true;
        } else {
            gripSpaces_[hand] = XR_NULL_HANDLE;
            XR_LOGW("DDDVR/OpenXRInput", "XR_INPUT_GRIP_SPACE_FAILED hand=%d result=%d", hand, result);
        }
    }
    if (createdAny) XR_LOGI("DDDVR/OpenXRInput", "XR_INPUT_GRIP_READY");
    return createdAny;
}

void OpenXrInput::suggestBindings(const char* interactionProfile, const XrActionSuggestedBinding* bindings, uint32_t count) {
    XrPath profilePath = XR_NULL_PATH;
    if (!stringToPath(interactionProfile, &profilePath)) return;

    std::vector<XrActionSuggestedBinding> validBindings;
    validBindings.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (bindings[i].binding != XR_NULL_PATH && bindings[i].action != XR_NULL_HANDLE) {
            validBindings.push_back(bindings[i]);
        }
    }
    if (validBindings.empty()) return;

    XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = profilePath;
    suggested.countSuggestedBindings = static_cast<uint32_t>(validBindings.size());
    suggested.suggestedBindings = validBindings.data();
    const XrResult result = xrSuggestInteractionProfileBindings(instance_, &suggested);
    if (!succeededOrUnsupported(result)) {
        XR_LOGW("DDDVR/OpenXRInput", "XR_INPUT_BINDINGS_FAILED profile=%s result=%d", interactionProfile, result);
    } else {
        XR_LOGI("DDDVR/OpenXRInput", "XR_INPUT_BINDINGS profile=%s result=%d", interactionProfile, result);
    }
}

bool OpenXrInput::stringToPath(const char* path, XrPath* out) const {
    const XrResult result = xrStringToPath(instance_, path, out);
    if (result != XR_SUCCESS) {
        XR_LOGW("DDDVR/OpenXRInput", "XR_INPUT_PATH_FAILED path=%s result=%d", path, result);
        if (out != nullptr) *out = XR_NULL_PATH;
        return false;
    }
    return true;
}

void OpenXrInput::pollBooleanAction(XrAction action, OpenXrInputActionCode code) {
    if (action == XR_NULL_HANDLE) return;
    for (XrPath handPath : handSubactionPaths_) {
        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.action = action;
        getInfo.subactionPath = handPath;
        XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
        const XrResult result = xrGetActionStateBoolean(session_, &getInfo, &state);
        if (result == XR_SUCCESS && state.isActive && state.changedSinceLastSync && state.currentState) {
            emit(code);
        }
    }
}

void OpenXrInput::pollTrigger() {
    if (triggerAction_ == XR_NULL_HANDLE && triggerClickAction_ == XR_NULL_HANDLE) return;
    const auto now = std::chrono::steady_clock::now();
    for (int hand = 0; hand < 2; ++hand) {
        bool sourceActive = false;
        bool down = false;
        float triggerValue = 0.f;

        if (triggerAction_ != XR_NULL_HANDLE) {
            XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
            getInfo.action = triggerAction_;
            getInfo.subactionPath = handSubactionPaths_[hand];
            XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
            const XrResult result = xrGetActionStateFloat(session_, &getInfo, &state);
            if (result == XR_SUCCESS && state.isActive) {
                sourceActive = true;
                triggerValue = state.currentState;
                down = triggerPressed_[hand]
                    ? state.currentState > kTriggerReleaseThreshold
                    : state.currentState >= kTriggerPressThreshold;
            }
        }

        if (triggerClickAction_ != XR_NULL_HANDLE) {
            XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
            getInfo.action = triggerClickAction_;
            getInfo.subactionPath = handSubactionPaths_[hand];
            XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
            const XrResult result = xrGetActionStateBoolean(session_, &getInfo, &state);
            if (result == XR_SUCCESS && state.isActive) {
                sourceActive = true;
                down = down || state.currentState;
            }
        }

        if (!sourceActive) {
            triggerPressed_[hand] = false;
            triggerConsumedByTimeline_[hand] = false;
            triggerConsumedByUi_[hand] = false;
            lastTriggerTimelineSeekEmit_[hand] = {};
            continue;
        }

        if (!triggerPressed_[hand] && down) {
            triggerPressed_[hand] = true;
            triggerConsumedByTimeline_[hand] = false;
            triggerConsumedByUi_[hand] = false;
            triggerPressedAt_[hand] = now;
            lastTriggerTimelineSeekEmit_[hand] = {};
            lastPointerActivity_ = now;
            XR_LOGI("DDDVR/OpenXRInput", "XR_INPUT_TRIGGER_DOWN hand=%d value=%.2f", hand, triggerValue);
        } else if (triggerPressed_[hand] && !down) {
            const bool consumedTimeline = triggerConsumedByTimeline_[hand];
            const bool consumedUi = triggerConsumedByUi_[hand];
            const bool consumed = consumedTimeline || consumedUi;
            if (!consumed && shouldEmitTriggerTap(hand, now)) {
                emit(OpenXrInputActionCode::PlayPause);
                lastTriggerTapEmit_[hand] = now;
            }
            triggerPressed_[hand] = false;
            triggerConsumedByTimeline_[hand] = false;
            triggerConsumedByUi_[hand] = false;
            lastTriggerTimelineSeekEmit_[hand] = {};
            lastPointerActivity_ = now;
            XR_LOGI("DDDVR/OpenXRInput", "XR_INPUT_TRIGGER_UP hand=%d consumedTimeline=%d consumedUi=%d",
                    hand, consumedTimeline ? 1 : 0, consumedUi ? 1 : 0);
        }
    }
}

void OpenXrInput::pollSqueeze() {
    if (squeezeAction_ == XR_NULL_HANDLE && squeezeClickAction_ == XR_NULL_HANDLE) return;
    const auto now = std::chrono::steady_clock::now();
    for (int hand = 0; hand < 2; ++hand) {
        bool sourceActive = false;
        bool down = false;
        float squeezeValue = 0.f;

        if (squeezeAction_ != XR_NULL_HANDLE) {
            XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
            getInfo.action = squeezeAction_;
            getInfo.subactionPath = handSubactionPaths_[hand];
            XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
            const XrResult result = xrGetActionStateFloat(session_, &getInfo, &state);
            if (result == XR_SUCCESS && state.isActive) {
                sourceActive = true;
                squeezeValue = state.currentState;
                down = squeezePressed_[hand]
                    ? state.currentState > kSqueezeReleaseThreshold
                    : state.currentState >= kSqueezePressThreshold;
            }
        }

        if (squeezeClickAction_ != XR_NULL_HANDLE) {
            XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
            getInfo.action = squeezeClickAction_;
            getInfo.subactionPath = handSubactionPaths_[hand];
            XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
            const XrResult result = xrGetActionStateBoolean(session_, &getInfo, &state);
            if (result == XR_SUCCESS && state.isActive) {
                sourceActive = true;
                down = down || state.currentState;
            }
        }

        if (!sourceActive) {
            if (squeezePressed_[hand]) {
                XR_LOGI("DDDVR/OpenXRInput", "XR_INPUT_SQUEEZE_UP hand=%d consumed=%d inactive=1",
                        hand, squeezeConsumedByMotion_[hand] ? 1 : 0);
            }
            squeezePressed_[hand] = false;
            squeezeConsumedByMotion_[hand] = false;
            if (activeGrabHand_ == hand) {
                const int other = hand == 0 ? 1 : 0;
                activeGrabHand_ = squeezePressed_[other] ? other : -1;
            }
            continue;
        }

        if (!squeezePressed_[hand] && down) {
            squeezePressed_[hand] = true;
            squeezeConsumedByMotion_[hand] = false;
            if (activeGrabHand_ < 0) activeGrabHand_ = hand;
            lastPointerActivity_ = now;
            XR_LOGI("DDDVR/OpenXRInput", "XR_INPUT_SQUEEZE_DOWN hand=%d value=%.2f", hand, squeezeValue);
        } else if (squeezePressed_[hand] && !down) {
            const bool consumed = squeezeConsumedByMotion_[hand];
            squeezePressed_[hand] = false;
            squeezeConsumedByMotion_[hand] = false;
            if (activeGrabHand_ == hand) {
                const int other = hand == 0 ? 1 : 0;
                activeGrabHand_ = squeezePressed_[other] ? other : -1;
            }
            lastPointerActivity_ = now;
            XR_LOGI("DDDVR/OpenXRInput", "XR_INPUT_SQUEEZE_UP hand=%d consumed=%d", hand, consumed ? 1 : 0);
        }
    }
}

void OpenXrInput::pollThumbstickClick() {
    if (thumbstickClickAction_ == XR_NULL_HANDLE) return;
    const auto now = std::chrono::steady_clock::now();
    for (int hand = 0; hand < 2; ++hand) {
        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.action = thumbstickClickAction_;
        getInfo.subactionPath = handSubactionPaths_[hand];
        XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
        const XrResult result = xrGetActionStateBoolean(session_, &getInfo, &state);
        if (result != XR_SUCCESS || !state.isActive || anyGrabPressed()) {
            if (thumbstickClickPressed_[hand]) {
                XR_LOGI("DDDVR/OpenXRInput", "XR_INPUT_THUMBSTICK_CLICK_UP hand=%d inactive=%d grab=%d",
                        hand, (result != XR_SUCCESS || !state.isActive) ? 1 : 0, anyGrabPressed() ? 1 : 0);
            }
            thumbstickClickPressed_[hand] = false;
            lastTimelineSeekEmit_[hand] = {};
            continue;
        }

        if (state.currentState) {
            const bool wasPressed = thumbstickClickPressed_[hand];
            thumbstickClickPressed_[hand] = true;
            if (!wasPressed) {
                XR_LOGI("DDDVR/OpenXRInput", "XR_INPUT_THUMBSTICK_CLICK_DOWN hand=%d", hand);
            }

            const auto lastEmit = lastTimelineSeekEmit_[hand];
            if (!wasPressed ||
                lastEmit.time_since_epoch().count() == 0 ||
                now - lastEmit >= kTimelineSeekDragInterval) {
                lastTimelineSeekEmit_[hand] = now;
                pendingFrameControls_.seekProgressPointerHand = hand;
                pendingFrameControls_.seekProgressFromTrigger = false;
                lastPointerActivity_ = now;
                XR_LOGI("DDDVR/OpenXRInput", "XR_INPUT_THUMBSTICK_CLICK_SEEK hand=%d held=%d",
                        hand, wasPressed ? 1 : 0);
            }
        } else if (thumbstickClickPressed_[hand]) {
            thumbstickClickPressed_[hand] = false;
            lastTimelineSeekEmit_[hand] = {};
            lastPointerActivity_ = now;
            XR_LOGI("DDDVR/OpenXRInput", "XR_INPUT_THUMBSTICK_CLICK_UP hand=%d inactive=0 grab=0", hand);
        }
    }
}

void OpenXrInput::pollThumbstick() {
    if (thumbstickAction_ == XR_NULL_HANDLE) return;
    const auto now = std::chrono::steady_clock::now();
    float deltaSeconds = 1.f / 90.f;
    if (lastFrameControlsTime_.time_since_epoch().count() != 0) {
        deltaSeconds = std::chrono::duration<float>(now - lastFrameControlsTime_).count();
        if (deltaSeconds < 0.f) deltaSeconds = 0.f;
        if (deltaSeconds > 0.05f) deltaSeconds = 0.05f;
    }
    lastFrameControlsTime_ = now;

    for (int hand = 0; hand < 2; ++hand) {
        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.action = thumbstickAction_;
        getInfo.subactionPath = handSubactionPaths_[hand];
        XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
        const XrResult result = xrGetActionStateVector2f(session_, &getInfo, &state);
        if (result != XR_SUCCESS || !state.isActive) {
            thumbstickXDirection_[hand] = 0;
            thumbstickYDirection_[hand] = 0;
            continue;
        }

        const float x = state.currentState.x;
        const float y = state.currentState.y;
        if (anyGrabPressed()) {
            const float absX = std::fabs(x);
            const float absY = std::fabs(y);
            if (absX > kThumbstickMoveDeadzone || absY > kThumbstickMoveDeadzone) {
                markGrabMotionConsumed();
                lastPointerActivity_ = now;
                static uint32_t grabStickLogCount = 0;
                grabStickLogCount += 1;
                if (grabStickLogCount <= 12 || grabStickLogCount % 45 == 0) {
                    XR_LOGI("DDDVR/OpenXRInput", "XR_INPUT_GRAB_STICK hand=%d activeGrab=%d x=%.2f y=%.2f dt=%.3f",
                            hand, activeGrabHand(), x, y, deltaSeconds);
                }
            }

            if (absX > kThumbstickMoveDeadzone && absX >= absY) {
                pendingFrameControls_.screenYawDeltaRadians += x * kYawSpeedRadiansPerSecond * deltaSeconds;
            } else if (absY > kThumbstickMoveDeadzone) {
                pendingFrameControls_.screenDistanceDeltaMeters += y * kDistanceSpeedMetersPerSecond * deltaSeconds;
            }
            thumbstickXDirection_[hand] = 0;
            thumbstickYDirection_[hand] = 0;
            continue;
        }

        int direction = thumbstickXDirection_[hand];
        if (std::fabs(x) < kThumbstickReleaseThreshold) {
            thumbstickXDirection_[hand] = 0;
        } else {
            const int newDirection = x > kThumbstickPressThreshold ? 1 : (x < -kThumbstickPressThreshold ? -1 : direction);
            if (newDirection != 0 && (direction != newDirection || now - lastThumbstickEmit_ >= kThumbstickRepeatDelay)) {
                thumbstickXDirection_[hand] = newDirection;
                lastThumbstickEmit_ = now;
                emit(newDirection > 0 ? OpenXrInputActionCode::SeekForward : OpenXrInputActionCode::SeekBack);
            }
        }
        thumbstickYDirection_[hand] = 0;
    }
}

bool OpenXrInput::anyGrabPressed() const {
    return squeezePressed_[0] || squeezePressed_[1];
}

int OpenXrInput::activeGrabHand() const {
    if (activeGrabHand_ >= 0 && squeezePressed_[activeGrabHand_]) return activeGrabHand_;
    if (squeezePressed_[0]) return 0;
    if (squeezePressed_[1]) return 1;
    return -1;
}

bool OpenXrInput::triggerPressed(int hand) const {
    if (hand < 0 || hand >= 2) return false;
    return triggerPressed_[hand];
}

bool OpenXrInput::shouldShowPointerRays() const {
    if (anyGrabPressed() || triggerPressed_[0] || triggerPressed_[1]) return true;
    if (lastPointerActivity_.time_since_epoch().count() == 0) return false;
    return std::chrono::steady_clock::now() - lastPointerActivity_ < kPointerIdleTimeout;
}

void OpenXrInput::markGrabMotionConsumed() {
    for (int hand = 0; hand < 2; ++hand) {
        if (squeezePressed_[hand]) {
            squeezeConsumedByMotion_[hand] = true;
        }
    }
}

void OpenXrInput::markTriggerTimelineConsumed(int hand) {
    if (hand < 0 || hand >= 2 || !triggerPressed_[hand]) return;
    triggerConsumedByTimeline_[hand] = true;
}

void OpenXrInput::markTriggerConsumedByUi(int hand) {
    if (hand < 0 || hand >= 2 || !triggerPressed_[hand]) return;
    triggerConsumedByUi_[hand] = true;
}

bool OpenXrInput::shouldEmitTriggerTap(int hand, std::chrono::steady_clock::time_point now) const {
    const auto held = now - triggerPressedAt_[hand];
    if (held < std::chrono::milliseconds(25)) return false;
    const auto last = lastTriggerTapEmit_[hand];
    return last.time_since_epoch().count() == 0 || now - last >= kTriggerTapDebounce;
}

void OpenXrInput::emit(OpenXrInputActionCode code) {
    if (code == OpenXrInputActionCode::None || !callback_) return;
    if (code == OpenXrInputActionCode::PlayPause) {
        const auto now = std::chrono::steady_clock::now();
        if (lastPlayPauseEmit_.time_since_epoch().count() != 0 &&
            now - lastPlayPauseEmit_ < kPlayPauseDebounce) {
            return;
        }
        lastPlayPauseEmit_ = now;
    }
    XR_LOGI("DDDVR/OpenXRInput", "XR_INPUT_ACTION code=%d", static_cast<int>(code));
    callback_(code);
}
