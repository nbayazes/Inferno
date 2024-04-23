#pragma once
#include "Level.h"
#include "Lighting.h"

namespace Inferno::Graphics {
    struct SegmentLight {
        struct SideLighting {
            List<LightData> Lights;
            // A side can have multiple dynamic lights, but they all share the same color and radius
            Color Color, AnimatedColor;
            float Radius, AnimatedRadius;
            Tag Tag;
        };

        Array<SideLighting, 6> Sides;
        List<LightData> Lights; // Lights located inside the segment
    };

    // Gathers level geometry lights in each segment
    List<SegmentLight> GatherSegmentLights(Level& level);
}

namespace Inferno::Render {
    void DrawLevel(GraphicsContext& ctx, Level& level);
    void RebuildLevelResources(Level& level);
    int GetTransparentQueueSize();
    span<RoomID> GetVisibleRooms();
}
