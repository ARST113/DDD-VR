#include "CinemaScreenRenderer.h"
#include <GLES3/gl3.h>

void CinemaScreenRenderer::render(){
    static const GLfloat quad[] = {
        -0.6f,-0.4f,  0.6f,-0.4f, -0.6f,0.4f,
         0.6f,-0.4f,  0.6f,0.4f, -0.6f,0.4f
    };
    GLuint vbo=0; glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo); glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,(void*)0);
    glDrawArrays(GL_TRIANGLES,0,6);
    glDisableVertexAttribArray(0);
    glDeleteBuffers(1,&vbo);
}
