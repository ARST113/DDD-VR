#pragma once
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

class ExternalOesVideoTexture {
public:
    bool create();
    GLuint id() const { return texId_; }
private:
    GLuint texId_ = 0;
};
