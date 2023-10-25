#include "pch.h"
#include "Room.h"

#include "Level.h"

DirectX::BoundingBox Inferno::Room::GetBounds(Level& level) const {
    Vector3 min(FLT_MAX, FLT_MAX, FLT_MAX), max(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    for (auto& segid : Segments) {
        if (auto seg = level.TryGetSegment(segid)) {
            for (auto& v : seg->GetVertices(level)) {
                max = Vector3::Max(max, *v);
                min = Vector3::Min(min, *v);
            }
        }
    }

    auto center = (max + min) / 2;
    auto extents = (max - min) / 2;
    return { center, extents };
}
