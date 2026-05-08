#pragma once
#include <GLES3/gl3.h>

class ExternalOesVideoTexture {
public:
    bool create();
    GLuint id() const { return texId_; }
private:
    GLuint texId_ = 0;
};
