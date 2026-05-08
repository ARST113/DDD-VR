#pragma once

#include "OpenXrPlatform.h"

namespace dddvr::openxr {

void setJavaVm(JavaVM* vm);
bool setApplicationContext(JNIEnv* env, jobject context);
bool hasJavaVm();
bool hasApplicationContext();
bool setApplicationActivity(JNIEnv* env, jobject activity);
bool hasApplicationActivity();
JavaVM* javaVm();
jobject applicationActivity();
XrResult initializeLoader();
const char* loaderInitStatus();

} // namespace dddvr::openxr
