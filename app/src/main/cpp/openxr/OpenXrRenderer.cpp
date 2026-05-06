#include "OpenXrRenderer.h"
#include "../util/XrLog.h"

bool OpenXrRenderer::initialize(){
    video_.create();
    screen_.initialize();
    XR_LOGI("DDDVR/OpenXRRenderer","renderer initialized");
    return true;
}

void OpenXrRenderer::renderEye(int eye, int width, int height){
    glViewport(0,0,width,height);
    glClearColor(0.02f,0.02f,0.02f,1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (eye == 0) screen_.render(0.1f,0.6f,1.0f);
    else screen_.render(1.0f,0.5f,0.1f);
}
