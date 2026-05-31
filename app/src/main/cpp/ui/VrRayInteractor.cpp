#include "VrRayInteractor.h"

#include <cmath>

namespace {
VrUiVec3 rotateByQuat(const XrQuaternionf& q, float vx, float vy, float vz) {
    const float tx = 2.f * (q.y * vz - q.z * vy);
    const float ty = 2.f * (q.z * vx - q.x * vz);
    const float tz = 2.f * (q.x * vy - q.y * vx);
    return {
        vx + q.w * tx + (q.y * tz - q.z * ty),
        vy + q.w * ty + (q.z * tx - q.x * tz),
        vz + q.w * tz + (q.x * ty - q.y * tx)
    };
}
}

VrRayHit VrRayInteractor::hitTest(
    const XrPosef& aimPose,
    const VrUiPlane& plane,
    int textureWidth,
    int textureHeight,
    int hand
) const {
    VrRayHit out{};
    out.hand = hand;
    if (textureWidth <= 0 || textureHeight <= 0 ||
        plane.widthMeters <= 0.f || plane.heightMeters <= 0.f) {
        return out;
    }

    VrUiVec3 direction = rotateByQuat(aimPose.orientation, 0.f, 0.f, -1.f);
    const float directionLength = std::sqrt(
        direction.x * direction.x +
        direction.y * direction.y +
        direction.z * direction.z
    );
    if (directionLength <= 0.001f) return out;
    direction.x /= directionLength;
    direction.y /= directionLength;
    direction.z /= directionLength;

    const float denom =
        direction.x * plane.normal.x +
        direction.y * plane.normal.y +
        direction.z * plane.normal.z;
    if (std::fabs(denom) <= 0.001f) return out;

    const VrUiVec3 toPlane{
        plane.center.x - aimPose.position.x,
        plane.center.y - aimPose.position.y,
        plane.center.z - aimPose.position.z
    };
    const float t =
        (toPlane.x * plane.normal.x +
         toPlane.y * plane.normal.y +
         toPlane.z * plane.normal.z) / denom;
    if (t <= 0.f) return out;

    const float hitX = aimPose.position.x + direction.x * t;
    const float hitY = aimPose.position.y + direction.y * t;
    const float hitZ = aimPose.position.z + direction.z * t;
    const VrUiVec3 delta{
        hitX - plane.center.x,
        hitY - plane.center.y,
        hitZ - plane.center.z
    };
    const float localX =
        delta.x * plane.right.x +
        delta.y * plane.right.y +
        delta.z * plane.right.z;
    const float localY =
        delta.x * plane.up.x +
        delta.y * plane.up.y +
        delta.z * plane.up.z;
    const float halfWidth = plane.widthMeters * 0.5f;
    const float halfHeight = plane.heightMeters * 0.5f;
    if (localX < -halfWidth || localX > halfWidth ||
        localY < -halfHeight || localY > halfHeight) {
        return out;
    }

    out.hit = true;
    out.pixelX = ((localX + halfWidth) / plane.widthMeters) * static_cast<float>(textureWidth);
    out.pixelY = ((halfHeight - localY) / plane.heightMeters) * static_cast<float>(textureHeight);
    out.worldX = hitX;
    out.worldY = hitY;
    out.worldZ = hitZ;
    return out;
}
