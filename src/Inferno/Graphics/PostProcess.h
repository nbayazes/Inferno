#pragma once

#include "Buffers.h"
#include "Compiler.h"
#include "Camera.h"
#include "ComputeShader.h"

namespace Inferno::Render {
    extern Inferno::Camera Camera;
}

namespace Inferno::PostFx {
    class ScanlineCS : public ComputeShader {
        enum RootSig { B0_Constants, U0_Result, T0_Source };

    public:
        ScanlineCS() : ComputeShader(8, 8) {}

        void Execute(ID3D12GraphicsCommandList* commandList, PixelBuffer& source, PixelBuffer& dest) {
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
    };

    class LinearizeDepthCS : public ComputeShader {
        enum RootSig { B0_Constants, U0_Result, T0_Source };

    public:
        LinearizeDepthCS() : ComputeShader(16, 16) {}

        void Execute(ID3D12GraphicsCommandList* commandList, DepthBuffer& source, PixelBuffer& dest) {
            source.Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            dest.Transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            const float nearClip = Render::Camera.NearClip;
            const float farClip = Render::Camera.FarClip;
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
    };

    class BloomExtractDownsampleCS : public ComputeShader {
        enum RootSig { B0_Constants, U0_Bloom, U1_Luma, T0_Source, T1_Emissive };

    public:
        BloomExtractDownsampleCS() : ComputeShader(8, 8) { }

        float BloomThreshold = 1.35f; // how high value needs to be to bloom. Setting to 0 causes exposure to have no effect.
        float Exposure = 1.4f;        // exposure adjustment on source image for bloom sampling
        const float InitialMinLog = -12.0f;
        const float InitialMaxLog = 4.0f;

        void Execute(ID3D12GraphicsCommandList* commandList, PixelBuffer& source, PixelBuffer& destBloom, PixelBuffer& destLuma) {
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
    };

    class DownsampleBloomCS : public ComputeShader {
        enum RootSig { B0_Constants, U0_4Results, T0_Bloom };

    public:
        DownsampleBloomCS() : ComputeShader(8, 8) { }

        void Execute(ID3D12GraphicsCommandList* commandList, PixelBuffer& source, PixelBuffer& dest) {
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
    };

    class BlurCS : public ComputeShader {
        enum RootSig { U0_Result, T0_Source };

    public:
        BlurCS() : ComputeShader(8, 8) { }

        void Execute(ID3D12GraphicsCommandList* commandList, PixelBuffer& source, PixelBuffer& dest) {
            source.Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            dest.Transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            commandList->SetComputeRootSignature(_rootSignature.Get());
            commandList->SetComputeRootDescriptorTable(U0_Result, dest.GetUAV());
            commandList->SetComputeRootDescriptorTable(T0_Source, source.GetSRV());
            commandList->SetPipelineState(_pso.Get());

            Dispatch2D(commandList, source);
        }
    };

    class UpsampleAndBlurCS : public ComputeShader {
        enum RootSig { B0_Constants, U0_Result, T0_HigherRes, T1_LowerRes };

    public:
        UpsampleAndBlurCS() : ComputeShader(8, 8) { }

        float UpsampleBlendFactor = 0.25f; // How much to blend between low and high res

        void Execute(ID3D12GraphicsCommandList* commandList, PixelBuffer& highResSrc, PixelBuffer& lowerResSrc, PixelBuffer& dest) {
            highResSrc.Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            lowerResSrc.Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            dest.Transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            DirectX::XMFLOAT3 constants = { 1.0f / highResSrc.GetWidth(), 1.0f / highResSrc.GetHeight(), UpsampleBlendFactor };

            commandList->SetComputeRootSignature(_rootSignature.Get());
            commandList->SetComputeRoot32BitConstants(B0_Constants, sizeof(constants) / 4, &constants, 0);
            commandList->SetComputeRootDescriptorTable(U0_Result, dest.GetUAV());
            commandList->SetComputeRootDescriptorTable(T0_HigherRes, highResSrc.GetSRV());
            commandList->SetComputeRootDescriptorTable(T1_LowerRes, lowerResSrc.GetSRV());
            commandList->SetPipelineState(_pso.Get());

            Dispatch2D(commandList, dest);
        }
    };

    class ToneMapCS : public ComputeShader {
        enum RootSig { B0_Constants, U0_Color, U1_Luma, T0_Bloom, T1_LUT };

    public:
        ToneMapCS() : ComputeShader(8, 8) {}

        float Exposure = 1.0f; // final scene exposure
        float BloomStrength = 0.5f;

        void Execute(ID3D12GraphicsCommandList* commandList, PixelBuffer& tonyMcMapface, PixelBuffer& bloom, PixelBuffer& colorDest, PixelBuffer& lumaDest) const {
            bloom.Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            tonyMcMapface.Transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            colorDest.Transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            lumaDest.Transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            struct ToneMapConstants {
                DirectX::XMFLOAT2 RcpBufferDim;
                float BloomStrength;
                float Exposure;
                HlslBool NewLightMode;
            };

            ToneMapConstants constants = {
                { 1.0f / (float)colorDest.GetWidth(), 1.0f / (float)colorDest.GetHeight() },
                BloomStrength, Exposure,
                (HlslBool)Settings::Graphics.NewLightMode
            };

            commandList->SetComputeRootSignature(_rootSignature.Get());
            commandList->SetComputeRoot32BitConstants(B0_Constants, sizeof(constants) / 4, &constants, 0);
            commandList->SetComputeRootDescriptorTable(U0_Color, colorDest.GetUAV());
            commandList->SetComputeRootDescriptorTable(U1_Luma, lumaDest.GetUAV());
            commandList->SetComputeRootDescriptorTable(T0_Bloom, bloom.GetSRV());
            commandList->SetComputeRootDescriptorTable(T1_LUT, tonyMcMapface.GetSRV());
            commandList->SetPipelineState(_pso.Get());

            Dispatch2D(commandList, colorDest);
        }
    };


    struct BloomBuffers {
        ColorBuffer Downsample[4]; // 8x8, 4x4, 2x2 and 1x1
        ColorBuffer Upsample[4];   // 8x8, 4x4, 2x2 and 1x1
        ColorBuffer OutputLuma, DownsampleBlur, DownsampleLuma, Blur;

        BloomBuffers() = default;
        BloomBuffers(const BloomBuffers&) = delete;
        BloomBuffers& operator=(const BloomBuffers&) = delete;

        void Create(UINT width, UINT height, DXGI_FORMAT format = DXGI_FORMAT_R11G11B10_FLOAT) {
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
    };

    class Bloom {
    public:
        // ExtractLumaCS ExtractLuma;
        BloomBuffers Buffers;
        BloomExtractDownsampleCS BloomExtractDownsample;
        DownsampleBloomCS DownsampleBloom;
        UpsampleAndBlurCS Upsample;
        ToneMapCS ToneMap;
        BlurCS Blur;
        Texture3D TonyMcMapFace;

        void Create(UINT width, UINT height, UINT scale = 3) {
            Buffers.Create(width / scale, height / scale);
        }

        void LoadResources(DirectX::ResourceUploadBatch& batch) {
            TonyMcMapFace.LoadDDS(batch, "tony_mc_mapface.dds");
            TonyMcMapFace.AddShaderResourceView();
        }

        void ReloadShaders() {
            BloomExtractDownsample.Load(L"shaders/BloomExtractDownsampleCS.hlsl");
            DownsampleBloom.Load(L"shaders/DownsampleBloomCS.hlsl");
            Upsample.Load(L"shaders/UpsampleAndBlurCS.hlsl");
            ToneMap.Load(L"shaders/ToneMapCS.hlsl");
            Blur.Load(L"shaders/BlurCS.hlsl");
        }

        // Updates source color buffer but also uses it as an input
        void Apply(ID3D12GraphicsCommandList* commandList, PixelBuffer& source) {
            //g_Descriptors->SetDescriptorHeaps(commandList);
            PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"Bloom");
            BloomExtractDownsample.Execute(commandList, source, Buffers.DownsampleBlur, Buffers.DownsampleLuma);
            DownsampleBloom.Execute(commandList, Buffers.DownsampleBlur, Buffers.Downsample[0]);
            Blur.Execute(commandList, Buffers.Downsample[3], Buffers.Blur);
            Upsample.Execute(commandList, Buffers.Downsample[2], Buffers.Blur, Buffers.Upsample[3]);
            Upsample.Execute(commandList, Buffers.Downsample[1], Buffers.Upsample[3], Buffers.Upsample[2]);
            Upsample.Execute(commandList, Buffers.Downsample[0], Buffers.Upsample[2], Buffers.Upsample[1]);
            Upsample.Execute(commandList, Buffers.DownsampleBlur, Buffers.Upsample[1], Buffers.Upsample[0]);
            ToneMap.Execute(commandList, TonyMcMapFace, Buffers.Upsample[0], source, Buffers.OutputLuma);
            PIXEndEvent(commandList);
        }
    };
}
