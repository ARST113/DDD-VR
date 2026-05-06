#pragma once
#include "../video/ExternalOesVideoTexture.h"
#include "../gl/CinemaScreenRenderer.h"
#include <GLES3/gl3.h>

class OpenXrRenderer {
public:
    bool initialize();
    void renderEye(int eye, int width, int height);
    unsigned int videoTextureId() const { return video_.id(); }
private:
    ExternalOesVideoTexture video_;
    CinemaScreenRenderer screen_;
};
