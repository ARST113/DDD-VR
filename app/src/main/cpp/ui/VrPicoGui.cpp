#include "VrPicoGui.h"

#include "VrPicoGuiBase.h"
#include "../util/XrLog.h"

#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        XR_LOGE("DDDVR/OpenXRUi", "XR_UI_SHADER_COMPILE_FAILED type=%u log=%s", type, log);
    }
    return shader;
}

GLuint createProgram(const char* vertexSrc, const char* fragmentSrc) {
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSrc);
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSrc);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024]{};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        XR_LOGE("DDDVR/OpenXRUi", "XR_UI_PROGRAM_LINK_FAILED log=%s", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

float clampDelta(float deltaSeconds) {
    if (deltaSeconds <= 0.f) return 1.f / 60.f;
    if (deltaSeconds > 0.1f) return 0.1f;
    return deltaSeconds;
}
}

bool VrPicoGui::initialize(int textureWidth, int textureHeight) {
    if (initialized_) return true;
    if (textureWidth <= 0 || textureHeight <= 0) return false;

    context_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "DDDVR_VrPicoGui";
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.FontGlobalScale = 1.95f;
    io.DisplaySize = ImVec2(static_cast<float>(textureWidth), static_cast<float>(textureHeight));
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 18.f;
    style.FrameRounding = 14.f;
    style.GrabRounding = 8.f;
    style.ScrollbarRounding = 8.f;

    textureWidth_ = textureWidth;
    textureHeight_ = textureHeight;
    if (!createDeviceObjects() ||
        !createFontsTexture() ||
        !createFramebuffer(textureWidth, textureHeight) ||
        !createQuadObjects()) {
        destroy();
        return false;
    }

    VrPicoGuiBase::instance().initialize(context_, fontTexture_);
    initialized_ = true;
    XR_LOGI("DDDVR/OpenXRUi", "XR_UI_BACKEND_READY texture=%dx%d", textureWidth_, textureHeight_);
    return true;
}

void VrPicoGui::destroy() {
    if (quadVbo_ != 0) { glDeleteBuffers(1, &quadVbo_); quadVbo_ = 0; }
    if (quadProgram_ != 0) { glDeleteProgram(quadProgram_); quadProgram_ = 0; }
    if (imguiIbo_ != 0) { glDeleteBuffers(1, &imguiIbo_); imguiIbo_ = 0; }
    if (imguiVbo_ != 0) { glDeleteBuffers(1, &imguiVbo_); imguiVbo_ = 0; }
    if (imguiProgram_ != 0) { glDeleteProgram(imguiProgram_); imguiProgram_ = 0; }
    if (uiFbo_ != 0) { glDeleteFramebuffers(1, &uiFbo_); uiFbo_ = 0; }
    if (uiTexture_ != 0) { glDeleteTextures(1, &uiTexture_); uiTexture_ = 0; }
    if (fontTexture_ != 0) { glDeleteTextures(1, &fontTexture_); fontTexture_ = 0; }
    if (context_ != nullptr) {
        ImGui::SetCurrentContext(context_);
        ImGui::DestroyContext(context_);
        context_ = nullptr;
    }
    VrPicoGuiBase::instance().shutdown();
    initialized_ = false;
}

void VrPicoGui::beginFrame(float deltaSeconds) {
    if (!initialized_) return;
    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(textureWidth_), static_cast<float>(textureHeight_));
    io.DisplayFramebufferScale = ImVec2(1.f, 1.f);
    io.DeltaTime = clampDelta(deltaSeconds);
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    io.AddMousePosEvent(pointerVisible_ ? pointerX_ : -FLT_MAX, pointerVisible_ ? pointerY_ : -FLT_MAX);
    io.AddMouseButtonEvent(0, primaryButtonPressed_);
    ImGui::NewFrame();
}

void VrPicoGui::endFrame() {
    if (!initialized_) return;
    ImGui::SetCurrentContext(context_);
    ImGui::Render();
    renderDrawDataToTexture(ImGui::GetDrawData());
}

void VrPicoGui::renderPanelQuad(
    const float* mvp,
    float centerX,
    float centerY,
    float centerZ,
    float widthMeters,
    float heightMeters
) {
    VrUiPlane plane{};
    plane.center = {centerX, centerY, centerZ};
    plane.right = {1.f, 0.f, 0.f};
    plane.up = {0.f, 1.f, 0.f};
    plane.normal = {0.f, 0.f, 1.f};
    plane.yawRadians = 0.f;
    plane.widthMeters = widthMeters;
    plane.heightMeters = heightMeters;
    renderPanelQuad(mvp, plane);
}

void VrPicoGui::renderPanelQuad(const float* mvp, const VrUiPlane& plane) {
    if (!initialized_ || mvp == nullptr || uiTexture_ == 0 || quadProgram_ == 0 || quadVbo_ == 0) return;

    const float halfW = plane.widthMeters * 0.5f;
    const float halfH = plane.heightMeters * 0.5f;
    auto append = [&](std::vector<GLfloat>& out, float localX, float localY, float u, float v) {
        out.push_back(plane.center.x + plane.right.x * localX + plane.up.x * localY);
        out.push_back(plane.center.y + plane.right.y * localX + plane.up.y * localY);
        out.push_back(plane.center.z + plane.right.z * localX + plane.up.z * localY);
        out.push_back(u);
        out.push_back(v);
    };

    std::vector<GLfloat> vertices;
    vertices.reserve(30);
    append(vertices, -halfW, -halfH, 0.f, 1.f);
    append(vertices,  halfW, -halfH, 1.f, 1.f);
    append(vertices, -halfW,  halfH, 0.f, 0.f);
    append(vertices,  halfW, -halfH, 1.f, 1.f);
    append(vertices,  halfW,  halfH, 1.f, 0.f);
    append(vertices, -halfW,  halfH, 0.f, 0.f);

    GLint lastProgram = 0;
    GLint lastTexture = 0;
    GLint lastArrayBuffer = 0;
    GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    glGetIntegerv(GL_CURRENT_PROGRAM, &lastProgram);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTexture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &lastArrayBuffer);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(quadProgram_);
    glUniformMatrix4fv(quadMvpLoc_, 1, GL_FALSE, mvp);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, uiTexture_);
    glUniform1i(quadTextureLoc_, 0);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GLfloat), vertices.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), reinterpret_cast<void*>(0));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), reinterpret_cast<void*>(3 * sizeof(GLfloat)));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    if (!blendEnabled) glDisable(GL_BLEND);
    if (depthEnabled) glEnable(GL_DEPTH_TEST);
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(lastArrayBuffer));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(lastTexture));
    glUseProgram(static_cast<GLuint>(lastProgram));
}

void VrPicoGui::setPointerPixel(float x, float y) {
    pointerX_ = x;
    pointerY_ = y;
}

void VrPicoGui::setPointerVisible(bool visible) {
    pointerVisible_ = visible;
}

void VrPicoGui::setPrimaryButton(bool pressed) {
    primaryButtonPressed_ = pressed;
}

bool VrPicoGui::createDeviceObjects() {
    const char* imguiVs =
        "#version 300 es\n"
        "precision mediump float;\n"
        "layout(location=0) in vec2 aPos;\n"
        "layout(location=1) in vec2 aUv;\n"
        "layout(location=2) in vec4 aColor;\n"
        "uniform mat4 uProj;\n"
        "out vec2 vUv;\n"
        "out vec4 vColor;\n"
        "void main(){ vUv=aUv; vColor=aColor; gl_Position=uProj*vec4(aPos.xy,0.0,1.0); }";
    const char* imguiFs =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec2 vUv;\n"
        "in vec4 vColor;\n"
        "uniform sampler2D uTexture;\n"
        "out vec4 fragColor;\n"
        "void main(){ fragColor=vColor*texture(uTexture,vUv); }";
    imguiProgram_ = createProgram(imguiVs, imguiFs);
    if (imguiProgram_ == 0) return false;
    imguiProjLoc_ = glGetUniformLocation(imguiProgram_, "uProj");
    imguiTextureLoc_ = glGetUniformLocation(imguiProgram_, "uTexture");
    glGenBuffers(1, &imguiVbo_);
    glGenBuffers(1, &imguiIbo_);
    return imguiVbo_ != 0 && imguiIbo_ != 0;
}

bool VrPicoGui::createFontsTexture() {
    ImGui::SetCurrentContext(context_);
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    int bytesPerPixel = 0;
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig fontConfig{};
    fontConfig.RasterizerMultiply = 1.12f;
    ImFont* font = io.Fonts->AddFontFromFileTTF(
        "/system/fonts/Roboto-Regular.ttf",
        17.0f,
        &fontConfig,
        io.Fonts->GetGlyphRangesCyrillic()
    );
    if (font == nullptr) {
        font = io.Fonts->AddFontDefault();
    }
    io.FontDefault = font;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &bytesPerPixel);
    if (pixels == nullptr || width <= 0 || height <= 0) return false;

    glGenTextures(1, &fontTexture_);
    glBindTexture(GL_TEXTURE_2D, fontTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    io.Fonts->SetTexID(static_cast<ImTextureID>(fontTexture_));
    return fontTexture_ != 0;
}

bool VrPicoGui::createFramebuffer(int textureWidth, int textureHeight) {
    glGenTextures(1, &uiTexture_);
    glBindTexture(GL_TEXTURE_2D, uiTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureWidth, textureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, &uiFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, uiFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, uiTexture_, 0);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        XR_LOGE("DDDVR/OpenXRUi", "XR_UI_FBO_FAILED status=0x%x", status);
        return false;
    }
    return true;
}

bool VrPicoGui::createQuadObjects() {
    const char* quadVs =
        "#version 300 es\n"
        "precision mediump float;\n"
        "layout(location=0) in vec3 aPos;\n"
        "layout(location=1) in vec2 aUv;\n"
        "uniform mat4 uMvp;\n"
        "out vec2 vUv;\n"
        "void main(){ vUv=aUv; gl_Position=uMvp*vec4(aPos,1.0); }";
    const char* quadFs =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec2 vUv;\n"
        "uniform sampler2D uTexture;\n"
        "out vec4 fragColor;\n"
        "void main(){ fragColor=texture(uTexture,vUv); }";
    quadProgram_ = createProgram(quadVs, quadFs);
    if (quadProgram_ == 0) return false;
    quadMvpLoc_ = glGetUniformLocation(quadProgram_, "uMvp");
    quadTextureLoc_ = glGetUniformLocation(quadProgram_, "uTexture");
    glGenBuffers(1, &quadVbo_);
    return quadVbo_ != 0;
}

void VrPicoGui::renderDrawDataToTexture(ImDrawData* drawData) {
    if (drawData == nullptr) return;

    GLint lastFramebuffer = 0;
    GLint lastViewport[4]{};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &lastFramebuffer);
    glGetIntegerv(GL_VIEWPORT, lastViewport);

    glBindFramebuffer(GL_FRAMEBUFFER, uiFbo_);
    glViewport(0, 0, textureWidth_, textureHeight_);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    renderImGuiDrawData(drawData);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(lastFramebuffer));
    glViewport(lastViewport[0], lastViewport[1], lastViewport[2], lastViewport[3]);
}

void VrPicoGui::renderImGuiDrawData(ImDrawData* drawData) {
    const int framebufferWidth = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
    const int framebufferHeight = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
    if (framebufferWidth <= 0 || framebufferHeight <= 0) return;

    GLint lastProgram = 0;
    GLint lastTexture = 0;
    GLint lastArrayBuffer = 0;
    GLint lastElementArrayBuffer = 0;
    GLint lastViewport[4]{};
    GLint lastScissorBox[4]{};
    GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_CURRENT_PROGRAM, &lastProgram);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTexture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &lastArrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &lastElementArrayBuffer);
    glGetIntegerv(GL_VIEWPORT, lastViewport);
    glGetIntegerv(GL_SCISSOR_BOX, lastScissorBox);

    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    glViewport(0, 0, framebufferWidth, framebufferHeight);

    const float left = drawData->DisplayPos.x;
    const float right = drawData->DisplayPos.x + drawData->DisplaySize.x;
    const float top = drawData->DisplayPos.y;
    const float bottom = drawData->DisplayPos.y + drawData->DisplaySize.y;
    const float ortho[4][4] = {
        { 2.f / (right - left), 0.f, 0.f, 0.f },
        { 0.f, 2.f / (top - bottom), 0.f, 0.f },
        { 0.f, 0.f, -1.f, 0.f },
        { (right + left) / (left - right), (top + bottom) / (bottom - top), 0.f, 1.f },
    };

    glUseProgram(imguiProgram_);
    glUniform1i(imguiTextureLoc_, 0);
    glUniformMatrix4fv(imguiProjLoc_, 1, GL_FALSE, &ortho[0][0]);
    glBindBuffer(GL_ARRAY_BUFFER, imguiVbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, imguiIbo_);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, pos)));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, uv)));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, col)));

    const ImVec2 clipOffset = drawData->DisplayPos;
    const ImVec2 clipScale = drawData->FramebufferScale;
    for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex) {
        const ImDrawList* cmdList = drawData->CmdLists[listIndex];
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(cmdList->VtxBuffer.Size * sizeof(ImDrawVert)),
            cmdList->VtxBuffer.Data,
            GL_STREAM_DRAW
        );
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(cmdList->IdxBuffer.Size * sizeof(ImDrawIdx)),
            cmdList->IdxBuffer.Data,
            GL_STREAM_DRAW
        );

        for (int cmdIndex = 0; cmdIndex < cmdList->CmdBuffer.Size; ++cmdIndex) {
            const ImDrawCmd* drawCmd = &cmdList->CmdBuffer[cmdIndex];
            if (drawCmd->UserCallback != nullptr) {
                drawCmd->UserCallback(cmdList, drawCmd);
                continue;
            }
            ImVec2 clipMin(
                (drawCmd->ClipRect.x - clipOffset.x) * clipScale.x,
                (drawCmd->ClipRect.y - clipOffset.y) * clipScale.y
            );
            ImVec2 clipMax(
                (drawCmd->ClipRect.z - clipOffset.x) * clipScale.x,
                (drawCmd->ClipRect.w - clipOffset.y) * clipScale.y
            );
            if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y) continue;

            glScissor(
                static_cast<GLint>(clipMin.x),
                static_cast<GLint>(framebufferHeight - clipMax.y),
                static_cast<GLsizei>(clipMax.x - clipMin.x),
                static_cast<GLsizei>(clipMax.y - clipMin.y)
            );
            const GLuint texture = static_cast<GLuint>(drawCmd->GetTexID());
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture);
            const GLvoid* indexOffset =
                reinterpret_cast<const GLvoid*>((drawCmd->IdxOffset) * sizeof(ImDrawIdx));
            glDrawElements(
                GL_TRIANGLES,
                static_cast<GLsizei>(drawCmd->ElemCount),
                sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                indexOffset
            );
        }
    }

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    if (!blendEnabled) glDisable(GL_BLEND);
    if (cullEnabled) glEnable(GL_CULL_FACE);
    if (depthEnabled) glEnable(GL_DEPTH_TEST);
    if (!scissorEnabled) glDisable(GL_SCISSOR_TEST);
    glUseProgram(static_cast<GLuint>(lastProgram));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(lastTexture));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(lastArrayBuffer));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(lastElementArrayBuffer));
    glViewport(lastViewport[0], lastViewport[1], lastViewport[2], lastViewport[3]);
    glScissor(lastScissorBox[0], lastScissorBox[1], lastScissorBox[2], lastScissorBox[3]);
}
