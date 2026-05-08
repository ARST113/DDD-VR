#pragma once

#include <jni.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#ifndef DDDVR_HAS_OPENXR
#error "DDDVR_HAS_OPENXR must be defined"
#endif
