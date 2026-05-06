#pragma once
#include "../video/ExternalOesVideoTexture.h"
#include "../gl/CinemaScreenRenderer.h"
class OpenXrRenderer {
public:
    bool initialize();
    void renderFrame();
    unsigned int videoTextureId() const { return video_.id(); }
private:
    ExternalOesVideoTexture video_;
    CinemaScreenRenderer screen_;
};
