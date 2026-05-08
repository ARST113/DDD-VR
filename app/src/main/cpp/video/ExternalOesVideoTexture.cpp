#include "ExternalOesVideoTexture.h"
#include "../util/XrLog.h"
bool ExternalOesVideoTexture::create(){glGenTextures(1,&texId_);XR_LOGI("DDDVR/OpenXRVideo","video texture created id=%u",texId_);return texId_!=0;}
