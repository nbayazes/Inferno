#pragma once
#include "CommandContext.h"
#include "Level.h"

namespace Inferno::Graphics {
    List<LightData> GatherLightSources(Level& level, float multiplier = 1, float defaultRadius = 20);
}

namespace Inferno::Render {

    void DrawLevel(Graphics::GraphicsContext& ctx, Level& level);
    void ResetLightCache();
}
