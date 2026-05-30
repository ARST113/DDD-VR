#pragma once
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <vector>

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
    void renderVideo(
        GLuint videoTexture,
        const float* mvp,
        const float* texMatrix,
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
    GLuint videoVbo_ = 0;
    GLuint overlayVbo_ = 0;
    GLint mvpLoc_ = -1;
    GLint texMatrixLoc_ = -1;
    GLint textureLoc_ = -1;
    GLint uvRectLoc_ = -1;
    GLint hasVideoLoc_ = -1;
    GLint fallbackColorLoc_ = -1;
    GLsizei videoVertexCount_ = 0;
    float halfWidthMeters_ = 0.f;
    float halfHeightMeters_ = 0.f;
    float centerX_ = 0.f;
    float centerY_ = 0.f;
    float centerZ_ = 0.f;
    float yawRadians_ = 0.f;
    float curveRadians_ = 0.f;
};
