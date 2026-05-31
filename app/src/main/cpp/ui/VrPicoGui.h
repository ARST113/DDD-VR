#pragma once

#include "VrUiTypes.h"
#include "../third/imgui/imgui.h"

#include <GLES3/gl3.h>

class VrPicoGui {
public:
    bool initialize(int textureWidth, int textureHeight);
    void destroy();

    void beginFrame(float deltaSeconds);
    void endFrame();

    void renderPanelQuad(
        const float* mvp,
        float centerX,
        float centerY,
        float centerZ,
        float widthMeters,
        float heightMeters
    );
    void renderPanelQuad(const float* mvp, const VrUiPlane& plane);

    void setPointerPixel(float x, float y);
    void setPointerVisible(bool visible);
    void setPrimaryButton(bool pressed);

    GLuint uiTexture() const { return uiTexture_; }
    int width() const { return textureWidth_; }
    int height() const { return textureHeight_; }
    bool initialized() const { return initialized_; }

private:
    bool createDeviceObjects();
    bool createFontsTexture();
    bool createFramebuffer(int textureWidth, int textureHeight);
    bool createQuadObjects();
    void renderDrawDataToTexture(ImDrawData* drawData);
    void renderImGuiDrawData(ImDrawData* drawData);

    ImGuiContext* context_ = nullptr;
    bool initialized_ = false;
    int textureWidth_ = 0;
    int textureHeight_ = 0;
    bool pointerVisible_ = false;
    float pointerX_ = -1.f;
    float pointerY_ = -1.f;
    bool primaryButtonPressed_ = false;

    GLuint fontTexture_ = 0;
    GLuint uiTexture_ = 0;
    GLuint uiFbo_ = 0;
    GLuint imguiProgram_ = 0;
    GLuint imguiVbo_ = 0;
    GLuint imguiIbo_ = 0;
    GLint imguiProjLoc_ = -1;
    GLint imguiTextureLoc_ = -1;

    GLuint quadProgram_ = 0;
    GLuint quadVbo_ = 0;
    GLint quadMvpLoc_ = -1;
    GLint quadTextureLoc_ = -1;
};
