#pragma once
#include <GLES3/gl3.h>

class CinemaScreenRenderer {
public:
    bool initialize();
    void render(float r, float g, float b);
private:
    GLuint program_ = 0;
    GLuint vbo_ = 0;
    GLint colorLoc_ = -1;
};
