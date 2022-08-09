#pragma once

#include "DirectX.h"
#include "Effects.h"
#include <typeindex>
#include "MaterialLibrary.h"

namespace Inferno {
    using Inferno::Render::Material2D;

    // Shader definition to allow recompilation
    struct ShaderInfo {
        std::wstring File;
        string VSEntryPoint = "vsmain";
        string PSEntryPoint = "psmain";
    };

    struct FrameConstants {
        Matrix ViewProjection;
        Vector3 Eye;
        float ElapsedTime;
        Vector2 FrameSize;
        float NearClip, FarClip;
    };

    using HlslBool = int32; // For alignment on GPU

    constexpr D3D12_INPUT_LAYOUT_DESC CreateLayout(span<const D3D12_INPUT_ELEMENT_DESC> desc) {
        return { desc.data(), (uint)desc.size() };
    };

    struct LevelVertex {
        Vector3 Position;
        Vector2 UV;
        Vector4 Color;
        Vector2 UV2; // for overlay texture
        Vector3 Normal;

        static inline const D3D12_INPUT_ELEMENT_DESC Description[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        static inline const D3D12_INPUT_LAYOUT_DESC Layout = CreateLayout(Description);
    };

    struct FlatVertex {
        Vector3 Position;
        Color Color;

        static inline const D3D12_INPUT_ELEMENT_DESC Description[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        static inline const D3D12_INPUT_LAYOUT_DESC Layout = CreateLayout(Description);
    };

    // this should match imgui shader
    struct CanvasVertex {
        Vector2 Position;
        Vector2 UV;
        uint32 Color;

        static inline const D3D12_INPUT_ELEMENT_DESC Description[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        static inline const D3D12_INPUT_LAYOUT_DESC Layout = CreateLayout(Description);
    };

    //struct HudVertex {
    //    Vector2 Position;
    //    Vector2 UV;
    //    Color Color;

    //    static inline const D3D12_INPUT_ELEMENT_DESC Description[] = {
    //        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    //        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    //        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    //    };

    //    static inline const D3D12_INPUT_LAYOUT_DESC Layout = CreateLayout(Description);
    //};

    struct ObjectVertex {
        Vector3 Position;
        Vector2 UV;
        Color Color;
        Vector3 Normal;

        static inline const D3D12_INPUT_ELEMENT_DESC Description[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        static inline const D3D12_INPUT_LAYOUT_DESC Layout = CreateLayout(Description);
    };

    // Shaders can be combined with different PSOs to create several effects
    class IShader {
    public:
        IShader(ShaderInfo info) : Info(info) {}
        virtual ~IShader() = default;

        ShaderInfo Info;
        D3D12_INPUT_LAYOUT_DESC InputLayout;
        DXGI_FORMAT Format = DXGI_FORMAT_R11G11B10_FLOAT;

        ComPtr<ID3DBlob> VertexShader;
        ComPtr<ID3DBlob> PixelShader;
        ComPtr<ID3D12RootSignature> RootSignature;

        void Apply(ID3D12GraphicsCommandList* commandList) {
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
        FlatLevelShader(ShaderInfo info) : IShader(info) {
            InputLayout = LevelVertex::Layout;
        }
    };

    class DepthShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants,
            RootParameterCount
        };
    public:
        const static auto OutputFormat = DXGI_FORMAT_R16_FLOAT;

        DepthShader(ShaderInfo info) : IShader(info) {
            InputLayout = LevelVertex::Layout;
            Format = OutputFormat;
        }
    };

    class ObjectDepthShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants,
            RootConstants,
            RootParameterCount
        };
    public:
        ObjectDepthShader(ShaderInfo info) : IShader(info) {
            InputLayout = LevelVertex::Layout;
            Format = DepthShader::OutputFormat;
        }

        struct Constants {
            Matrix World;
        };

        void SetConstants(ID3D12GraphicsCommandList* commandList, const Constants& consts) {
            commandList->SetGraphicsRoot32BitConstants(RootConstants, sizeof(consts) / 4, &consts, 0);
        }
    };

    class DepthCutoutShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants,
            RootConstants,
            Material1,
            Material2,
            Sampler,
            RootParameterCount
        };
    public:
        struct Constants {
            Vector2 Scroll, Scroll2; // For UV scrolling
            HlslBool HasOverlay;
            float Threshold = 0;
        };

        DepthCutoutShader(ShaderInfo info) : IShader(info) {
            InputLayout = LevelVertex::Layout;
            Format = DepthShader::OutputFormat;
        }

        void SetConstants(ID3D12GraphicsCommandList* commandList, const Constants& consts) {
            commandList->SetGraphicsRoot32BitConstants(RootConstants, sizeof(consts) / 4, &consts, 0);
        }

        void SetMaterial1(ID3D12GraphicsCommandList* commandList, const Material2D& material) {
            commandList->SetGraphicsRootDescriptorTable(Material1, material.Handles[Material2D::Diffuse]);
        }

        void SetMaterial2(ID3D12GraphicsCommandList* commandList, const Material2D& material) {
            commandList->SetGraphicsRootDescriptorTable(Material2, material.Handles[Material2D::SuperTransparency]);
        }

        void SetSampler(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sampler) {
            commandList->SetGraphicsRootDescriptorTable(Sampler, sampler);
        }
    };

    class LevelShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants,
            RootConstants,
            Material1,
            Material2,
            Depth,
            Sampler,
            RootParameterCount
        };
    public:
        struct InstanceConstants {
            Vector2 Scroll, Scroll2; // For UV scrolling
            float LightingScale;
            HlslBool Distort;
            HlslBool Overlay;
        };

        LevelShader(ShaderInfo info) : IShader(info) {
            InputLayout = LevelVertex::Layout;
        }

        void SetSampler(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sampler) {
            commandList->SetGraphicsRootDescriptorTable(Sampler, sampler);
        }

        void SetMaterial1(ID3D12GraphicsCommandList* commandList, const Material2D& material) {
            commandList->SetGraphicsRootDescriptorTable(Material1, material.Handles[0]);
        }

        void SetMaterial2(ID3D12GraphicsCommandList* commandList, const Material2D& material) {
            commandList->SetGraphicsRootDescriptorTable(Material2, material.Handles[0]);
        }

        void SetDepthTexture(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(Depth, texture);
        }

        void SetInstanceConstants(ID3D12GraphicsCommandList* commandList, const InstanceConstants& consts) {
            commandList->SetGraphicsRoot32BitConstants(RootConstants, sizeof(consts) / 4, &consts, 0);
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
        SpriteShader(ShaderInfo info) : IShader(info) {
            InputLayout = ObjectVertex::Layout;
        }

        void SetSampler(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sampler) {
            commandList->SetGraphicsRootDescriptorTable(Sampler, sampler);
        }

        void SetDiffuse(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(Diffuse, texture);
        }

        void SetDepthTexture(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(Depth, texture);
        }
    };

    class ObjectShader : public IShader {
        enum RootParameterIndex : uint {
            FrameConstants,
            RootConstants,
            Material,
            Sampler,
            RootParameterCount
        };
    public:
        struct Constants {
            Matrix World;
            Vector4 EmissiveLight, Ambient;
        };

        ObjectShader(ShaderInfo info) : IShader(info) {
            InputLayout = ObjectVertex::Layout;
        }

        void SetMaterial(ID3D12GraphicsCommandList* commandList, const Material2D& material) {
            commandList->SetGraphicsRootDescriptorTable(Material, material.Handles[0]);
        }

        void SetMaterial(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            commandList->SetGraphicsRootDescriptorTable(Material, handle);
        }

        void SetSampler(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sampler) {
            commandList->SetGraphicsRootDescriptorTable(Sampler, sampler);
        }

        void SetConstants(ID3D12GraphicsCommandList* commandList, const Constants& consts) {
            commandList->SetGraphicsRoot32BitConstants(RootConstants, sizeof(consts) / 4, &consts, 0);
        }
    };

    class FlatShader : public IShader {
        enum RootParameterIndex : uint {
            ConstantBuffer,
            RootParameterCount
        };
    public:
        FlatShader(ShaderInfo info) : IShader(info) {
            InputLayout = FlatVertex::Layout;
        }

        struct Constants {
            Matrix Transform = Matrix::Identity;
            Color Tint = { 1, 1, 1, 1 };
        };

        void SetConstants(ID3D12GraphicsCommandList* commandList, const Constants& constants) {
            commandList->SetGraphicsRoot32BitConstants(0, sizeof(constants) / 4, &constants, 0);
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
        UIShader(ShaderInfo info) : IShader(info) {
            InputLayout = CanvasVertex::Layout;
            Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        }

        void SetDiffuse(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(Diffuse, texture);
        }

        void SetWorldViewProjection(ID3D12GraphicsCommandList* commandList, const Matrix& wvp) {
            commandList->SetGraphicsRoot32BitConstants(Constants, sizeof(wvp) / 4, &wvp.m, 0);
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

        HudShader(ShaderInfo info) : IShader(info) {
            InputLayout = CanvasVertex::Layout;
            Format = DXGI_FORMAT_R11G11B10_FLOAT;
        }

        void SetDiffuse(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(Diffuse, texture);
        }

        void SetConstants(ID3D12GraphicsCommandList* commandList, const Constants& constants) {
            commandList->SetGraphicsRoot32BitConstants(RootConstants, sizeof(constants) / 4, &constants, 0);
        }
    };

    enum class BlendMode { Opaque, Alpha, StraightAlpha, Additive };
    enum class CullMode { None, CounterClockwise, Clockwise };
    enum class DepthMode { Default, Read, None };

    struct EffectSettings {
        BlendMode Blend = BlendMode::Opaque;
        CullMode Culling = CullMode::CounterClockwise;
        DepthMode Depth = DepthMode::Default;
        D3D12_PRIMITIVE_TOPOLOGY_TYPE TopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE::D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        bool EnableMultisample = true;
    };

    template<class TShader>
    struct Effect {
        Effect(TShader* shader, EffectSettings settings = {})
            : Shader(shader), Settings(settings) {
        }

        EffectSettings Settings;
        TShader* Shader;
        ComPtr<ID3D12PipelineState> PipelineState;

        void Apply(ID3D12GraphicsCommandList* commandList) {
            commandList->SetPipelineState(PipelineState.Get());
            Shader->Apply(commandList);
        };
    };

    void CompileShader(IShader*) noexcept;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC BuildPipelineStateDesc(EffectSettings effect, IShader* shader, uint msaaSamples, uint renderTargets = 1);

    struct ShaderResources {
        LevelShader Level = ShaderInfo{ L"shaders/level.hlsl" };
        FlatLevelShader LevelFlat = ShaderInfo{ L"shaders/levelflat.hlsl" };
        FlatShader Flat = ShaderInfo{ L"shaders/editor.hlsl" };
        DepthShader Depth = ShaderInfo{ L"shaders/Depth.hlsl" };
        ObjectDepthShader DepthObject = ShaderInfo{ L"shaders/DepthObject.hlsl" };
        DepthCutoutShader DepthCutout = ShaderInfo{ L"shaders/DepthCutout.hlsl" };
        UIShader UserInterface = ShaderInfo{ L"shaders/imgui.hlsl" };
        HudShader Hud = ShaderInfo{ L"shaders/HUD.hlsl" };
        SpriteShader Sprite = ShaderInfo{ L"shaders/sprite.hlsl" };
        ObjectShader Object = ShaderInfo{ L"shaders/object.hlsl" };
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
        
        Effect<DepthShader> Depth = { &_shaders->Depth, { BlendMode::Opaque } };
        Effect<ObjectDepthShader> DepthObject = { &_shaders->DepthObject, { BlendMode::Opaque } };
        Effect<DepthCutoutShader> DepthCutout = { &_shaders->DepthCutout, { BlendMode::Opaque } };
        
        Effect<ObjectShader> Object = { &_shaders->Object, { BlendMode::Alpha, CullMode::CounterClockwise, DepthMode::Read } };
        Effect<ObjectShader> ObjectGlow = { &_shaders->Object, { BlendMode::Additive, CullMode::None, DepthMode::Read } };
        
        Effect<UIShader> UserInterface = { &_shaders->UserInterface, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, false } };
        Effect<HudShader> Hud = { &_shaders->Hud, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None } };
        Effect<HudShader> HudAdditive = { &_shaders->Hud, { BlendMode::Additive, CullMode::None, DepthMode::None } };
        Effect<FlatShader> Flat = { &_shaders->Flat, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None } };
        Effect<FlatShader> FlatAdditive = { &_shaders->Flat, { BlendMode::Additive, CullMode::CounterClockwise, DepthMode::Read } };
        Effect<FlatShader> EditorSelection = { &_shaders->Flat, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None } };
        Effect<FlatShader> Line = { &_shaders->Flat, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE } };
       
        Effect<SpriteShader> Sprite = { &_shaders->Sprite, { BlendMode::Alpha, CullMode::CounterClockwise, DepthMode::Read } };
        Effect<SpriteShader> SpriteAdditive = { &_shaders->Sprite, { BlendMode::Additive, CullMode::CounterClockwise, DepthMode::Read } };

        void Compile(ID3D12Device* device, uint msaaSamples) {
            auto Reset = [](IShader& shader) {
                shader.PixelShader.Reset();
                shader.VertexShader.Reset();
                shader.RootSignature.Reset();
            };

            CompileShader(&_shaders->Flat);
            CompileShader(&_shaders->Level);
            CompileShader(&_shaders->LevelFlat);
            CompileShader(&_shaders->UserInterface);
            CompileShader(&_shaders->Sprite);
            CompileShader(&_shaders->Object);
            CompileShader(&_shaders->Depth);
            CompileShader(&_shaders->DepthObject);
            CompileShader(&_shaders->DepthCutout);
            CompileShader(&_shaders->Hud);

            auto Compile = [&](auto& effect, uint renderTargets = 1) {
                try {
                    auto psoDesc = BuildPipelineStateDesc(effect.Settings, effect.Shader, msaaSamples, renderTargets);
                    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&effect.PipelineState)));
                }
                catch (const std::exception& e) {
                    SPDLOG_ERROR("Unable to compile shader: {}", e.what());
                }
            };

            Compile(Level);
            Compile(LevelWall);
            Compile(LevelWallAdditive);

            Compile(Depth);
            Compile(DepthObject);
            Compile(DepthCutout);

            Compile(LevelFlat);
            Compile(LevelWallFlat);

            Compile(Object);
            Compile(ObjectGlow);
            Compile(Sprite);
            Compile(SpriteAdditive);

            Compile(Flat);
            Compile(FlatAdditive);
            Compile(EditorSelection);
            Compile(Line);

            Compile(UserInterface);
            Compile(Hud);
            Compile(HudAdditive);
        }
    };
}