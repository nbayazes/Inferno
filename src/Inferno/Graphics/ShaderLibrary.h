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

        // Binds per-frame shader constants
        template <class T>
        void BindTempConstants(ID3D12GraphicsCommandList* cmdList, const T& data, uint32 rootParameter) {
            BindTempConstants(cmdList, &data, sizeof(data), rootParameter);
        }
    }

    using HlslBool = int32; // For alignment on GPU

    // Shader definition to allow recompilation
    struct ShaderInfo {
        std::wstring File;
        wstring VSEntryPoint = L"vsmain";
        wstring PSEntryPoint = L"psmain";
    };

    struct FrameConstants {
        Matrix ViewProjection;
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
    class IShader {
    public:
        IShader(ShaderInfo info) : Info(std::move(info)) {}
        virtual ~IShader() = default;

        ShaderInfo Info;
        D3D12_INPUT_LAYOUT_DESC InputLayout{};
        DXGI_FORMAT Format = DXGI_FORMAT_R11G11B10_FLOAT;

        ComPtr<ID3DBlob> VertexShader;
        ComPtr<ID3DBlob> PixelShader;
        ComPtr<ID3D12RootSignature> RootSignature;

        void Apply(ID3D12GraphicsCommandList* commandList) const {
            commandList->SetGraphicsRootSignature(RootSignature.Get());
        }
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
            Diffuse,
            Depth,
            Sampler,
            RootParameterCount
        };

    public:
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

    class ObjectShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants, // b0
            TextureTable, // t0, space1
            RootConstants, // b1
            Material, // t0 - t4
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
            Vector4 Ambient;
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
            RootConstants,
            Diffuse,
            Sampler,
            RootParameterCount
        };

    public:
        struct Constants {
            Matrix Transform = Matrix::Identity;
            Color Tint = { 1, 1, 1 };
            float ScanlinePitch = 0;
            float ScanelineIntensity = 0;
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
    };


    void CompileShader(IShader*);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC BuildPipelineStateDesc(EffectSettings effect, IShader* shader, uint msaaSamples, uint renderTargets = 1);

    struct ShaderResources {
        LevelShader Level = ShaderInfo{ L"shaders/level.hlsl" };
        FlatLevelShader LevelFlat = ShaderInfo{ L"shaders/levelflat.hlsl" };
        FlatShader Flat = ShaderInfo{ L"shaders/editor.hlsl" };
        DepthShader Depth = ShaderInfo{ L"shaders/Depth.hlsl" };
        ObjectDepthShader DepthObject = ShaderInfo{ L"shaders/DepthObject.hlsl" };
        DepthCutoutShader DepthCutout = ShaderInfo{ L"shaders/DepthCutout.hlsl" };
        UIShader UserInterface = ShaderInfo{ L"shaders/imgui.hlsl" };
        BriefingShader Briefing = ShaderInfo{ L"shaders/imgui.hlsl" };
        HudShader Hud = ShaderInfo{ L"shaders/HUD.hlsl" };
        SpriteShader Sprite = ShaderInfo{ L"shaders/sprite.hlsl" };
        ObjectShader Object = ShaderInfo{ L"shaders/object.hlsl" };
        ObjectShader BriefingObject = ShaderInfo{ L"shaders/BriefingObject.hlsl" };
        TerrainShader Terrain = ShaderInfo{ L"shaders/Terrain.hlsl" };
        ObjectDistortionShader ObjectDistortion = ShaderInfo{ L"shaders/Cloak.hlsl" };
        StarShader Stars = ShaderInfo{ L"shaders/stars.hlsl" };
        SpriteShader Sun = ShaderInfo{ L"shaders/Sun.hlsl" };
    };

    class EffectResources {
        ShaderResources* _shaders;

    public:
        EffectResources(ShaderResources* shaders) : _shaders(shaders) {}

        Effect<LevelShader> Level = { &_shaders->Level, { BlendMode::Opaque, CullMode::CounterClockwise, DepthMode::Read } };
        Effect<LevelShader> LevelWall = { &_shaders->Level, { BlendMode::Alpha, CullMode::CounterClockwise, DepthMode::Read } };
        Effect<LevelShader> LevelWallAdditive = { &_shaders->Level, { BlendMode::Additive, CullMode::CounterClockwise, DepthMode::Read } };
        Effect<FlatLevelShader> LevelFlat = { &_shaders->LevelFlat, { BlendMode::Opaque, CullMode::CounterClockwise, DepthMode::Read } };
        Effect<FlatLevelShader> LevelWallFlat = { &_shaders->LevelFlat, { BlendMode::Alpha, CullMode::CounterClockwise, DepthMode::Read } };

        Effect<TerrainShader> Terrain = { &_shaders->Terrain, { BlendMode::Opaque, CullMode::CounterClockwise, DepthMode::ReadWrite } };

        Effect<DepthShader> Depth = { &_shaders->Depth, { BlendMode::Opaque } };
        Effect<DepthCutoutShader> DepthCutout = { &_shaders->DepthCutout, { BlendMode::Opaque } };
        Effect<ObjectDepthShader> DepthObject = { &_shaders->DepthObject, { BlendMode::Opaque, CullMode::None } };
        Effect<ObjectDepthShader> DepthObjectFlipped = { &_shaders->DepthObject, { BlendMode::Opaque, CullMode::Clockwise } };

        Effect<ObjectShader> Object = { &_shaders->Object, { BlendMode::Alpha, CullMode::None, DepthMode::Read } };
        Effect<ObjectShader> BriefingObject = { &_shaders->BriefingObject, { BlendMode::Alpha, CullMode::None, DepthMode::ReadWrite } };
        Effect<ObjectShader> ObjectGlow = { &_shaders->Object, { BlendMode::Additive, CullMode::None, DepthMode::Read } };
        Effect<ObjectDistortionShader> ObjectDistortion{ &_shaders->ObjectDistortion, { BlendMode::Alpha, CullMode::CounterClockwise, DepthMode::Read } };

        Effect<UIShader> UserInterface = { &_shaders->UserInterface, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, false } };
        Effect<BriefingShader> Briefing = { &_shaders->Briefing, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, false } };
        Effect<HudShader> Hud = { &_shaders->Hud, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None } };
        Effect<HudShader> HudAdditive = { &_shaders->Hud, { BlendMode::Additive, CullMode::None, DepthMode::None } };
        Effect<FlatShader> Flat = { &_shaders->Flat, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None } };
        Effect<FlatShader> FlatAdditive = { &_shaders->Flat, { BlendMode::Additive, CullMode::CounterClockwise, DepthMode::Read } };
        Effect<FlatShader> EditorSelection = { &_shaders->Flat, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None } };
        Effect<FlatShader> Line = { &_shaders->Flat, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE } };

        Effect<SpriteShader> Sprite = { &_shaders->Sprite, { BlendMode::Alpha, CullMode::CounterClockwise, DepthMode::Read } };
        Effect<SpriteShader> SpriteOpaque = { &_shaders->Sprite, { BlendMode::Alpha, CullMode::CounterClockwise, DepthMode::ReadWrite } };
        Effect<SpriteShader> SpriteAdditive = { &_shaders->Sprite, { BlendMode::Additive, CullMode::CounterClockwise, DepthMode::Read } };
        Effect<SpriteShader> SpriteAdditiveBiased = { &_shaders->Sprite, { BlendMode::Additive, CullMode::CounterClockwise, DepthMode::ReadDecalBiased } };
        Effect<SpriteShader> SpriteMultiply = { &_shaders->Sprite, { BlendMode::Multiply, CullMode::CounterClockwise, DepthMode::ReadDecalBiased } };

        Effect<SpriteShader> Sun = { &_shaders->Sun, { BlendMode::Additive, CullMode::CounterClockwise, DepthMode::Read } };
        Effect<StarShader> Stars = { &_shaders->Stars, { BlendMode::Opaque, CullMode::None, DepthMode::None } };

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
            CompileShader(&_shaders->Terrain);
            CompileShader(&_shaders->Stars);
            CompileShader(&_shaders->Sun);

            auto compile = [&](auto& effect, uint renderTargets = 1) {
                try {
                    auto psoDesc = BuildPipelineStateDesc(effect.Settings, effect.Shader, msaaSamples, renderTargets);
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

            compile(Terrain);

            compile(Object);
            compile(ObjectGlow);
            compile(ObjectDistortion);
            compile(BriefingObject);
            compile(Sprite);
            compile(SpriteOpaque);
            compile(SpriteAdditive);
            compile(SpriteMultiply);
            compile(SpriteAdditiveBiased);

            compile(Flat);
            compile(FlatAdditive);
            compile(EditorSelection);
            compile(Line);

            compile(UserInterface);
            compile(Briefing);
            compile(Hud);
            compile(HudAdditive);

            compile(Stars);
            compile(Sun);
        }
    };
}
