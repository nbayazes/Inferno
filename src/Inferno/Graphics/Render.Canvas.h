#pragma once

#include "Types.h"
#include "DirectX.h"
#include "ShaderLibrary.h"
#include "DeviceResources.h"
#include "CommandContext.h"

namespace Inferno::Render {
    struct CanvasPayload {
        CanvasVertex V0, V1, V2, V3;
        D3D12_GPU_DESCRIPTOR_HANDLE Texture{};
    };

    extern Ptr<DeviceResources> Adapter;
    extern Ptr<ShaderResources> Shaders;
    extern Ptr<EffectResources> Effects;

    // Draws a quad to the 2D canvas (UI Layer)
    class Canvas2D {
        DirectX::PrimitiveBatch<CanvasVertex> _batch;
        Dictionary<uint64, List<CanvasPayload>> _commands;

    public:
        Canvas2D(ID3D12Device* device) : _batch(device) { }

        void Draw(CanvasPayload& payload) {
            if (!payload.Texture.ptr) return;
            _commands[payload.Texture.ptr].push_back(payload);
        }

        void DrawBitmap(TexID id, const Vector2& pos, const Vector2& size) {
            //auto material = &Materials->Get(id);
            auto handle = Materials->Get(id).Handles[Material2D::Diffuse];
            if (!handle.ptr) 
                handle = Materials->White.Handles[Material2D::Diffuse];

            CanvasPayload payload{};
            auto color = Color(1, 1, 1).BGRA();
            payload.V0 = { Vector2{ pos.x, pos.y + size.y }, { 0, 1 }, color }; // bottom left
            payload.V1 = { Vector2{ pos.x + size.x, pos.y + size.y }, { 1, 1 }, color }; // bottom right
            payload.V2 = { Vector2{ pos.x + size.x, pos.y }, { 1, 0 }, color }; // top right
            payload.V3 = { Vector2{ pos.x, pos.y }, { 0, 0 }, color }; // top left
            payload.Texture = handle;
            Draw(payload);
        }

        void Render(Graphics::GraphicsContext& ctx, const Vector2& outputSize) {
            // draw batched text
            auto orthoProj = Matrix::CreateOrthographicOffCenter(0, outputSize.x, outputSize.y, 0.0, 0.0, -2.0f);

            auto cmdList = ctx.CommandList();
            auto& effect = Effects->UserInterface;
            ctx.ApplyEffect(effect);
            effect.Shader->SetWorldViewProjection(cmdList, orthoProj);

            for (auto& [texture, group] : _commands) {
                effect.Shader->SetDiffuse(cmdList, group.front().Texture);
                _batch.Begin(cmdList);
                for (auto& c : group)
                    _batch.DrawQuad(c.V0, c.V1, c.V2, c.V3);

                _batch.End();
                group.clear();
            }

            _commands.clear();
        }
    };

}