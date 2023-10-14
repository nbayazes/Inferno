#include "pch.h"
#include "ShaderLibrary.h"
#include "Compiler.h"

namespace Inferno {
    using namespace DirectX;
    using namespace DirectX::SimpleMath;

    List<IShader> CompiledShaders;

    // Recompiles a shader
    void CompileShader(IShader* shader) noexcept {
        try {
            auto vertexShader = LoadVertexShader(shader->Info.File, shader->RootSignature, shader->Info.VSEntryPoint);
            auto pixelShader = LoadPixelShader(shader->Info.File, shader->Info.PSEntryPoint);

            // Only assign shaders if they compiled successfully
            if (vertexShader && pixelShader) {
                shader->VertexShader = vertexShader;
                shader->PixelShader = pixelShader;
            }
        }
        catch (std::exception& e) {
            SPDLOG_ERROR(e.what());    
        }
    }

    // Orgb = srgb * Srgb + drgb * Drgb
    constexpr D3D12_RENDER_TARGET_BLEND_DESC BLEND_DESC_MULTIPLY_RT = {
        .BlendEnable = true,
        .LogicOpEnable = false,
        .SrcBlend = D3D12_BLEND_DEST_COLOR, // O = S * D
        .DestBlend = D3D12_BLEND_ZERO, // Zero out additive term
        .BlendOp = D3D12_BLEND_OP_ADD,
        .SrcBlendAlpha = D3D12_BLEND_ONE,
        .DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA,
        .BlendOpAlpha = D3D12_BLEND_OP_ADD,
        .LogicOp = D3D12_LOGIC_OP_NOOP,
        .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL
    };

    constexpr D3D12_BLEND_DESC BLEND_DESC_MULTIPLY = {
        .RenderTarget = { BLEND_DESC_MULTIPLY_RT }
    };

    constexpr D3D12_DEPTH_STENCIL_DESC DEPTH_EQUAL =
    {
        TRUE, // DepthEnable
        D3D12_DEPTH_WRITE_MASK_ZERO,
        D3D12_COMPARISON_FUNC_EQUAL, // DepthFunc
        FALSE, // StencilEnable
        D3D12_DEFAULT_STENCIL_READ_MASK,
        D3D12_DEFAULT_STENCIL_WRITE_MASK,
        {
            D3D12_STENCIL_OP_KEEP, // StencilFailOp
            D3D12_STENCIL_OP_KEEP, // StencilDepthFailOp
            D3D12_STENCIL_OP_KEEP, // StencilPassOp
            D3D12_COMPARISON_FUNC_ALWAYS // StencilFunc
        }, // FrontFace
        {
            D3D12_STENCIL_OP_KEEP, // StencilFailOp
            D3D12_STENCIL_OP_KEEP, // StencilDepthFailOp
            D3D12_STENCIL_OP_KEEP, // StencilPassOp
            D3D12_COMPARISON_FUNC_ALWAYS // StencilFunc
        } // BackFace
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC BuildPipelineStateDesc(EffectSettings effect, IShader* shader, uint msaaSamples, uint renderTargets) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        if (!shader->RootSignature || !shader->VertexShader || !shader->PixelShader)
            throw Exception("Shader is not valid");

        psoDesc.pRootSignature = shader->RootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(shader->VertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(shader->PixelShader.Get());
        psoDesc.InputLayout = shader->InputLayout;

        psoDesc.RasterizerState = [&effect] {
            switch (effect.Culling) {
                case CullMode::None: return CommonStates::CullNone;
                case CullMode::Clockwise: return CommonStates::CullClockwise;
                case CullMode::CounterClockwise: default: return CommonStates::CullCounterClockwise;
            }
        }();

        psoDesc.BlendState = [&effect] {
            switch (effect.Blend) {
                case BlendMode::Alpha: return CommonStates::AlphaBlend;
                case BlendMode::StraightAlpha: return CommonStates::NonPremultiplied;
                case BlendMode::Additive: return CommonStates::Additive;
                case BlendMode::Opaque: default: return CommonStates::Opaque;
                case BlendMode::Multiply: return BLEND_DESC_MULTIPLY;
            }
        }();

        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.DepthStencilState = [&effect] {
            switch (effect.Depth) {
                case DepthMode::None: return CommonStates::DepthNone;
                case DepthMode::ReadWrite: return CommonStates::DepthDefault;
                case DepthMode::Read: default: return CommonStates::DepthRead;
                case DepthMode::ReadEqual: return DEPTH_EQUAL;
            }
        }();
        
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = effect.TopologyType;
        psoDesc.NumRenderTargets = renderTargets;

        if (effect.Depth == DepthMode::ReadDecalBiased) {
            // Biases for decals
            psoDesc.RasterizerState.DepthBias = -10'000;
            psoDesc.RasterizerState.SlopeScaledDepthBias = -4.0f;
            psoDesc.RasterizerState.DepthBiasClamp = -100'000;
        }

        if (effect.Depth == DepthMode::ReadSpriteBiased) {
            // Biases for sprites
            psoDesc.RasterizerState.DepthBias = -20'000;
            psoDesc.RasterizerState.SlopeScaledDepthBias = -4.0f;
            psoDesc.RasterizerState.DepthBiasClamp = -200'000;
        }

        for (uint i = 0; i < renderTargets; i++)
            psoDesc.RTVFormats[i] = shader->Format;

        psoDesc.SampleDesc.Count = effect.EnableMultisample ? msaaSamples : 1;
        return psoDesc;
    }
}

