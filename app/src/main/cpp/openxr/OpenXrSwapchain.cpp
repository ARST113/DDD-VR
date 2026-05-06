#include "OpenXrSwapchain.h"
#include "../util/XrLog.h"
bool OpenXrSwapchain::create(){ XR_LOGI("DDDVR/OpenXRSession","swapchain created"); return true; }
int OpenXrSwapchain::acquireImage(){ return 0; }
void OpenXrSwapchain::releaseImage(){}
