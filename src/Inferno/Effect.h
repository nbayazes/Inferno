#pragma once
#include <d3d12.h>

namespace Inferno {
    enum class BlendMode { Opaque, Alpha, StraightAlpha, Additive, Multiply };
    enum class CullMode { None, CounterClockwise, Clockwise };
    enum class DepthMode { ReadWrite, Read, ReadDecalBiased, ReadSpriteBiased, None };

    struct EffectSettings {
        BlendMode Blend = BlendMode::Opaque;
        CullMode Culling = CullMode::CounterClockwise;
        DepthMode Depth = DepthMode::ReadWrite;
        D3D12_PRIMITIVE_TOPOLOGY_TYPE TopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        bool EnableMultisample = true;
    };

    template<class TShader>
    struct Effect {
        Effect(TShader* shader, const EffectSettings& settings = {})
            : Settings(settings), Shader(shader) {}

        EffectSettings Settings;
        TShader* Shader;
        ComPtr<ID3D12PipelineState> PipelineState;

        void Apply(ID3D12GraphicsCommandList* commandList) {
            commandList->SetPipelineState(PipelineState.Get());
            Shader->Apply(commandList);
        };
    };
}