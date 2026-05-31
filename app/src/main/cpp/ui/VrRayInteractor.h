#pragma once

#include "VrUiTypes.h"

class VrRayInteractor {
public:
    VrRayHit hitTest(
        const XrPosef& aimPose,
        const VrUiPlane& plane,
        int textureWidth,
        int textureHeight,
        int hand = -1
    ) const;
};

