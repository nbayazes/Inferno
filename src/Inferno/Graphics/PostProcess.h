#pragma once

#include "CameraContext.h"
#include "ComputeShader.h"

namespace Inferno::PostFx {
    class ScanlineCS : public ComputeShader {
        enum RootSig { B0_Constants, U0_Result, T0_Source };

    public:
        ScanlineCS() : ComputeShader(8, 8) {}

        void Execute(ID3D12GraphicsCommandList* commandList, PixelBuffer& source, PixelBuffer& dest) const;
    };

    class LinearizeDepthCS : public ComputeShader {
        enum RootSig { B0_Constants, U0_Result, T0_Source };

    public:
        LinearizeDepthCS() : ComputeShader(16, 16) {}

        void Execute(const GraphicsContext& ctx, DepthBuffer& source, PixelBuffer& dest) const;
    };

    class BloomExtractDownsampleCS : public ComputeShader {
        enum RootSig { B0_Constants, U0_Bloom, U1_Luma, T0_Source, T1_Emissive };

    public:
        BloomExtractDownsampleCS() : ComputeShader(8, 8) { }

        float BloomThreshold = 1.25f; // how high value needs to be to bloom. Setting to 0 causes exposure to have no effect.
        float Exposure = 1.0f;        // exposure adjustment on source image for bloom sampling
        const float InitialMinLog = -12.0f;
        const float InitialMaxLog = 4.0f;

        void Execute(ID3D12GraphicsCommandList* commandList, PixelBuffer& source, PixelBuffer& destBloom, PixelBuffer& destLuma) const;
    };

    class DownsampleBloomCS : public ComputeShader {
        enum RootSig { B0_Constants, U0_4Results, T0_Bloom };

    public:
        DownsampleBloomCS() : ComputeShader(8, 8) { }

        void Execute(ID3D12GraphicsCommandList* commandList, PixelBuffer& source, PixelBuffer& dest) const;
    };

    class BlurCS : public ComputeShader {
        enum RootSig { U0_Result, T0_Source };

    public:
        BlurCS() : ComputeShader(8, 8) { }

        void Execute(ID3D12GraphicsCommandList* commandList, PixelBuffer& source, PixelBuffer& dest) const;
    };

    class UpsampleAndBlurCS : public ComputeShader {
        enum RootSig { B0_Constants, U0_Result, T0_HigherRes, T1_LowerRes };

    public:
        UpsampleAndBlurCS() : ComputeShader(8, 8) { }

        float UpsampleBlendFactor = 0.325f; // How much to blend between low and high res

        void Execute(ID3D12GraphicsCommandList* commandList, PixelBuffer& highResSrc, PixelBuffer& lowerResSrc, PixelBuffer& dest) const;
    };

    // Unpacks a R32_UINT buffer to a color buffer
    class UnpackPostBuffer : public ComputeShader {
        enum { T0_SOURCE, U0_DEST };
    public:
        UnpackPostBuffer() : ComputeShader(8, 8) {}

        void Execute(ID3D12GraphicsCommandList* commandList, PixelBuffer& source, PixelBuffer& dest) const;
    };

    class ToneMapCS : public ComputeShader {
        enum RootSig { B0_Constants, U0_Color, U1_Luma, T0_Bloom, T1_LUT, T2_DIRT, T3_SRC_COLOR };

    public:
        ToneMapCS() : ComputeShader(8, 8) {}

        float Exposure = 1.0f; // final scene exposure
        float BloomStrength = 0.35f;

        void Execute(ID3D12GraphicsCommandList* commandList,
                     PixelBuffer& tonyMcMapface,
                     PixelBuffer& bloom, 
                     PixelBuffer& colorDest,
                     PixelBuffer& lumaDest,
                     PixelBuffer* source,
                     Texture2D& dirt) const;
    };


    struct BloomBuffers {
        ColorBuffer Downsample[4]; // 8x8, 4x4, 2x2 and 1x1
        ColorBuffer Upsample[4];   // 8x8, 4x4, 2x2 and 1x1
        ColorBuffer OutputLuma, DownsampleBlur, DownsampleLuma, Blur;

        BloomBuffers() = default;
        BloomBuffers(const BloomBuffers&) = delete;
        BloomBuffers& operator=(const BloomBuffers&) = delete;

        void Create(UINT width, UINT height, DXGI_FORMAT format = DXGI_FORMAT_R11G11B10_FLOAT);
    };

    class ToneMapping {
        ColorBuffer _post;

    public:
        // ExtractLumaCS ExtractLuma;
        BloomBuffers Buffers;
        BloomExtractDownsampleCS BloomExtractDownsample;
        DownsampleBloomCS DownsampleBloom;
        UpsampleAndBlurCS Upsample;
        ToneMapCS ToneMap;
        BlurCS Blur;
        Texture3D TonyMcMapFace;
        Texture2D Dirt;
        UnpackPostBuffer UnpackPost;

        void Create(UINT width, UINT height, UINT scale = 3);

        void LoadResources(DirectX::ResourceUploadBatch& batch);

        void ReloadShaders();

        // Updates source color buffer but also uses it as an input
        void Apply(ID3D12GraphicsCommandList* commandList, PixelBuffer& source);
    };
}
