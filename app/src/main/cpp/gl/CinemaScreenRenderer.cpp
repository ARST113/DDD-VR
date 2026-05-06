#include "CinemaScreenRenderer.h"
#include <GLES3/gl3.h>
void CinemaScreenRenderer::render(){ glClearColor(0.04f,0.04f,0.08f,1.f); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);} 
