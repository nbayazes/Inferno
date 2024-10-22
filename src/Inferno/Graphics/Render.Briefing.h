#pragma once
#include "CameraContext.h"
#include "Game.Briefing.h"
#include "GpuResources.h"

namespace Inferno::Render {
    void DrawBriefing(GraphicsContext& ctx, RenderTarget& target, const BriefingState& briefing);
}
