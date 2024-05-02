#pragma once
#include <d3d12.h>

namespace Inferno {
    enum class BlendMode { Opaque, Alpha, StraightAlpha, Additive, Multiply };
    enum class CullMode { None, CounterClockwise, Clockwise, Wireframe };
    enum class DepthMode { ReadWrite, Read, ReadDecalBiased, ReadSpriteBiased, ReadEqual, None };
    enum class StencilMode { None, PortalRead, PortalReadNeq, PortalWrite };

    struct EffectSettings {
        BlendMode Blend = BlendMode::Opaque;
        CullMode Culling = CullMode::CounterClockwise;
        DepthMode Depth = DepthMode::ReadWrite;
        StencilMode Stencil = StencilMode::None;
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
    };
}