#include "OpenXrRenderer.h"
#include "../util/XrLog.h"

bool OpenXrRenderer::initialize(){
    video_.create();
    XR_LOGI("DDDVR/OpenXRRenderer","renderer initialized");
    return true;
}

void OpenXrRenderer::renderEye(int eye, int width, int height){
    glViewport(0,0,width,height);
    if (eye == 0) glClearColor(0.1f,0.2f,0.6f,1.f);
    else glClearColor(0.6f,0.2f,0.1f,1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    screen_.render();
}
