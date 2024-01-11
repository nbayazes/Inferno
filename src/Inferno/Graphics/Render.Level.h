#pragma once
#include "CommandContext.h"
#include "Level.h"

namespace Inferno::Graphics {
    // Gathers static lights across the level
    List<List<LightData>> GatherLightSources(Level& level, float multiplier = 1, float defaultRadius = 20);
}

namespace Inferno::Render {
    void DrawLevel(Graphics::GraphicsContext& ctx, Level& level);
    void RebuildLevelResources(Level& level);
    int GetTransparentQueueSize();
    span<RoomID> GetVisibleRooms();
}
