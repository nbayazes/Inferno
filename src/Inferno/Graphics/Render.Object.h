#pragma once
#include "Render.h"

namespace Inferno::Render {
    void DrawObject(Graphics::GraphicsContext& ctx, const Object& object, RenderPass pass);
    void OutrageModelDepthPrepass(Graphics::GraphicsContext& ctx, const Object& object);
}
