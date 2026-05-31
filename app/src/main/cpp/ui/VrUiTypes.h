#pragma once

#include "../openxr/OpenXrPlatform.h"

struct VrUiVec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

struct VrUiPlane {
    VrUiVec3 center;
    VrUiVec3 right{1.f, 0.f, 0.f};
    VrUiVec3 up{0.f, 1.f, 0.f};
    VrUiVec3 normal{0.f, 0.f, 1.f};
    float yawRadians = 0.f;
    float widthMeters = 1.8f;
    float heightMeters = 0.55f;
};

struct VrRayHit {
    bool hit = false;
    int hand = -1;
    float pixelX = 0.f;
    float pixelY = 0.f;
    float worldX = 0.f;
    float worldY = 0.f;
    float worldZ = 0.f;
};
