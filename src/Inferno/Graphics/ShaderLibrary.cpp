#include "pch.h"
#include "ShaderLibrary.h"
#include "Compiler.h"

#pragma comment(lib, "d3dcompiler") // Automatically link with d3dcompiler.lib as we are using D3DCompile() below.

//#if defined(_DEBUG)
//// Enable better shader debugging with the graphics debugging tools.
//constexpr UINT COMPILE_FLAGS = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
//#else
//constexpr UINT COMPILE_FLAGS = 0;
//#endif

namespace Inferno {
    using namespace DirectX;
    using namespace DirectX::SimpleMath;

    List<IShader> CompiledShaders;

    // Recompiles a shader
    void CompileShader(IShader* shader) noexcept {
        auto vertexShader = LoadVertexShader(shader->Info.File, shader->RootSignature, shader->Info.VSEntryPoint);
        auto pixelShader = LoadPixelShader(shader->Info.File, shader->Info.PSEntryPoint);
        // Only assign shaders if they compiled successfully
        if (vertexShader && pixelShader) {
            shader->VertexShader = vertexShader;
            shader->PixelShader = pixelShader;
        }
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC BuildPipelineStateDesc(EffectSettings effect, IShader* shader, DXGI_FORMAT format, uint msaaSamples, uint renderTargets) {
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
            // todo: Multiply blend mode?
            switch (effect.Blend) {
                case BlendMode::Alpha: return CommonStates::AlphaBlend;
                case BlendMode::StraightAlpha: return CommonStates::NonPremultiplied;
                case BlendMode::Additive: return CommonStates::Additive;
                case BlendMode::Opaque: default: return CommonStates::Opaque;
            }
        }();

        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.DepthStencilState = [&effect] {
            switch (effect.Depth) {
                case DepthMode::None: return CommonStates::DepthNone;
                case DepthMode::Default: return CommonStates::DepthDefault;
                case DepthMode::Read: default: return CommonStates::DepthRead;
            };
        }();
        
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = effect.TopologyType;
        psoDesc.NumRenderTargets = renderTargets;
        for (uint i = 0; i < renderTargets; i++)
            psoDesc.RTVFormats[i] = format;

        psoDesc.SampleDesc.Count = effect.EnableMultisample ? msaaSamples : 1;
        return psoDesc;
    }
}

