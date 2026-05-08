#include "GlProgram.h"
#include "../util/XrLog.h"
static GLuint compile(GLenum t, const char* s){GLuint sh=glCreateShader(t); glShaderSource(sh,1,&s,nullptr); glCompileShader(sh); return sh;}
bool GlProgram::create(const char* vs,const char* fs){auto v=compile(GL_VERTEX_SHADER,vs);auto f=compile(GL_FRAGMENT_SHADER,fs);program_=glCreateProgram();glAttachShader(program_,v);glAttachShader(program_,f);glLinkProgram(program_);glDeleteShader(v);glDeleteShader(f);XR_LOGI("DDDVR/OpenXRRenderer","shader linked");return program_!=0;}
void GlProgram::use() const { glUseProgram(program_);}
