#include "CinemaScreenRenderer.h"
#include "../util/XrLog.h"
#include <cmath>
#include <vector>

namespace {
constexpr int kScreenSegments = 48;
constexpr float kUiPlaneOffsetMeters = 0.12f;
constexpr float kUiPanelWidthScale = 1.28f;
constexpr float kUiPanelYOffsetMeters = -0.36f;
constexpr float kUiPanelHeightMeters = 0.32f;
constexpr float kUiProgressYOffsetMeters = -0.105f;
constexpr float kUiProgressWidthScale = 0.74f;
constexpr float kUiPlayButtonWidthMeters = 0.34f;
constexpr float kUiPlayButtonHeightMeters = 0.18f;
constexpr float kUiPlayButtonYOffsetMeters = 0.055f;
}

static GLuint compileShader(GLenum t, const char* src){
    GLuint sh=glCreateShader(t); glShaderSource(sh,1,&src,nullptr); glCompileShader(sh);
    GLint ok=0; glGetShaderiv(sh,GL_COMPILE_STATUS,&ok); if(!ok){char log[512]; glGetShaderInfoLog(sh,512,nullptr,log); XR_LOGE("DDDVR/OpenXRRenderer","shader compile error: %s",log);} return sh;
}

bool CinemaScreenRenderer::initialize(float screenWidthMeters, float screenDistanceMeters, float curveRadians){
    const char* vs =
        "#version 300 es\n"
        "layout(location=0) in vec3 aPos;"
        "layout(location=1) in vec2 aTexCoord;"
        "uniform mat4 uMvp;"
        "out vec2 vTexCoord;"
        "void main(){"
        "  gl_Position=uMvp*vec4(aPos,1.0);"
        "  vTexCoord=aTexCoord;"
        "}";
    const char* fs =
        "#version 300 es\n"
        "#extension GL_OES_EGL_image_external_essl3 : require\n"
        "precision highp float;"
        "in vec2 vTexCoord;"
        "uniform samplerExternalOES uTexture;"
        "uniform mat4 uTexMatrix;"
        "uniform vec4 uUvRect;"
        "uniform bool uHasVideo;"
        "uniform vec3 uFallbackColor;"
        "out vec4 fragColor;"
        "void main(){"
        "  if(!uHasVideo){ fragColor=vec4(uFallbackColor,1.0); return; }"
        "  vec2 local=vec2(vTexCoord.x, 1.0-vTexCoord.y);"
        "  vec2 mapped=vec2(local.x*uUvRect.z+uUvRect.x, local.y*uUvRect.w+uUvRect.y);"
        "  vec2 uv=(uTexMatrix*vec4(mapped,0.0,1.0)).xy;"
        "  fragColor=texture(uTexture,uv);"
        "}";
    GLuint v=compileShader(GL_VERTEX_SHADER,vs), f=compileShader(GL_FRAGMENT_SHADER,fs);
    program_ = glCreateProgram(); glAttachShader(program_,v); glAttachShader(program_,f); glLinkProgram(program_);
    glDeleteShader(v); glDeleteShader(f);
    GLint linked=0; glGetProgramiv(program_,GL_LINK_STATUS,&linked); if(!linked){char log[512]; glGetProgramInfoLog(program_,512,nullptr,log); XR_LOGE("DDDVR/OpenXRRenderer","program link error: %s",log); return false;}
    mvpLoc_=glGetUniformLocation(program_,"uMvp");
    texMatrixLoc_=glGetUniformLocation(program_,"uTexMatrix");
    textureLoc_=glGetUniformLocation(program_,"uTexture");
    uvRectLoc_=glGetUniformLocation(program_,"uUvRect");
    hasVideoLoc_=glGetUniformLocation(program_,"uHasVideo");
    fallbackColorLoc_=glGetUniformLocation(program_,"uFallbackColor");

    halfWidthMeters_ = screenWidthMeters * 0.5f;
    halfHeightMeters_ = screenWidthMeters * (9.f / 16.f) * 0.5f;
    centerX_ = 0.f;
    centerY_ = 0.f;
    centerZ_ = -screenDistanceMeters;
    curveRadians_ = curveRadians;
    glGenBuffers(1,&videoVbo_);
    rebuildVideoMesh();
    glGenBuffers(1,&overlayVbo_);
    return true;
}

void CinemaScreenRenderer::setPlacement(float yawRadians, float centerX, float centerY, float centerZ, float curveRadians) {
    yawRadians_ = yawRadians;
    centerX_ = centerX;
    centerY_ = centerY;
    centerZ_ = centerZ;
    curveRadians_ = curveRadians;
    rebuildVideoMesh();
}

void CinemaScreenRenderer::rebuildVideoMesh() {
    std::vector<GLfloat> vertices;
    vertices.reserve(kScreenSegments * 6 * 5);
    const float width = halfWidthMeters_ * 2.f;
    for (int segment = 0; segment < kScreenSegments; ++segment) {
        const float t0 = static_cast<float>(segment) / static_cast<float>(kScreenSegments);
        const float t1 = static_cast<float>(segment + 1) / static_cast<float>(kScreenSegments);
        const float x0 = -halfWidthMeters_ + width * t0;
        const float x1 = -halfWidthMeters_ + width * t1;
        appendVertex(vertices, x0, -halfHeightMeters_, t0, 1.f);
        appendVertex(vertices, x1, -halfHeightMeters_, t1, 1.f);
        appendVertex(vertices, x0,  halfHeightMeters_, t0, 0.f);
        appendVertex(vertices, x1, -halfHeightMeters_, t1, 1.f);
        appendVertex(vertices, x1,  halfHeightMeters_, t1, 0.f);
        appendVertex(vertices, x0,  halfHeightMeters_, t0, 0.f);
    }
    videoVertexCount_ = static_cast<GLsizei>(vertices.size() / 5);
    glBindBuffer(GL_ARRAY_BUFFER, videoVbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GLfloat), vertices.data(), GL_STATIC_DRAW);
}

void CinemaScreenRenderer::appendVertex(
    std::vector<GLfloat>& vertices,
    float localX,
    float localY,
    float u,
    float v
) const {
    float x = localX;
    float z = 0.f;
    const float curve = curveRadians_ < 0.f ? 0.f : curveRadians_;
    if (curve > 0.001f && halfWidthMeters_ > 0.001f) {
        const float radius = (halfWidthMeters_ * 2.f) / curve;
        const float angle = (localX / halfWidthMeters_) * (curve * 0.5f);
        x = std::sin(angle) * radius;
        z = radius * (1.f - std::cos(angle));
    }

    const float c = std::cos(yawRadians_);
    const float s = std::sin(yawRadians_);
    const float worldX = centerX_ + c * x - s * z;
    const float worldZ = centerZ_ + s * x + c * z;
    vertices.push_back(worldX);
    vertices.push_back(localY + centerY_);
    vertices.push_back(worldZ);
    vertices.push_back(u);
    vertices.push_back(v);
}

void CinemaScreenRenderer::appendFlatVertex(
    std::vector<GLfloat>& vertices,
    float localX,
    float localY,
    float localZ,
    float u,
    float v
) const {
    const float c = std::cos(yawRadians_);
    const float s = std::sin(yawRadians_);
    const float worldX = centerX_ + c * localX - s * localZ;
    const float worldZ = centerZ_ + s * localX + c * localZ;
    vertices.push_back(worldX);
    vertices.push_back(localY + centerY_);
    vertices.push_back(worldZ);
    vertices.push_back(u);
    vertices.push_back(v);
}

void CinemaScreenRenderer::renderVideo(
    GLuint videoTexture,
    const float* mvp,
    const float* texMatrix,
    const CinemaUvRect& uvRect,
    bool hasVideo,
    float fallbackR,
    float fallbackG,
    float fallbackB
){
    glUseProgram(program_);
    glUniformMatrix4fv(mvpLoc_, 1, GL_FALSE, mvp);
    glUniformMatrix4fv(texMatrixLoc_, 1, GL_FALSE, texMatrix);
    glUniform4f(uvRectLoc_, uvRect.uOffset, uvRect.vOffset, uvRect.uScale, uvRect.vScale);
    glUniform1i(hasVideoLoc_, hasVideo ? 1 : 0);
    glUniform3f(fallbackColorLoc_, fallbackR, fallbackG, fallbackB);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, videoTexture);
    glUniform1i(textureLoc_, 0);
    glBindBuffer(GL_ARRAY_BUFFER,videoVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5 * sizeof(GLfloat),(void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5 * sizeof(GLfloat),(void*)(3 * sizeof(GLfloat)));
    glDrawArrays(GL_TRIANGLES,0,videoVertexCount_);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

void CinemaScreenRenderer::renderUiOverlay(
    const float* mvp,
    int progressPermille,
    bool playing,
    CinemaUiHoverTarget hoverTarget
){
    const float safeProgress = progressPermille <= 0 ? 0.f :
        (progressPermille >= 1000 ? 1.f : progressPermille / 1000.f);
    const float panelY = -halfHeightMeters_ + kUiPanelYOffsetMeters;
    const float panelW = halfWidthMeters_ * kUiPanelWidthScale;
    const float panelH = kUiPanelHeightMeters;
    const float progressY = panelY + kUiProgressYOffsetMeters;
    const float progressTrackW = panelW * kUiProgressWidthScale;
    const float progressH = 0.026f;
    const float playY = panelY + kUiPlayButtonYOffsetMeters;
    const bool hoverProgress = hoverTarget == CinemaUiHoverTarget::Progress;
    const bool hoverPlay = hoverTarget == CinemaUiHoverTarget::PlayPause;
    const bool hoverPanel = hoverTarget == CinemaUiHoverTarget::Panel || hoverTarget == CinemaUiHoverTarget::Video;

    if (hoverPanel || hoverProgress || hoverPlay) {
        renderSolidRect(mvp, 0.f, panelY, panelW + 0.045f, panelH + 0.045f, 0.025f, 0.050f, 0.075f, kUiPlaneOffsetMeters - 0.004f);
    }
    renderSolidRect(mvp, 0.f, panelY, panelW, panelH, 0.014f, 0.015f, 0.018f, kUiPlaneOffsetMeters);
    renderSolidRect(mvp, -panelW * 0.435f, panelY, panelW * 0.11f, panelH * 0.92f, 0.06f, 0.12f, 0.22f, kUiPlaneOffsetMeters + 0.004f);

    if (hoverPlay) {
        renderSolidRect(mvp, 0.f, playY, kUiPlayButtonWidthMeters, kUiPlayButtonHeightMeters, 0.08f, 0.20f, 0.36f, kUiPlaneOffsetMeters + 0.006f);
    }

    if (hoverProgress) {
        renderSolidRect(mvp, 0.f, progressY, progressTrackW + 0.10f, progressH + 0.055f, 0.055f, 0.15f, 0.26f, kUiPlaneOffsetMeters + 0.005f);
    }
    renderSolidRect(mvp, 0.f, progressY, progressTrackW, progressH, 0.075f, 0.080f, 0.095f, kUiPlaneOffsetMeters + 0.006f);
    if (safeProgress > 0.f) {
        const float progressW = progressTrackW * safeProgress;
        const float progressX = -progressTrackW * 0.5f + progressW * 0.5f;
        renderSolidRect(mvp, progressX, progressY, progressW, progressH, 0.30f, 0.52f, 1.0f, kUiPlaneOffsetMeters + 0.010f);
        const float knobX = -progressTrackW * 0.5f + progressTrackW * safeProgress;
        const float knobSize = hoverProgress ? 0.066f : 0.050f;
        renderSolidRect(mvp, knobX, progressY, knobSize, knobSize, 0.86f, 0.92f, 1.0f, kUiPlaneOffsetMeters + 0.014f);
    }

    if (playing) {
        renderSolidRect(mvp, -0.025f, playY, 0.026f, 0.090f, 0.88f, 0.92f, 1.0f, kUiPlaneOffsetMeters + 0.014f);
        renderSolidRect(mvp, 0.025f, playY, 0.026f, 0.090f, 0.88f, 0.92f, 1.0f, kUiPlaneOffsetMeters + 0.014f);
    } else {
        renderSolidRect(mvp, -0.015f, playY, 0.030f, 0.100f, 0.36f, 0.64f, 1.0f, kUiPlaneOffsetMeters + 0.014f);
        renderSolidRect(mvp, 0.025f, playY, 0.030f, 0.075f, 0.36f, 0.64f, 1.0f, kUiPlaneOffsetMeters + 0.014f);
        renderSolidRect(mvp, 0.055f, playY, 0.030f, 0.050f, 0.36f, 0.64f, 1.0f, kUiPlaneOffsetMeters + 0.014f);
    }

    renderSolidRect(mvp, -panelW * 0.23f, playY, 0.020f, 0.070f, 0.58f, 0.64f, 0.72f, kUiPlaneOffsetMeters + 0.014f);
    renderSolidRect(mvp, panelW * 0.23f, playY, 0.020f, 0.070f, 0.58f, 0.64f, 0.72f, kUiPlaneOffsetMeters + 0.014f);
    renderSolidRect(mvp, panelW * 0.39f, playY, 0.018f, 0.018f, 0.62f, 0.66f, 0.73f, kUiPlaneOffsetMeters + 0.014f);
    renderSolidRect(mvp, panelW * 0.43f, playY, 0.018f, 0.018f, 0.62f, 0.66f, 0.73f, kUiPlaneOffsetMeters + 0.014f);
    renderSolidRect(mvp, panelW * 0.47f, playY, 0.018f, 0.018f, 0.62f, 0.66f, 0.73f, kUiPlaneOffsetMeters + 0.014f);
}

void CinemaScreenRenderer::renderGrabHighlight(const float* mvp) {
    const float panelY = -halfHeightMeters_ + kUiPanelYOffsetMeters;
    const float panelW = halfWidthMeters_ * kUiPanelWidthScale;
    const float topEdgeY = panelY + kUiPanelHeightMeters * 0.5f - 0.018f;
    renderSolidRect(mvp, 0.f, topEdgeY, panelW * 0.84f, 0.018f, 0.10f, 0.46f, 1.0f, kUiPlaneOffsetMeters + 0.018f);
}

void CinemaScreenRenderer::renderSolidRect(
    const float* mvp,
    float centerX,
    float centerY,
    float width,
    float height,
    float r,
    float g,
    float b,
    float localZ
){
    const float left = centerX - width * 0.5f;
    const float right = centerX + width * 0.5f;
    const float bottom = centerY - height * 0.5f;
    const float top = centerY + height * 0.5f;
    std::vector<GLfloat> quad;
    quad.reserve(30);
    appendFlatVertex(quad, left,  bottom, localZ, 0.f, 0.f);
    appendFlatVertex(quad, right, bottom, localZ, 1.f, 0.f);
    appendFlatVertex(quad, left,  top,    localZ, 0.f, 1.f);
    appendFlatVertex(quad, right, bottom, localZ, 1.f, 0.f);
    appendFlatVertex(quad, right, top,    localZ, 1.f, 1.f);
    appendFlatVertex(quad, left,  top,    localZ, 0.f, 1.f);
    static const float identity[16] = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f
    };
    glUseProgram(program_);
    glUniformMatrix4fv(mvpLoc_, 1, GL_FALSE, mvp);
    glUniformMatrix4fv(texMatrixLoc_, 1, GL_FALSE, identity);
    glUniform4f(uvRectLoc_, 0.f, 0.f, 1.f, 1.f);
    glUniform1i(hasVideoLoc_, 0);
    glUniform3f(fallbackColorLoc_, r, g, b);
    glBindBuffer(GL_ARRAY_BUFFER, overlayVbo_);
    glBufferData(GL_ARRAY_BUFFER, quad.size() * sizeof(GLfloat), quad.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5 * sizeof(GLfloat),(void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5 * sizeof(GLfloat),(void*)(3 * sizeof(GLfloat)));
    glDrawArrays(GL_TRIANGLES,0,6);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

void CinemaScreenRenderer::renderRay(
    const float* mvp,
    const float start[3],
    const float end[3],
    float r,
    float g,
    float b
) {
    const GLfloat line[] = {
        start[0], start[1], start[2], 0.f, 0.f,
        end[0], end[1], end[2], 1.f, 1.f,
    };
    static const float identity[16] = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f
    };
    glUseProgram(program_);
    glUniformMatrix4fv(mvpLoc_, 1, GL_FALSE, mvp);
    glUniformMatrix4fv(texMatrixLoc_, 1, GL_FALSE, identity);
    glUniform4f(uvRectLoc_, 0.f, 0.f, 1.f, 1.f);
    glUniform1i(hasVideoLoc_, 0);
    glUniform3f(fallbackColorLoc_, r, g, b);
    glBindBuffer(GL_ARRAY_BUFFER, overlayVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(line), line, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void*)(3 * sizeof(GLfloat)));
    glLineWidth(3.f);
    glDrawArrays(GL_LINES, 0, 2);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

void CinemaScreenRenderer::renderCursorDot(
    const float* mvp,
    const float center[3],
    const float right[3],
    const float up[3],
    float radiusMeters,
    bool active
) {
    auto drawQuad = [&](float radius, float r, float g, float b) {
        const float lx[3] = {
            right[0] * radius,
            right[1] * radius,
            right[2] * radius
        };
        const float ly[3] = {
            up[0] * radius,
            up[1] * radius,
            up[2] * radius
        };
        const GLfloat quad[] = {
            center[0] - lx[0] - ly[0], center[1] - lx[1] - ly[1], center[2] - lx[2] - ly[2], 0.f, 0.f,
            center[0] + lx[0] - ly[0], center[1] + lx[1] - ly[1], center[2] + lx[2] - ly[2], 1.f, 0.f,
            center[0] - lx[0] + ly[0], center[1] - lx[1] + ly[1], center[2] - lx[2] + ly[2], 0.f, 1.f,
            center[0] + lx[0] - ly[0], center[1] + lx[1] - ly[1], center[2] + lx[2] - ly[2], 1.f, 0.f,
            center[0] + lx[0] + ly[0], center[1] + lx[1] + ly[1], center[2] + lx[2] + ly[2], 1.f, 1.f,
            center[0] - lx[0] + ly[0], center[1] - lx[1] + ly[1], center[2] - lx[2] + ly[2], 0.f, 1.f,
        };
        static const float identity[16] = {
            1.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            0.f, 0.f, 0.f, 1.f
        };
        glUseProgram(program_);
        glUniformMatrix4fv(mvpLoc_, 1, GL_FALSE, mvp);
        glUniformMatrix4fv(texMatrixLoc_, 1, GL_FALSE, identity);
        glUniform4f(uvRectLoc_, 0.f, 0.f, 1.f, 1.f);
        glUniform1i(hasVideoLoc_, 0);
        glUniform3f(fallbackColorLoc_, r, g, b);
        glBindBuffer(GL_ARRAY_BUFFER, overlayVbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void*)(3 * sizeof(GLfloat)));
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
    };

    const float outer = active ? radiusMeters * 1.55f : radiusMeters * 1.25f;
    drawQuad(outer, 0.10f, 0.85f, 1.0f);
    drawQuad(radiusMeters, 0.92f, 0.97f, 1.0f);
}
