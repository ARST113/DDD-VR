#include "CinemaScreenRenderer.h"
#include "../util/XrLog.h"

static GLuint compileShader(GLenum t, const char* src){
    GLuint sh=glCreateShader(t); glShaderSource(sh,1,&src,nullptr); glCompileShader(sh);
    GLint ok=0; glGetShaderiv(sh,GL_COMPILE_STATUS,&ok); if(!ok){char log[512]; glGetShaderInfoLog(sh,512,nullptr,log); XR_LOGE("DDDVR/OpenXRRenderer","shader compile error: %s",log);} return sh;
}

bool CinemaScreenRenderer::initialize(){
    const char* vs = "#version 300 es\nlayout(location=0) in vec2 aPos; void main(){ gl_Position=vec4(aPos,0.0,1.0);}";
    const char* fs = "#version 300 es\nprecision mediump float; uniform vec3 uColor; out vec4 fragColor; void main(){ fragColor=vec4(uColor,1.0);}";
    GLuint v=compileShader(GL_VERTEX_SHADER,vs), f=compileShader(GL_FRAGMENT_SHADER,fs);
    program_ = glCreateProgram(); glAttachShader(program_,v); glAttachShader(program_,f); glLinkProgram(program_);
    glDeleteShader(v); glDeleteShader(f);
    GLint linked=0; glGetProgramiv(program_,GL_LINK_STATUS,&linked); if(!linked){char log[512]; glGetProgramInfoLog(program_,512,nullptr,log); XR_LOGE("DDDVR/OpenXRRenderer","program link error: %s",log); return false;}
    colorLoc_=glGetUniformLocation(program_,"uColor");
    const GLfloat quad[] = {-0.6f,-0.4f, 0.6f,-0.4f, -0.6f,0.4f, 0.6f,-0.4f, 0.6f,0.4f, -0.6f,0.4f};
    glGenBuffers(1,&vbo_); glBindBuffer(GL_ARRAY_BUFFER,vbo_); glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
    return true;
}

void CinemaScreenRenderer::render(float r,float g,float b){
    glUseProgram(program_);
    glUniform3f(colorLoc_, r,g,b);
    glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,(void*)0);
    glDrawArrays(GL_TRIANGLES,0,6);
    glDisableVertexAttribArray(0);
}
