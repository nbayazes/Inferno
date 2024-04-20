#pragma once
#include "Render.h"

namespace Inferno::Render {
    void DrawModel(GraphicsContext& ctx, const Object& object, ModelID modelId, RenderPass pass, const UploadBuffer<FrameConstants>& frameConstants);
    void DrawObject(GraphicsContext& ctx, const Object& object, RenderPass pass);
    void OutrageModelDepthPrepass(GraphicsContext& ctx, const Object& object);
    void SpriteDepthPrepass(GraphicsContext& ctx, const Object& object, const Vector3* up);
    void ModelDepthPrepass(GraphicsContext& ctx, const Object& object, ModelID modelId);
}
