#pragma once

namespace Inferno {
    struct HitInfo;

    struct BoundingCapsule {
        Vector3 A, B;
        float Radius;

        HitInfo Intersects(const DirectX::BoundingSphere& sphere) const;
        bool Intersects(const BoundingCapsule& other) const;
        bool Intersects(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& faceNormal, Vector3& refPoint, Vector3& normal, float& dist) const;
    };
}
