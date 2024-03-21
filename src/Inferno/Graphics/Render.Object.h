#pragma once
#include "Render.h"

namespace Inferno::Render {
    void DrawModel(Graphics::GraphicsContext& ctx, const Object& object, ModelID modelId, RenderPass pass, const UploadBuffer<FrameConstants>& frameConstants);
    void DrawObject(Graphics::GraphicsContext& ctx, const Object& object, RenderPass pass);
    void OutrageModelDepthPrepass(Graphics::GraphicsContext& ctx, const Object& object);
    void SpriteDepthPrepass(ID3D12GraphicsCommandList* cmdList, const Object& object, const Vector3* up);
    void ModelDepthPrepass(Graphics::GraphicsContext& ctx, const Object& object, ModelID modelId);
}
