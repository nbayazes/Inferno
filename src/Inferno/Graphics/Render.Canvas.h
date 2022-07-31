#pragma once

#include "Types.h"
#include "DirectX.h"
#include "ShaderLibrary.h"
#include "DeviceResources.h"

namespace Inferno::Render {
    struct CanvasPayload {
        CanvasVertex V0, V1, V2, V3;
        Texture2D* Texture;
    };

    extern Ptr<DeviceResources> Adapter;
    extern Ptr<ShaderResources> Shaders;
    extern Ptr<EffectResources> Effects;

    // Draws a quad to the 2D canvas (UI Layer)
    class Canvas2D {
        DirectX::PrimitiveBatch<CanvasVertex> _batch;
        Dictionary<Texture2D*, List<CanvasPayload>> _commands;

    public:
        Canvas2D(ID3D12Device* device) : _batch(device) { }

        void Draw(CanvasPayload& payload) {
            if (!payload.Texture) return;
            _commands[payload.Texture].push_back(payload);
        }

        void Render(ID3D12GraphicsCommandList* cmdList, const Vector2& outputSize) {
            // draw batched text
            auto orthoProj = Matrix::CreateOrthographicOffCenter(0, outputSize.x, outputSize.y, 0.0, 0.0, -2.0f);

            Effects->UserInterface.Apply(cmdList);
            Shaders->UserInterface.SetWorldViewProjection(cmdList, orthoProj);

            for (auto& [texture, group] : _commands) {
                Shaders->UserInterface.SetDiffuse(cmdList, texture->GetSRV());
                _batch.Begin(cmdList);
                for (auto& c : group)
                    _batch.DrawQuad(c.V0, c.V1, c.V2, c.V3);

                _batch.End();
                group.clear();
            }
        }
    };

}