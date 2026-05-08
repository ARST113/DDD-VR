#pragma once

#include "OpenXrPlatform.h"

namespace dddvr::openxr {

void setJavaVm(JavaVM* javaVm);
bool setApplicationContext(JNIEnv* env, jobject context);
bool initializeLoader();

} // namespace dddvr::openxr
