#pragma once

#include "../third/imgui/imgui.h"

#include <GLES3/gl3.h>

class VrPicoGuiBase {
public:
    static VrPicoGuiBase& instance();
    bool initialize(ImGuiContext* context, GLuint fontTexture);
    void shutdown();
    bool initialized() const { return initialized_; }

private:
    VrPicoGuiBase() = default;
    bool initialized_ = false;
    ImGuiContext* context_ = nullptr;
    GLuint fontTexture_ = 0;
};

