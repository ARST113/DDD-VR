#pragma once
#include <GLES3/gl3.h>

class GlProgram {
public:
    bool create(const char* vs, const char* fs);
    void use() const;
    GLuint id() const { return program_; }
private:
    GLuint program_ = 0;
};
