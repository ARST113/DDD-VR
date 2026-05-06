#include "OpenXrRenderer.h"
#include "../util/XrLog.h"
bool OpenXrRenderer::initialize(){ video_.create(); XR_LOGI("DDDVR/OpenXRRenderer","frame loop started"); return true; }
void OpenXrRenderer::renderFrame(){ screen_.render(); }
