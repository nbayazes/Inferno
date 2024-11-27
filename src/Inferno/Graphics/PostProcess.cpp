#include <pch.h>

#include "PostProcess.h"
#include "FileSystem.h"
#include "Game.h"
#include "WindowsDialogs.h"
#include "Render.h"

namespace Inferno::PostFx {
    void ScanlineCS::Execute(ID3D12GraphicsCommandList* commandList, PixelBuffer& source, PixelBuffer& dest) const {
        source.Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        dest.Transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        float constants[2] = {
            1.0f / dest.GetWidth(), 1.0f / dest.GetHeight()
        };

        commandList->SetComputeRootSignature(_rootSignature.Get());
        commandList->SetComputeRoot32BitConstants(B0_Constants, sizeof(constants) / 4, &constants, 0);
        commandList->SetComputeRootDescriptorTable(U0_Result, dest.GetUAV());
        commandList->SetComputeRootDescriptorTable(T0_Source, source.GetSRV());
        commandList->SetPipelineState(_pso.Get());
        Dispatch2D(commandList, dest);
    }

    void LinearizeDepthCS::Execute(const GraphicsContext& ctx, DepthBuffer& source, PixelBuffer& dest) const {
        const auto& commandList = ctx.GetCommandList();
        source.Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        dest.Transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        const float nearClip = ctx.Camera.GetNearClip();
        const float farClip = ctx.Camera.GetFarClip();
        float constants[2] = { nearClip, farClip };
        //float constants[1] = { (farClip - nearClip) / nearClip };

        commandList->SetComputeRootSignature(_rootSignature.Get());
        commandList->SetComputeRoot32BitConstants(B0_Constants, sizeof(constants) / 4, &constants, 0);
        commandList->SetComputeRootDescriptorTable(U0_Result, dest.GetUAV());
        auto srcSrv = source.GetSRV();
        //if (!srcSrv.ptr) {
        //    source.AddShaderResourceView();
        //    srcSrv = source.GetSRV();
        //}

        assert(srcSrv.ptr);
        commandList->SetComputeRootDescriptorTable(T0_Source, source.GetSRV());
        commandList->SetPipelineState(_pso.Get());
        Dispatch2D(commandList, dest);
    }

    void BloomExtractDownsampleCS::Execute(ID3D12GraphicsCommandList* commandList, PixelBuffer& source, PixelBuffer& destBloom, PixelBuffer& destLuma) const {
        source.Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        destBloom.Transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        destLuma.Transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        float constants[6] = {
            1.0f / destBloom.GetWidth(), 1.0f / destBloom.GetHeight(), BloomThreshold,
            1.0f / Exposure, InitialMinLog, 1.0f / (InitialMaxLog - InitialMinLog)
        };

        commandList->SetComputeRootSignature(_rootSignature.Get());
        commandList->SetComputeRoot32BitConstants(B0_Constants, sizeof(constants) / 4, &constants, 0);
        // this is why a dynamic ring buffer for handles is necessary. otherwise each resource must be bound individually.
        commandList->SetComputeRootDescriptorTable(U0_Bloom, destBloom.GetUAV());
        commandList->SetComputeRootDescriptorTable(U1_Luma, destLuma.GetUAV());
        commandList->SetComputeRootDescriptorTable(T0_Source, source.GetSRV());
        commandList->SetPipelineState(_pso.Get());
        Dispatch2D(commandList, destBloom);
    }

    void DownsampleBloomCS::Execute(ID3D12GraphicsCommandList* commandList, PixelBuffer& source, PixelBuffer& dest) const {
        source.Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        dest.Transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        DirectX::XMFLOAT2 constants = { 1.0f / source.GetWidth(), 1.0f / source.GetHeight() };

        commandList->SetComputeRootSignature(_rootSignature.Get());
        commandList->SetComputeRoot32BitConstants(B0_Constants, sizeof(constants) / 4, &constants, 0);
        commandList->SetComputeRootDescriptorTable(U0_4Results, dest.GetUAV()); // binding 0 will bind the next 3 as well
        commandList->SetComputeRootDescriptorTable(T0_Bloom, source.GetSRV());
        commandList->SetPipelineState(_pso.Get());

        Dispatch2D(commandList, source);
    }

    void DownsampleCS::Execute(ID3D12GraphicsCommandList* commandList, PixelBuffer& source, PixelBuffer& dest) const {
        source.Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        dest.Transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        auto xratio = (float)source.GetWidth() / dest.GetWidth();
        auto yratio = (float)source.GetHeight() / dest.GetHeight();

        DirectX::XMFLOAT2 constants = {
            1.0f / (float)source.GetWidth() * sqrt(xratio),
            1.0f / (float)source.GetHeight() * sqrt(yratio)
        };

        commandList->SetComputeRootSignature(_rootSignature.Get());
        commandList->SetComputeRoot32BitConstants(B0_Constants, sizeof(constants) / 4, &constants, 0);
        commandList->SetComputeRootDescriptorTable(U0_Result, dest.GetUAV());
        commandList->SetComputeRootDescriptorTable(T0_Bloom, source.GetSRV());
        commandList->SetPipelineState(_pso.Get());

        Dispatch2D(commandList, source);
    }

    void BlurCS::Execute(ID3D12GraphicsCommandList* commandList, PixelBuffer& source, PixelBuffer& dest) const {
        source.Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        dest.Transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        commandList->SetComputeRootSignature(_rootSignature.Get());
        commandList->SetComputeRootDescriptorTable(U0_Result, dest.GetUAV());
        commandList->SetComputeRootDescriptorTable(T0_Source, source.GetSRV());
        commandList->SetPipelineState(_pso.Get());

        Dispatch2D(commandList, source);
    }

    void UpsampleAndBlurCS::Execute(ID3D12GraphicsCommandList* commandList, PixelBuffer& highResSrc, PixelBuffer& lowerResSrc, PixelBuffer& dest) const {
        highResSrc.Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        lowerResSrc.Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        dest.Transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        DirectX::XMFLOAT3 constants = {
            1.0f / highResSrc.GetWidth(),
            1.0f / highResSrc.GetHeight(),
            UpsampleBlendFactor
        };

        commandList->SetComputeRootSignature(_rootSignature.Get());
        commandList->SetComputeRoot32BitConstants(B0_Constants, sizeof(constants) / 4, &constants, 0);
        commandList->SetComputeRootDescriptorTable(U0_Result, dest.GetUAV());
        commandList->SetComputeRootDescriptorTable(T0_HigherRes, highResSrc.GetSRV());
        commandList->SetComputeRootDescriptorTable(T1_LowerRes, lowerResSrc.GetSRV());
        commandList->SetPipelineState(_pso.Get());

        Dispatch2D(commandList, dest);
    }

    void ToneMapCS::Execute(ID3D12GraphicsCommandList* commandList,
                            PixelBuffer& tonyMcMapface,
                            PixelBuffer& bloom,
                            PixelBuffer& colorDest,
                            PixelBuffer& lumaDest,
                            PixelBuffer* source,
                            Texture2D& dirt) const {
        // todo: disable bloom option
        bloom.Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tonyMcMapface.Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (dirt) dirt.Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        lumaDest.Transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        struct ToneMapConstants {
            DirectX::XMFLOAT2 RcpBufferDim;
            float BloomStrength;
            float Exposure;
            HlslBool NewLightMode;
            int32 ToneMapper;
            HlslBool EnableDirt;
            HlslBool EnableBloom;
            Color Tint;
        };

        ToneMapConstants constants = {
            { 1.0f / (float)colorDest.GetWidth(), 1.0f / (float)colorDest.GetHeight() },
            BloomStrength /** Settings::Graphics.RenderScale*/, // Lower resolution blurs more, so reduce the intensity
            Exposure,
            (HlslBool)Settings::Graphics.NewLightMode,
            Settings::Graphics.ToneMapper,
            (HlslBool)(dirt && (Game::GetState() == GameState::Game || Game::GetState() == GameState::PauseMenu)),
            (HlslBool)Settings::Graphics.EnableBloom,
            Game::ScreenTint.GetColor()
        };

        commandList->SetPipelineState(_pso.Get());
        commandList->SetComputeRootSignature(_rootSignature.Get());
        commandList->SetComputeRoot32BitConstants(B0_Constants, sizeof(constants) / 4, &constants, 0);
        commandList->SetComputeRootDescriptorTable(T0_Bloom, bloom.GetSRV());
        commandList->SetComputeRootDescriptorTable(T1_LUT, tonyMcMapface.GetSRV());
        if (dirt) commandList->SetComputeRootDescriptorTable(T2_DIRT, dirt.GetSRV());

        colorDest.Transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList->SetComputeRootDescriptorTable(U0_Color, colorDest.GetUAV());

        if (!Render::Adapter->TypedUAVLoadSupport_R11G11B10_FLOAT()) {
            // Without UAV loads, need to separate the read and write buffers
            source->Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            commandList->SetComputeRootDescriptorTable(T3_SRC_COLOR, source->GetSRV());
        }

        Dispatch2D(commandList, colorDest);
    }

    void BloomBuffers::Create(UINT width, UINT height, DXGI_FORMAT format) {
        Blur.Create(L"Blur Result", width / 16, height / 16, format);
        DownsampleBlur.Create(L"Bloom extract downsample", width, height, format);
        DownsampleLuma.Create(L"Downsample Luma", width, height, DXGI_FORMAT_R8_UINT);
        OutputLuma.Create(L"Output Luma", width, height, DXGI_FORMAT_R8_UINT);

        Downsample[0].Create(L"Bloom Downsample 8x8", width / 2, height / 2, format);
        Downsample[1].Create(L"Bloom Downsample 4x4", width / 4, height / 4, format);
        Downsample[2].Create(L"Bloom Downsample 2x2", width / 8, height / 8, format);
        Downsample[3].Create(L"Bloom Downsample 1x1", width / 16, height / 16, format);

        Upsample[0].Create(L"Bloom Upsample 8x8", width / 1, height / 1, format);
        Upsample[1].Create(L"Bloom Upsample 4x4", width / 2, height / 2, format);
        Upsample[2].Create(L"Bloom Upsample 2x2", width / 4, height / 4, format);
        Upsample[3].Create(L"Bloom Upsample 1x1", width / 8, height / 8, format);

        Blur.AddUnorderedAccessView();
        Blur.AddShaderResourceView();

        DownsampleBlur.AddUnorderedAccessView();
        DownsampleLuma.AddUnorderedAccessView();
        DownsampleBlur.AddShaderResourceView();
        OutputLuma.AddUnorderedAccessView();

        Downsample[0].AddUnorderedAccessView();
        Downsample[1].AddUnorderedAccessView();
        Downsample[2].AddUnorderedAccessView();
        Downsample[3].AddUnorderedAccessView();
        Downsample[0].AddShaderResourceView();
        Downsample[1].AddShaderResourceView();
        Downsample[2].AddShaderResourceView();
        Downsample[3].AddShaderResourceView();

        Upsample[0].AddUnorderedAccessView();
        Upsample[1].AddUnorderedAccessView();
        Upsample[2].AddUnorderedAccessView();
        Upsample[3].AddUnorderedAccessView();
        Upsample[0].AddShaderResourceView();
        Upsample[1].AddShaderResourceView();
        Upsample[2].AddShaderResourceView();
        Upsample[3].AddShaderResourceView();
    }

    void UnpackPostBuffer::Execute(ID3D12GraphicsCommandList* commandList, PixelBuffer& source, PixelBuffer& dest) const {
        PIXScopedEvent(commandList, PIX_COLOR_DEFAULT, "Unpack buffer");

        commandList->SetPipelineState(_pso.Get());
        commandList->SetComputeRootSignature(_rootSignature.Get());
        source.Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        dest.Transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList->SetComputeRootDescriptorTable(T0_SOURCE, source.GetSRV());
        commandList->SetComputeRootDescriptorTable(U0_DEST, dest.GetUAV());
        Dispatch2D(commandList, source);
    }

    void ToneMapping::Create(UINT width, UINT height, UINT scale) {
        Buffers.Create(width / scale, height / scale);

        if (!Render::Adapter->TypedUAVLoadSupport_R11G11B10_FLOAT()) {
            _post.Create(L"Post process buffer", width, height, DXGI_FORMAT_R32_UINT, 1);
            _post.AddUnorderedAccessView();
            _post.AddShaderResourceView();
        }
        else {
            _post.Release();
        }
    }

    void ToneMapping::LoadResources(DirectX::ResourceUploadBatch& batch) {
        if (auto path = FileSystem::TryFindFile("tony_mc_mapface.dds")) {
            TonyMcMapFace.LoadDDS(batch, *path);
            TonyMcMapFace.AddShaderResourceView();
        }
        else {
            ShowErrorMessage(L"Unable to find required file: tony_mc_mapface.dds");
        }

        if (auto path = FileSystem::TryFindFile("cockpit-dirt.dds")) {
            Dirt.LoadDDS(batch, *path);
            Dirt.AddShaderResourceView();
        }
    }

    void ToneMapping::ReloadShaders() {
        BloomExtractDownsample.Load(L"shaders/BloomExtractDownsampleCS.hlsl");
        DownsampleBloom.Load(L"shaders/DownsampleBloomCS.hlsl");
        Downsample.Load(L"shaders/DownsampleCS.hlsl");
        Upsample.Load(L"shaders/UpsampleAndBlurCS.hlsl");
        if (Render::Adapter->TypedUAVLoadSupport_R11G11B10_FLOAT())
            ToneMap.Load(L"shaders/ToneMapCS.hlsl");
        else
            ToneMap.Load(L"shaders/ToneMapCS-NoUAVL.hlsl");

        Blur.Load(L"shaders/BlurCS.hlsl");
        UnpackPost.Load("shaders/UnpackBufferCS.hlsl");
    }

    void ToneMapping::Apply(ID3D12GraphicsCommandList* commandList, PixelBuffer& source) {
        if (Settings::Graphics.EnableBloom) {
            PIXScopedEvent(commandList, PIX_COLOR_DEFAULT, "Bloom");
            BloomExtractDownsample.Execute(commandList, source, Buffers.DownsampleBlur, Buffers.DownsampleLuma);
            DownsampleBloom.Execute(commandList, Buffers.DownsampleBlur, Buffers.Downsample[0]);
            Blur.Execute(commandList, Buffers.Downsample[3], Buffers.Blur);
            Upsample.Execute(commandList, Buffers.Downsample[2], Buffers.Blur, Buffers.Upsample[3]);
            Upsample.Execute(commandList, Buffers.Downsample[1], Buffers.Upsample[3], Buffers.Upsample[2]);
            Upsample.Execute(commandList, Buffers.Downsample[0], Buffers.Upsample[2], Buffers.Upsample[1]);
            Upsample.Execute(commandList, Buffers.DownsampleBlur, Buffers.Upsample[1], Buffers.Upsample[0]);
        }

        {
            PIXScopedEvent(commandList, PIX_COLOR_DEFAULT, "Tone map");

            if (Render::Adapter->TypedUAVLoadSupport_R11G11B10_FLOAT()) {
                ToneMap.Execute(commandList, TonyMcMapFace, Buffers.Upsample[0], source, Buffers.OutputLuma, nullptr, Dirt);
            }
            else {
                ToneMap.Execute(commandList, TonyMcMapFace, Buffers.Upsample[0], _post, Buffers.OutputLuma, &source, Dirt);
                UnpackPost.Execute(commandList, _post, source);
            }
        }
    }
}
