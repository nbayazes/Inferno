#pragma once
#include "Render.h"

namespace Inferno::Render {
    void StaticModelDepthPrepass(GraphicsContext& ctx, ModelID modelId, const Matrix& transform);
    void DrawStaticModel(GraphicsContext& ctx, ModelID modelId, RenderPass pass, const Color& ambient, const UploadBuffer<FrameConstants>& frameConstants, const Matrix& transform);
    void DrawModel(GraphicsContext& ctx, const Object& object, ModelID modelId, RenderPass pass, const UploadBuffer<FrameConstants>& frameConstants);
    void DrawAutomapModel(GraphicsContext& ctx, const Object& object, ModelID modelId, const Color& color, const UploadBuffer<FrameConstants>& frameConstants);
    void DrawFoggedObject(GraphicsContext& ctx, const Object& object, RenderPass pass);
    void DrawObject(GraphicsContext& ctx, const Object& object, RenderPass pass);
    void OutrageModelDepthPrepass(GraphicsContext& ctx, const Object& object);
    void SpriteDepthPrepass(GraphicsContext& ctx, const Object& object, const Vector3* up);
    void ModelDepthPrepass(GraphicsContext& ctx, const Object& object, ModelID modelId);
}
