#pragma once

#include "DirectX.h"
#include "Effect.h"
#include "Lighting.h"
#include "Material2D.h"
#include "Settings.h"
#include "VertexTypes.h"

namespace Inferno {
    namespace Render {
        void BindTempConstants(ID3D12GraphicsCommandList* cmdList, const void* data, uint64 size, uint32 rootParameter);

        void BindTempConstantsCompute(ID3D12GraphicsCommandList* cmdList, const void* data, uint64 size, uint32 rootParameter);

        // Binds per-frame shader constants
        template <class T>
        void BindTempConstants(ID3D12GraphicsCommandList* cmdList, const T& data, uint32 rootParameter) {
            BindTempConstants(cmdList, &data, sizeof(data), rootParameter);
        }

        template <class T>
        void BindTempConstantsCompute(ID3D12GraphicsCommandList* cmdList, const T& data, uint32 rootParameter) {
            BindTempConstantsCompute(cmdList, &data, sizeof(data), rootParameter);
        }
    }

    using HlslBool = int32; // For alignment on GPU

    // Shader definition to allow recompilation
    struct ShaderInfo {
        string File;
        string VSEntryPoint = "vsmain";
        string PSEntryPoint = "psmain";
    };

    struct FrameConstants {
        Matrix ViewProjection;
        Matrix View;
        Vector3 Eye;
        float ElapsedTime;
        Vector2 Size;
        float NearClip, FarClip;
        Vector3 EyeDir;
        float GlobalDimming;
        Vector3 EyeUp;
        HlslBool NewLightMode;
        TextureFilterMode FilterMode;
        float RenderScale;
    };

    // Shaders can be combined with different PSOs to create several effects
    class IShader : public NonCopyable {
    public:
        IShader(ShaderInfo info) : Info(std::move(info)) {}

        ShaderInfo Info;
        D3D12_INPUT_LAYOUT_DESC InputLayout{};
        DXGI_FORMAT Format = DXGI_FORMAT_R11G11B10_FLOAT;

        ComPtr<ID3DBlob> VertexShader;
        ComPtr<ID3DBlob> PixelShader;
        ComPtr<ID3D12RootSignature> RootSignature;
    };

    constexpr D3D12_ROOT_SIGNATURE_FLAGS DefaultRootFlags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    class FlatLevelShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants,
            RootParameterCount
        };

    public:
        FlatLevelShader(const ShaderInfo& info) : IShader(info) {
            InputLayout = LevelVertex::Layout;
        }
    };

    class ComposeShader : public IShader {
        enum RootParameterIndex : uint {
            Texture,
            Sampler
        };

    public:
        ComposeShader(const ShaderInfo& info) : IShader(info) {}

        static void SetSource(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            commandList->SetGraphicsRootDescriptorTable(Texture, handle);
        }

        static void SetSampler(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sampler) {
            commandList->SetGraphicsRootDescriptorTable(Sampler, sampler);
        }
    };

    class AutomapShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants,
            InstanceConstants,
            Diffuse1,
            Diffuse2,
            Mask,
            Depth,
            Sampler,
            RootParameterCount
        };

    public:
        struct Constants {
            Color Color;
            HlslBool Flat = false;
            HlslBool HasOverlay = false;
        };

        AutomapShader(const ShaderInfo& info) : IShader(info) {
            InputLayout = LevelVertex::Layout;
        }

        static void SetConstants(ID3D12GraphicsCommandList* commandList, const Constants& constants) {
            commandList->SetGraphicsRoot32BitConstants(InstanceConstants, sizeof(constants) / 4, &constants, 0);
        }

        static void SetSampler(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sampler) {
            commandList->SetGraphicsRootDescriptorTable(Sampler, sampler);
        }

        static void SetDiffuse1(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            commandList->SetGraphicsRootDescriptorTable(Diffuse1, handle);
        }

        static void SetDiffuse2(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            commandList->SetGraphicsRootDescriptorTable(Diffuse2, handle);
        }

        static void SetMask(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            commandList->SetGraphicsRootDescriptorTable(Mask, handle);
        }

        static void SetDepth(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            commandList->SetGraphicsRootDescriptorTable(Depth, handle);
        }
    };

    class AutomapOutlineShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants,
            Depth
        };

    public:
        AutomapOutlineShader(const ShaderInfo& info) : IShader(info) {}

        static void SetDepth(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            commandList->SetGraphicsRootDescriptorTable(Depth, handle);
        }
    };

    class DepthShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants,
            RootParameterCount
        };

    public:
        constexpr static auto OutputFormat = DXGI_FORMAT_R16_FLOAT;

        DepthShader(const ShaderInfo& info) : IShader(info) {
            InputLayout = LevelVertex::Layout;
            Format = OutputFormat;
        }
    };

    class ObjectDepthShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants,
            TextureTable, // t0, space1
            RootConstants,
            DissolveTexture, // t0
            VClipTable, // t1
            Sampler,
            RootParameterCount
        };

    public:
        ObjectDepthShader(const ShaderInfo& info) : IShader(info) {
            InputLayout = ObjectVertex::Layout;
            Format = DepthShader::OutputFormat;
        }

        struct Constants {
            Matrix World;
            float PhaseAmount;
            float TimeOffset;
        };

        static void SetConstants(ID3D12GraphicsCommandList* commandList, const Constants& consts) {
            Render::BindTempConstants(commandList, consts, RootConstants);
        }

        static void SetDissolveTexture(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(DissolveTexture, texture);
        }

        static void SetSampler(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sampler) {
            commandList->SetGraphicsRootDescriptorTable(Sampler, sampler);
        }

        static void SetTextureTable(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE start) {
            commandList->SetGraphicsRootDescriptorTable(TextureTable, start);
        }

        static void SetVClipTable(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE start) {
            commandList->SetGraphicsRootDescriptorTable(VClipTable, start);
        }
    };

    class DepthCutoutShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants,
            TextureTable, // t0, space1
            RootConstants,
            Material1,
            Material2,
            StMask,
            Sampler,
            RootParameterCount
        };

    public:
        struct Constants {
            Vector2 Scroll, Scroll2; // For UV scrolling
            HlslBool HasOverlay;
            float Threshold = 0;
        };

        DepthCutoutShader(const ShaderInfo& info) : IShader(info) {
            InputLayout = LevelVertex::Layout;
            Format = DepthShader::OutputFormat;
        }

        static void SetTextureTable(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE start) {
            commandList->SetGraphicsRootDescriptorTable(TextureTable, start);
        }

        static void SetConstants(ID3D12GraphicsCommandList* commandList, const Constants& consts) {
            commandList->SetGraphicsRoot32BitConstants(RootConstants, sizeof(consts) / 4, &consts, 0);
        }

        static void SetDiffuse1(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            commandList->SetGraphicsRootDescriptorTable(Material1, handle);
        }

        static void SetDiffuse2(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            commandList->SetGraphicsRootDescriptorTable(Material2, handle);
        }

        static void SetSuperTransparent(ID3D12GraphicsCommandList* commandList, const Material2D& material) {
            commandList->SetGraphicsRootDescriptorTable(StMask, material.Handles[Material2D::SuperTransparency]);
        }

        static void SetSampler(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sampler) {
            commandList->SetGraphicsRootDescriptorTable(Sampler, sampler);
        }
    };

    class StarShader : public IShader {
    public:
        struct Parameters {
            Color AtmosphereColor;
            Color StarColor;
            float SkyRadius = 1.7f;
            float Scale = 1.0f;
        };

        StarShader(const ShaderInfo& info) : IShader(info) {}

        static void SetParameters(ID3D12GraphicsCommandList* commandList, const Parameters& consts) {
            commandList->SetGraphicsRoot32BitConstants(1, sizeof(consts) / 4, &consts, 0);
        }
    };

    class LevelShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants, // b0
            TextureTable, // t0, space1
            RootConstants, // b1
            Diffuse1, // t0
            Material1, // t1 - t4
            Diffuse2, // t5
            Material2, // t6 - t9
            Depth, // t10
            Sampler, // s0
            NormalSampler, // s1
            MaterialInfoBuffer, // t14
            Environment, // t15
            LightGrid, // t11, t12, t13, b2
        };

    public:
        struct InstanceConstants {
            Vector2 Scroll, Scroll2; // For UV scrolling
            float LightingScale;
            HlslBool Distort;
            HlslBool IsOverlay; // Use UV2s
            HlslBool HasOverlay; // Discard pixels under overlay
            int Tex1, Tex2;
            float EnvStrength;
            float Pad;
            Color LightColor;
        };

        LevelShader(const ShaderInfo& info) : IShader(info) {
            InputLayout = LevelVertex::Layout;
        }

        static void SetTextureTable(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE start) {
            commandList->SetGraphicsRootDescriptorTable(TextureTable, start);
        }

        static void SetSampler(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sampler) {
            commandList->SetGraphicsRootDescriptorTable(Sampler, sampler);
        }

        static void SetNormalSampler(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sampler) {
            commandList->SetGraphicsRootDescriptorTable(NormalSampler, sampler);
        }

        static void SetDiffuse1(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            commandList->SetGraphicsRootDescriptorTable(Diffuse1, handle);
        }

        static void SetMaterial1(ID3D12GraphicsCommandList* commandList, const Material2D& material) {
            commandList->SetGraphicsRootDescriptorTable(Material1, material.Handles[1]);
        }

        static void SetDiffuse2(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            commandList->SetGraphicsRootDescriptorTable(Diffuse2, handle);
        }

        static void SetMaterial2(ID3D12GraphicsCommandList* commandList, const Material2D& material) {
            commandList->SetGraphicsRootDescriptorTable(Material2, material.Handles[1]);
        }

        static void SetDepthTexture(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(Depth, texture);
        }

        static void SetInstanceConstants(ID3D12GraphicsCommandList* commandList, const InstanceConstants& consts) {
            Render::BindTempConstants(commandList, consts, RootConstants);
        }

        static void SetLightGrid(ID3D12GraphicsCommandList* commandList, Graphics::FillLightGridCS& lightGrid) {
            commandList->SetGraphicsRootDescriptorTable(LightGrid, lightGrid.GetSRVTable());
            commandList->SetGraphicsRootDescriptorTable(LightGrid + 1, lightGrid.GetLightGrid().GetSRV());
            commandList->SetGraphicsRootDescriptorTable(LightGrid + 2, lightGrid.GetBitMask().GetSRV());
            commandList->SetGraphicsRootConstantBufferView(LightGrid + 3, lightGrid.GetConstants());
        }

        static void SetMaterialInfoBuffer(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            commandList->SetGraphicsRootDescriptorTable(MaterialInfoBuffer, handle);
        }

        static void SetEnvironment(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            commandList->SetGraphicsRootDescriptorTable(Environment, handle);
        }
    };

    class SpriteShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants,
            RootConstants,
            Diffuse,
            Depth,
            Sampler,
            RootParameterCount
        };

    public:
        struct Constants {
            float DepthBias = 0; // Size in world units to bias the sprite towards the camera. Positive values appear in front of other geometry.
            float Softness = 0; // Value between 0 and 1 to soften the edge of the sprite when intersecting geometry. Higher is softer.
        };

        SpriteShader(const ShaderInfo& info) : IShader(info) {
            InputLayout = ObjectVertex::Layout;
        }

        static void SetSampler(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sampler) {
            commandList->SetGraphicsRootDescriptorTable(Sampler, sampler);
        }

        static void SetDiffuse(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(Diffuse, texture);
        }

        static void SetDepthTexture(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(Depth, texture);
        }

        static void SetConstants(ID3D12GraphicsCommandList* commandList, const Constants& constants) {
            commandList->SetGraphicsRoot32BitConstants(RootConstants, sizeof(constants) / 4, &constants, 0);
        }
    };

    class SunShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants,
            Diffuse,
            Sampler,
            RootParameterCount
        };

    public:
        SunShader(const ShaderInfo& info) : IShader(info) {
            InputLayout = ObjectVertex::Layout;
        }

        static void SetSampler(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sampler) {
            commandList->SetGraphicsRootDescriptorTable(Sampler, sampler);
        }

        static void SetDiffuse(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(Diffuse, texture);
        }
    };

    class AsteroidShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants, // b0
            RootConstants
        };

    public:
        struct Constants {
            Matrix World;
            Vector4 Ambient;
        };

        AsteroidShader(const ShaderInfo& info) : IShader(info) {
            InputLayout = ObjectVertex::Layout;
        }

        static void SetConstants(ID3D12GraphicsCommandList* commandList, const Constants& consts) {
            Render::BindTempConstants(commandList, consts, RootConstants);
        }
    };

    class MenuSunShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants, // b0
            RootConstants, // b1
            Noise, // t0
        };

    public:
        struct Constants {
            Matrix World;
            Vector4 Ambient;
        };

        MenuSunShader(const ShaderInfo& info) : IShader(info) {
            InputLayout = ObjectVertex::Layout;
        }

        static void SetConstants(ID3D12GraphicsCommandList* commandList, const Constants& consts) {
            Render::BindTempConstants(commandList, consts, RootConstants);
        }

        static void SetNoise(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            commandList->SetGraphicsRootDescriptorTable(Noise, handle);
        }
    };

    class ObjectShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants, // b0
            TextureTable, // t0, space1
            RootConstants, // b1
            Matcap, // t0
            MaterialInfoBuffer, // t5
            VClipTable, // t6
            DissolveTexture, // t7
            EnvironmentCube, // t8
            Sampler, // s0
            NormalSampler, // s1
            LightGrid, // t11, t12, t13, b2
        };

    public:
        struct Constants {
            Matrix World;
            Vector4 EmissiveLight, Ambient;
            Color PhaseColor; // For the leading edge of dissolves
            int TexIdOverride = -1;
            float TimeOffset;
            float PhaseAmount; // 0 to 1. 1 is fully dissolved (invisible)
        };

        ObjectShader(const ShaderInfo& info) : IShader(info) {
            InputLayout = ObjectVertex::Layout;
        }

        static void SetTextureTable(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE start) {
            commandList->SetGraphicsRootDescriptorTable(TextureTable, start);
        }

        static void SetVClipTable(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE start) {
            commandList->SetGraphicsRootDescriptorTable(VClipTable, start);
        }

        static void SetMatcap(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(Matcap, texture);
        }

        static void SetDissolveTexture(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(DissolveTexture, texture);
        }

        static void SetEnvironmentCube(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(EnvironmentCube, texture);
        }

        static void SetSampler(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sampler) {
            commandList->SetGraphicsRootDescriptorTable(Sampler, sampler);
        }

        static void SetNormalSampler(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sampler) {
            commandList->SetGraphicsRootDescriptorTable(NormalSampler, sampler);
        }

        static void SetConstants(ID3D12GraphicsCommandList* commandList, const Constants& consts) {
            Render::BindTempConstants(commandList, consts, RootConstants);
        }

        static void SetMaterialInfoBuffer(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            commandList->SetGraphicsRootDescriptorTable(MaterialInfoBuffer, handle);
        }

        static void SetLightGrid(ID3D12GraphicsCommandList* commandList, Graphics::FillLightGridCS& lightGrid) {
            commandList->SetGraphicsRootDescriptorTable(LightGrid, lightGrid.GetSRVTable());
            commandList->SetGraphicsRootDescriptorTable(LightGrid + 1, lightGrid.GetLightGrid().GetSRV());
            commandList->SetGraphicsRootDescriptorTable(LightGrid + 2, lightGrid.GetBitMask().GetSRV());
            commandList->SetGraphicsRootConstantBufferView(LightGrid + 3, lightGrid.GetConstants());
        }
    };

    class ObjectDistortionShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants, // b0
            InstanceConstants, // b1
            FrameTexture, // t0
        };

    public:
        struct Constants {
            Matrix World;
            float TimeOffset;
            float Noise, Noise2;
        };

        ObjectDistortionShader(const ShaderInfo& info) : IShader(info) {
            InputLayout = ObjectVertex::Layout;
        }

        static void SetFrameTexture(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(FrameTexture, texture);
        }

        static void SetConstants(ID3D12GraphicsCommandList* commandList, const Constants& consts) {
            Render::BindTempConstants(commandList, consts, InstanceConstants);
        }
    };

    class TerrainDepthShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants,
            RootConstants,
            RootParameterCount
        };

    public:
        constexpr static auto OutputFormat = DXGI_FORMAT_R16_FLOAT;

        struct Constants {
            Matrix World;
        };

        static void SetConstants(ID3D12GraphicsCommandList* commandList, const Constants& consts) {
            Render::BindTempConstants(commandList, consts, RootConstants);
        }

        TerrainDepthShader(const ShaderInfo& info) : IShader(info) {
            InputLayout = ObjectVertex::Layout;
            Format = OutputFormat;
        }
    };

    class TerrainShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants, // b0
            RootConstants, // b1
            Diffuse, // t0
            Material, // t1 - t4
            Sampler, // s0
            NormalSampler, // s1
            LightGrid, // t11, t12, t13, b2
        };

    public:
        struct Constants {
            Matrix World;
            Vector4 Light;
            Vector3 LightDir;
            float Padding;
        };

        TerrainShader(const ShaderInfo& info) : IShader(info) {
            InputLayout = ObjectVertex::Layout;
        }

        static void SetDiffuse(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            commandList->SetGraphicsRootDescriptorTable(Diffuse, handle);
        }

        static void SetSampler(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sampler) {
            commandList->SetGraphicsRootDescriptorTable(Sampler, sampler);
        }

        static void SetNormalSampler(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sampler) {
            commandList->SetGraphicsRootDescriptorTable(NormalSampler, sampler);
        }

        static void SetConstants(ID3D12GraphicsCommandList* commandList, const Constants& consts) {
            Render::BindTempConstants(commandList, consts, RootConstants);
        }

        static void SetLightGrid(ID3D12GraphicsCommandList* commandList, Graphics::FillLightGridCS& lightGrid) {
            commandList->SetGraphicsRootDescriptorTable(LightGrid, lightGrid.GetSRVTable());
            commandList->SetGraphicsRootDescriptorTable(LightGrid + 1, lightGrid.GetLightGrid().GetSRV());
            commandList->SetGraphicsRootDescriptorTable(LightGrid + 2, lightGrid.GetBitMask().GetSRV());
            commandList->SetGraphicsRootConstantBufferView(LightGrid + 3, lightGrid.GetConstants());
        }
    };

    class FlatShader : public IShader {
        enum RootParameterIndex : uint {
            ConstantBuffer
        };

    public:
        FlatShader(const ShaderInfo& info) : IShader(info) {
            InputLayout = FlatVertex::Layout;
        }

        struct Constants {
            Matrix Transform = Matrix::Identity;
            Color Tint = { 1, 1, 1, 1 };
        };

        static void SetConstants(ID3D12GraphicsCommandList* commandList, const Constants& constants) {
            Render::BindTempConstants(commandList, constants, ConstantBuffer);
        }
    };

    class UIShader : public IShader {
        enum RootParameterIndex : uint {
            Constants,
            Diffuse,
            Sampler,
            RootParameterCount
        };

    public:
        UIShader(const ShaderInfo& info) : IShader(info) {
            InputLayout = CanvasVertex::Layout;
            Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // Draws directly to SRGB back buffer
        }

        static void SetDiffuse(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(Diffuse, texture);
        }

        static void SetWorldViewProjection(ID3D12GraphicsCommandList* commandList, const Matrix& wvp) {
            commandList->SetGraphicsRoot32BitConstants(Constants, sizeof(wvp) / 4, &wvp.m, 0);
        }

        static void SetSampler(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sampler) {
            commandList->SetGraphicsRootDescriptorTable(Sampler, sampler);
        }
    };

    class BriefingShader : public UIShader {
    public:
        BriefingShader(const ShaderInfo& info) : UIShader(info) {
            InputLayout = CanvasVertex::Layout;
            Format = DXGI_FORMAT_R8G8B8A8_UNORM; // Draws to intermediate linear buffer
        }
    };

    class HudShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants,
            RootConstants,
            Diffuse,
            Sampler,
        };

    public:
        struct Constants {
            Matrix Transform = Matrix::Identity;
            Color Tint = { 1, 1, 1 };
            float Scanline = 0;
        };

        HudShader(const ShaderInfo& info) : IShader(info) {
            InputLayout = HudVertex::Layout;
            Format = DXGI_FORMAT_R11G11B10_FLOAT;
        }

        static void SetDiffuse(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(Diffuse, texture);
        }

        static void SetConstants(ID3D12GraphicsCommandList* commandList, const Constants& constants) {
            Render::BindTempConstants(commandList, constants, RootConstants);
        }

        static void SetSampler(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sampler) {
            commandList->SetGraphicsRootDescriptorTable(Sampler, sampler);
        }
    };


    void CompileShader(IShader*);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC BuildPipelineStateDesc(EffectSettings info, IShader* shader, bool useStencil, uint msaaSamples, uint renderTargets = 1);

    struct ShaderResources {
        LevelShader Level = ShaderInfo{ "shaders/level.hlsl" };
        FlatLevelShader LevelFlat = ShaderInfo{ "shaders/levelflat.hlsl" };
        FlatShader Flat = ShaderInfo{ "shaders/editor.hlsl" };
        DepthShader Depth = ShaderInfo{ "shaders/Depth.hlsl" };
        ObjectDepthShader DepthObject = ShaderInfo{ "shaders/DepthObject.hlsl" };
        DepthCutoutShader DepthCutout = ShaderInfo{ "shaders/DepthCutout.hlsl" };
        UIShader UserInterface = ShaderInfo{ "shaders/imgui.hlsl" };
        BriefingShader Briefing = ShaderInfo{ "shaders/imgui.hlsl" };
        HudShader Hud = ShaderInfo{ "shaders/HUD.hlsl" };
        SpriteShader Sprite = ShaderInfo{ "shaders/sprite.hlsl" };
        ObjectShader Object = ShaderInfo{ "shaders/object.hlsl" };
        ObjectShader AutomapObject = ShaderInfo{ "shaders/AutomapObject.hlsl" };
        ObjectShader BriefingObject = ShaderInfo{ "shaders/BriefingObject.hlsl" };
        TerrainDepthShader TerrainDepth = ShaderInfo{ "shaders/TerrainDepth.hlsl" };
        TerrainShader Terrain = ShaderInfo{ "shaders/Terrain.hlsl" };
        ObjectDistortionShader ObjectDistortion = ShaderInfo{ "shaders/Cloak.hlsl" };
        StarShader Stars = ShaderInfo{ "shaders/stars.hlsl" };
        SpriteShader Sun = ShaderInfo{ "shaders/Sun.hlsl" };
        AutomapShader Automap = ShaderInfo{ "shaders/Automap.hlsl" };
        AutomapOutlineShader AutomapOutline = ShaderInfo{ "shaders/AutomapOutline.hlsl" };
        AsteroidShader Asteroid = ShaderInfo{ "shaders/Asteroid.hlsl" };
        MenuSunShader MenuSun = ShaderInfo{ "shaders/MenuSun.hlsl" };
        ComposeShader Composition = ShaderInfo{ "shaders/Compose.hlsl" };
    };

    class EffectResources {
        ShaderResources* _shaders;

    public:
        EffectResources(ShaderResources* shaders) : _shaders(shaders) {}

        Effect<LevelShader> Level = { &_shaders->Level, { BlendMode::Opaque, CullMode::CounterClockwise, DepthMode::Read, StencilMode::PortalRead } };
        Effect<LevelShader> LevelWall = { &_shaders->Level, { BlendMode::Alpha, CullMode::CounterClockwise, DepthMode::Read, StencilMode::PortalRead } };
        Effect<LevelShader> LevelWallAdditive = { &_shaders->Level, { BlendMode::Additive, CullMode::CounterClockwise, DepthMode::Read, StencilMode::PortalRead } };
        Effect<FlatLevelShader> LevelFlat = { &_shaders->LevelFlat, { BlendMode::Opaque, CullMode::CounterClockwise, DepthMode::Read, StencilMode::PortalRead } };
        Effect<FlatLevelShader> LevelWallFlat = { &_shaders->LevelFlat, { BlendMode::Alpha, CullMode::CounterClockwise, DepthMode::Read, StencilMode::PortalRead } };

        Effect<AutomapShader> Automap = { &_shaders->Automap, { BlendMode::Opaque, CullMode::CounterClockwise, DepthMode::ReadWrite } };
        Effect<AutomapShader> AutomapTransparent = { &_shaders->Automap, { BlendMode::Additive, CullMode::CounterClockwise, DepthMode::Read } };
        Effect<AutomapOutlineShader> AutomapOutline = { &_shaders->AutomapOutline, { BlendMode::Alpha, CullMode::None, DepthMode::None } };

        Effect<DepthShader> Depth = { &_shaders->Depth, { .Blend = BlendMode::Opaque, .Stencil = StencilMode::PortalRead } };
        Effect<DepthCutoutShader> DepthCutout = { &_shaders->DepthCutout, { .Blend = BlendMode::Opaque, .Stencil = StencilMode::PortalRead } };
        Effect<ObjectDepthShader> DepthObject = { &_shaders->DepthObject, { .Blend = BlendMode::Opaque, .Culling = CullMode::None, .Stencil = StencilMode::PortalRead } };
        Effect<ObjectDepthShader> DepthObjectFlipped = { &_shaders->DepthObject, { .Blend = BlendMode::Opaque, .Culling = CullMode::Clockwise, .Stencil = StencilMode::PortalRead } };

        Effect<ObjectShader> Object = { &_shaders->Object, { .Blend = BlendMode::Alpha, .Culling = CullMode::None, .Depth = DepthMode::Read, .Stencil = StencilMode::PortalRead } };
        Effect<ObjectShader> ObjectGlow = { &_shaders->Object, { .Blend = BlendMode::Additive, .Culling = CullMode::None, .Depth = DepthMode::Read, .Stencil = StencilMode::PortalRead } };
        Effect<ObjectDistortionShader> ObjectDistortion{ &_shaders->ObjectDistortion, { .Blend = BlendMode::Alpha, .Culling = CullMode::CounterClockwise, .Depth = DepthMode::Read, .Stencil = StencilMode::PortalRead } };
        Effect<ObjectShader> BriefingObject = { &_shaders->BriefingObject, { BlendMode::Alpha, CullMode::None, DepthMode::ReadWrite } };
        Effect<ObjectShader> AutomapObject = { &_shaders->AutomapObject, { .Blend = BlendMode::Alpha, .Culling = CullMode::None, .Depth = DepthMode::ReadWrite } };

        Effect<UIShader> UserInterface = { &_shaders->UserInterface, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None, StencilMode::None, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, false } };
        Effect<BriefingShader> Briefing = { &_shaders->Briefing, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None, StencilMode::None, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, false } };
        Effect<HudShader> Hud = { &_shaders->Hud, { .Blend = BlendMode::StraightAlpha, .Culling = CullMode::None, .Depth = DepthMode::None, .EnableMultisample = false } };
        Effect<HudShader> HudAdditive = { &_shaders->Hud, { .Blend = BlendMode::Additive, .Culling = CullMode::None, .Depth = DepthMode::None, .EnableMultisample = false } };
        Effect<FlatShader> Flat = { &_shaders->Flat, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None/*, StencilMode::PortalRead*/ } };
        Effect<FlatShader> FlatAdditive = { &_shaders->Flat, { BlendMode::Additive, CullMode::CounterClockwise, DepthMode::Read/*, StencilMode::PortalRead*/ } };
        Effect<FlatShader> EditorSelection = { &_shaders->Flat, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None/*, StencilMode::PortalRead*/ } };
        Effect<FlatShader> Line = { &_shaders->Flat, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None, StencilMode::None, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE } };

        Effect<SpriteShader> Sprite = { &_shaders->Sprite, { BlendMode::Alpha, CullMode::CounterClockwise, DepthMode::Read, StencilMode::PortalRead } };
        Effect<SpriteShader> SpriteTerrain = { &_shaders->Sprite, { BlendMode::Alpha, CullMode::CounterClockwise, DepthMode::Read } };
        //Effect<SpriteShader> SpriteOpaque = { &_shaders->Sprite, { BlendMode::Alpha, CullMode::CounterClockwise, DepthMode::ReadWrite, StencilMode::PortalRead } };
        Effect<SpriteShader> SpriteAdditive = { &_shaders->Sprite, { BlendMode::Additive, CullMode::CounterClockwise, DepthMode::Read, StencilMode::PortalRead } };
        Effect<SpriteShader> SpriteAdditiveTerrain = { &_shaders->Sprite, { BlendMode::Additive, CullMode::CounterClockwise, DepthMode::Read } };
        //Effect<SpriteShader> SpriteAdditiveBiased = { &_shaders->Sprite, { BlendMode::Additive, CullMode::CounterClockwise, DepthMode::ReadDecalBiased, StencilMode::PortalRead } };
        Effect<SpriteShader> SpriteMultiply = { &_shaders->Sprite, { BlendMode::Multiply, CullMode::CounterClockwise, DepthMode::ReadDecalBiased, StencilMode::PortalRead } };

        Effect<SpriteShader> Sun = { &_shaders->Sun, { BlendMode::Additive, CullMode::CounterClockwise, DepthMode::Read } };
        Effect<StarShader> Stars = { &_shaders->Stars, { BlendMode::Opaque, CullMode::None, DepthMode::None } };
        Effect<AsteroidShader> Asteroid = { &_shaders->Asteroid, { .Blend = BlendMode::Alpha, .Culling = CullMode::Clockwise, .Depth = DepthMode::ReadWrite } };
        Effect<MenuSunShader> MenuSun = { &_shaders->MenuSun, { .Blend = BlendMode::Alpha, .Culling = CullMode::Clockwise, .Depth = DepthMode::ReadWrite } };

        Effect<TerrainShader> Terrain = { &_shaders->Terrain, { .Depth = DepthMode::Read, .Stencil = StencilMode::PortalReadNeq } };
        Effect<TerrainDepthShader> TerrainDepth = { &_shaders->TerrainDepth, { .Depth = DepthMode::ReadWrite, .Stencil = StencilMode::PortalReadNeq } };
        Effect<DepthShader> TerrainPortal = { &_shaders->Depth, { .Depth = DepthMode::Read, .Stencil = StencilMode::PortalWrite } };
        Effect<ObjectShader> TerrainObject = { &_shaders->Object, { .Blend = BlendMode::Alpha, .Culling = CullMode::None, .Depth = DepthMode::Read } };
        Effect<ObjectDepthShader> TerrainDepthObject = { &_shaders->DepthObject, { .Blend = BlendMode::Opaque, .Culling = CullMode::None } };
        Effect<ComposeShader> Compose = { &_shaders->Composition, { .Blend = BlendMode::Alpha, .Culling = CullMode::None, .Depth = DepthMode::None, .EnableMultisample = false } };

        void Compile(ID3D12Device* device, uint msaaSamples) {
            CompileShader(&_shaders->Flat);
            CompileShader(&_shaders->Level);
            CompileShader(&_shaders->LevelFlat);
            CompileShader(&_shaders->UserInterface);
            CompileShader(&_shaders->Briefing);
            CompileShader(&_shaders->Sprite);
            CompileShader(&_shaders->Object);
            CompileShader(&_shaders->BriefingObject);
            CompileShader(&_shaders->ObjectDistortion);
            CompileShader(&_shaders->Depth);
            CompileShader(&_shaders->DepthObject);
            CompileShader(&_shaders->DepthCutout);
            CompileShader(&_shaders->Hud);
            CompileShader(&_shaders->TerrainDepth);
            CompileShader(&_shaders->Terrain);
            CompileShader(&_shaders->Stars);
            CompileShader(&_shaders->Sun);
            CompileShader(&_shaders->Automap);
            CompileShader(&_shaders->AutomapObject);
            CompileShader(&_shaders->AutomapOutline);
            CompileShader(&_shaders->Asteroid);
            CompileShader(&_shaders->MenuSun);
            CompileShader(&_shaders->Composition);

            auto compile = [&](auto& effect, bool useStencil = true, uint renderTargets = 1) {
                try {
                    auto psoDesc = BuildPipelineStateDesc(effect.Settings, effect.Shader, useStencil, msaaSamples, renderTargets);
                    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&effect.PipelineState)));
                }
                catch (const std::exception& e) {
                    SPDLOG_ERROR("Unable to compile shader: {}", e.what());
                }
            };

            compile(Level);
            compile(LevelWall);
            compile(LevelWallAdditive);

            compile(Depth);
            compile(DepthObject);
            compile(DepthObjectFlipped);
            compile(DepthCutout);

            compile(LevelFlat);
            compile(LevelWallFlat);
            compile(Automap);
            compile(AutomapTransparent);
            compile(AutomapObject);
            compile(AutomapOutline);

            compile(Terrain);
            compile(TerrainDepth);
            compile(TerrainPortal);
            compile(TerrainObject);
            compile(TerrainDepthObject);

            compile(Object);
            compile(ObjectGlow);
            compile(ObjectDistortion);
            compile(BriefingObject, false);
            compile(Sprite);
            compile(SpriteTerrain);
            //compile(SpriteOpaque);
            compile(SpriteAdditive);
            compile(SpriteAdditiveTerrain);
            compile(SpriteMultiply);
            //compile(SpriteAdditiveBiased);

            compile(Flat);
            compile(FlatAdditive);
            compile(EditorSelection);
            compile(Line);

            compile(UserInterface, false);
            compile(Briefing, false);
            compile(Hud);
            compile(HudAdditive);

            compile(Stars);
            compile(Sun);
            compile(MenuSun);
            compile(Asteroid);
            compile(Compose, false);
        }
    };
}
