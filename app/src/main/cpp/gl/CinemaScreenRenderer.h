#pragma once
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <vector>
#include "../video/FfmpegVideoTexture.h"
#include "DolbyMappingTexture.h"

struct CinemaUvRect {
    float uOffset = 0.f;
    float vOffset = 0.f;
    float uScale = 1.f;
    float vScale = 1.f;
};

enum class CinemaUiHoverTarget {
    None = 0,
    Video = 1,
    Panel = 2,
    PlayPause = 3,
    Progress = 4
};

class CinemaScreenRenderer {
public:
    bool initialize(float screenWidthMeters, float screenDistanceMeters, float curveRadians);
    void setPlacement(float yawRadians, float centerX, float centerY, float centerZ, float curveRadians);
    void setAspectRatio(float aspectRatio);
    void setColorControls(float brightness, float contrast, float saturation, float gamma);
    void setHdrLookEnabled(bool enabled);
    void renderVideo(
        GLuint videoTexture,
        const float* mvp,
        const float* texMatrix,
        const CinemaUvRect& uvRect,
        bool hasVideo,
        float fallbackR,
        float fallbackG,
        float fallbackB,
        const FfmpegVideoTextureSet* colorMetadata = nullptr
    );
    void renderFfmpegVideo(
        const FfmpegVideoTextureSet& textures,
        const float* mvp,
        const CinemaUvRect& uvRect,
        bool hasVideo,
        float fallbackR,
        float fallbackG,
        float fallbackB
    );
    void renderUiOverlay(
        const float* mvp,
        int progressPermille,
        bool playing,
        CinemaUiHoverTarget hoverTarget
    );
    void renderGrabHighlight(const float* mvp);
    void renderRay(
        const float* mvp,
        const float start[3],
        const float end[3],
        float r,
        float g,
        float b
    );
    void renderCursorDot(
        const float* mvp,
        const float center[3],
        const float right[3],
        const float up[3],
        float radiusMeters,
        bool active
    );
private:
    void rebuildVideoMesh();
    void appendVertex(
        std::vector<GLfloat>& vertices,
        float localX,
        float localY,
        float u,
        float v
    ) const;
    void renderSolidRect(
        const float* mvp,
        float centerX,
        float centerY,
        float width,
        float height,
        float r,
        float g,
        float b,
        float localZ = 0.12f
    );
    void appendFlatVertex(
        std::vector<GLfloat>& vertices,
        float localX,
        float localY,
        float localZ,
        float u,
        float v
    ) const;
    GLuint program_ = 0;
    GLuint ffmpegProgram_ = 0;
    GLuint videoVbo_ = 0;
    GLuint overlayVbo_ = 0;
    GLint mvpLoc_ = -1;
    GLint texMatrixLoc_ = -1;
    GLint textureLoc_ = -1;
    GLint uvRectLoc_ = -1;
    GLint hasVideoLoc_ = -1;
    GLint fallbackColorLoc_ = -1;
    GLint colorParamsLoc_ = -1;
    GLint hdrTransferLoc_ = -1;
    GLint dolbyProfileLoc_ = -1;
    GLint hdrPowerValueLoc_ = -1;
    GLint hdrColorMatrixLoc_ = -1;
    GLint colorFixLoc_ = -1;
    GLint dolbyMappingEnabledLoc_ = -1;
    GLint dolbyMappingKindLoc_ = -1;
    GLint dolbySampler2DLoc_[3] = {-1, -1, -1};
    GLint dolbySampler3DLoc_[3] = {-1, -1, -1};
    GLint dolbyColorEnabledLoc_ = -1;
    GLint dolbyInputColorInverseLoc_ = -1;
    GLint dolbyYccToRgbLoc_ = -1;
    GLint dolbyYccOffsetLoc_ = -1;
    GLint dolbyColorMatrixLoc_ = -1;
    GLint dolbyBlFullRangeLoc_ = -1;
    GLint ffmpegMvpLoc_ = -1;
    GLint ffmpegUvRectLoc_ = -1;
    GLint ffmpegHasVideoLoc_ = -1;
    GLint ffmpegFallbackColorLoc_ = -1;
    GLint ffmpegColorParamsLoc_ = -1;
    GLint ffmpegPlane0Loc_ = -1;
    GLint ffmpegPlane1Loc_ = -1;
    GLint ffmpegPlane2Loc_ = -1;
    GLint ffmpegFormatLoc_ = -1;
    GLint ffmpegTransferLoc_ = -1;
    GLint ffmpegPrimariesLoc_ = -1;
    GLint ffmpegRangeLoc_ = -1;
    GLint ffmpegDolbyLoc_ = -1;
    GLint ffmpegDolbyProfileLoc_ = -1;
    GLint ffmpegHdrPowerValueLoc_ = -1;
    GLint ffmpegHdrColorMatrixLoc_ = -1;
    GLint ffmpegColorFixLoc_ = -1;
    GLint ffmpegDolbyMappingEnabledLoc_ = -1;
    GLint ffmpegDolbyMappingKindLoc_ = -1;
    GLint ffmpegDolbySampler2DLoc_[3] = {-1, -1, -1};
    GLint ffmpegDolbySampler3DLoc_[3] = {-1, -1, -1};
    GLint ffmpegDolbyColorEnabledLoc_ = -1;
    GLint ffmpegDolbyYccToRgbLoc_ = -1;
    GLint ffmpegDolbyYccOffsetLoc_ = -1;
    GLint ffmpegDolbyColorMatrixLoc_ = -1;
    GLint ffmpegDolbyBlFullRangeLoc_ = -1;
    DolbyMappingTexture dolbyMappingTexture_;
    GLsizei videoVertexCount_ = 0;
    float halfWidthMeters_ = 0.f;
    float halfHeightMeters_ = 0.f;
    float centerX_ = 0.f;
    float centerY_ = 0.f;
    float centerZ_ = 0.f;
    float yawRadians_ = 0.f;
    float curveRadians_ = 0.f;
    float brightness_ = 1.f;
    float contrast_ = 1.f;
    float saturation_ = 1.f;
    float gamma_ = 1.f;
    bool hdrLookEnabled_ = false;
    int externalHdrLogKey_ = -1;
    int planarHdrLogKey_ = -1;
};
