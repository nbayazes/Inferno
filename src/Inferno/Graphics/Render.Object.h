#pragma once
#include "Render.h"

namespace Inferno::Render {
    void DrawObject(Graphics::GraphicsContext& ctx, const Object& object, RenderPass pass);
    void OutrageModelDepthPrepass(Graphics::GraphicsContext& ctx, const Object& object);
    void SpriteDepthPrepass(ID3D12GraphicsCommandList* cmdList, const Object& object, const Vector3* up);
    void ModelDepthPrepass(ID3D12GraphicsCommandList* cmdList, const Object& object, ModelID modelId);
}
