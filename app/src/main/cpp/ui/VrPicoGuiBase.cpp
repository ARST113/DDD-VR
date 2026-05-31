#include "VrPicoGuiBase.h"

#include "../util/XrLog.h"

VrPicoGuiBase& VrPicoGuiBase::instance() {
    static VrPicoGuiBase base;
    return base;
}

bool VrPicoGuiBase::initialize(ImGuiContext* context, GLuint fontTexture) {
    context_ = context;
    fontTexture_ = fontTexture;
    initialized_ = context_ != nullptr && fontTexture_ != 0;
    if (initialized_) {
        XR_LOGI("DDDVR/OpenXRUi", "XR_UI_PICO_GUI_BASE_READY fontTexture=%u", fontTexture_);
    }
    return initialized_;
}

void VrPicoGuiBase::shutdown() {
    initialized_ = false;
    context_ = nullptr;
    fontTexture_ = 0;
}

