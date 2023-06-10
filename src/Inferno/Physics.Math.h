#pragma once

#include "Types.h"
#include "DirectX.h"
#include "Face.h"

namespace Inferno {
    Vector3 ClosestPointOnLine(const Vector3& a, const Vector3& b, const Vector3& p);
    // Returns true if a point lies within a triangle
    bool TriangleContainsPoint(const Vector3& p0, const Vector3& p1, const Vector3& p2, Vector3 point);

    // Returns the closest point on a triangle to a point
    Vector3 ClosestPointOnTriangle(const Vector3& p0, const Vector3& p1, const Vector3& p2, Vector3 point);

    // Returns the nearest distance to the face edge and a point. Skips the internal split.
    float FaceEdgeDistance(const Segment& seg, SideID side, const Face& face, const Vector3& point);

    // Wraps a UV value to 0-1
    void WrapUV(Vector2& uv);

    // Returns the UVs on a face closest to a point in world coordinates
    Vector2 IntersectFaceUVs(const Vector3& point, const Face& face, int tri);
    void FixOverlayRotation(uint& x, uint& y, int width, int height, OverlayRotation rotation);

    // Returns true if the point was transparent
    bool WallPointIsTransparent(const Vector3& pnt, const Face& face, int tri);
}
