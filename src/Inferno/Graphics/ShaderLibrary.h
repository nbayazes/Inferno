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
        string VSEntryPoint;
        string PSEntryPoint;
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
            Constant,
            RootParameterCount
        };
    public:
        struct Constants {
            Matrix WVP;
            Vector3 Eye;
        };

        FlatLevelShader(ShaderInfo info) : IShader(info) {
            InputLayout = LevelVertex::Layout;
        }

        void SetConstants(ID3D12GraphicsCommandList* commandList, const Constants& consts) {
            commandList->SetGraphicsRoot32BitConstants(Constant, sizeof(consts) / 4, &consts, 0);
        }
    };

    class LevelShader : public IShader {
        enum RootParameterIndex : uint {
            Constant,
            InstanceConstant,
            Material1,
            Material2,
            Depth,
            Sampler,
            RootParameterCount
        };
    public:
        struct Constants {
            Matrix WVP;
            Vector3 Eye;
            float _pad;
            Vector3 LightDirection;
            float _pad2;
            Vector2 FrameSize;
        };

        struct InstanceConstants {
            float Time, FrameTime;
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

        void SetConstants(ID3D12GraphicsCommandList* commandList, const Constants& consts) {
            commandList->SetGraphicsRoot32BitConstants(Constant, sizeof(consts) / 4, &consts, 0);
        }

        void SetInstanceConstants(ID3D12GraphicsCommandList* commandList, const InstanceConstants& consts) {
            commandList->SetGraphicsRoot32BitConstants(InstanceConstant, sizeof(consts) / 4, &consts, 0);
        }
    };

    class SpriteShader : public IShader {
        enum RootParameterIndex : uint {
            ConstantBuffer,
            Diffuse,
            LinearZ,
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

        void SetLinearZ(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(LinearZ, texture);
        }

        void SetWorldViewProjection(ID3D12GraphicsCommandList* commandList, const Matrix& wvp) {
            commandList->SetGraphicsRoot32BitConstants(ConstantBuffer, sizeof(wvp) / 4, &wvp.m, 0);
        }
    };

    class EmissiveShader : public IShader {
        enum RootParameterIndex : uint {
            ConstantBuffer,
            Diffuse,
            Emissive,
            Sampler,
            RootParameterCount
        };
    public:
        EmissiveShader(ShaderInfo info) : IShader(info) {
            InputLayout = LevelVertex::Layout;
        }

        void SetSampler(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE sampler) {
            commandList->SetGraphicsRootDescriptorTable(Sampler, sampler);
        }

        void SetDiffuse(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(Diffuse, texture);
        }

        void SetEmissive(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(Emissive, texture);
        }

        void SetWorldViewProjection(ID3D12GraphicsCommandList* commandList, const Matrix& wvp) {
            commandList->SetGraphicsRoot32BitConstants(ConstantBuffer, sizeof(wvp) / 4, &wvp.m, 0);
        }
    };

    class ObjectShader : public IShader {
        enum RootParameterIndex : uint {
            ConstantBuffer,
            Material,
            Sampler,
            RootParameterCount
        };
    public:
        struct Constants {
            DirectX::XMMATRIX World;
            DirectX::XMMATRIX Projection;
            DirectX::XMVECTOR LightDirection[3];
            Color Colors[3];
            DirectX::XMVECTOR Eye;
            //float Time;
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
            commandList->SetGraphicsRoot32BitConstants(ConstantBuffer, sizeof(consts) / 4, &consts, 0);
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
            DirectX::SimpleMath::Matrix Transform = Matrix::Identity;
            DirectX::SimpleMath::Color Tint = { 1, 1, 1, 1 };
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
        }

        void SetDiffuse(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE texture) {
            commandList->SetGraphicsRootDescriptorTable(Diffuse, texture);
        }

        void SetWorldViewProjection(ID3D12GraphicsCommandList* commandList, const Matrix& wvp) {
            commandList->SetGraphicsRoot32BitConstants(Constants, sizeof(wvp) / 4, &wvp.m, 0);
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

    D3D12_GRAPHICS_PIPELINE_STATE_DESC BuildPipelineStateDesc(EffectSettings effect, IShader* shader, DXGI_FORMAT format, uint msaaSamples, uint renderTargets = 1);

    struct ShaderResources {
        LevelShader Level = ShaderInfo{ L"shaders/level.hlsl", "VSLevel", "PSLevel" };
        FlatLevelShader LevelFlat = ShaderInfo{ L"shaders/levelflat.hlsl", "VSLevel", "PSLevel" };
        FlatShader Flat = ShaderInfo{ L"shaders/editor.hlsl", "VSFlat", "PSFlat" };
        UIShader UserInterface = ShaderInfo{ L"shaders/imgui.hlsl", "VSMain", "PSMain" };
        SpriteShader Sprite = ShaderInfo{ L"shaders/sprite.hlsl", "VSMain", "PSMain" };
        ObjectShader Object = ShaderInfo{ L"shaders/object.hlsl", "VSMain", "PSMain" };
    };

    class EffectResources {
        ShaderResources* _shaders;
    public:
        EffectResources(ShaderResources* shaders) : _shaders(shaders) {}

        Effect<LevelShader> Level = { &_shaders->Level, { BlendMode::Opaque, CullMode::CounterClockwise, DepthMode::Read } };
        Effect<LevelShader> LevelWall = { &_shaders->Level, { BlendMode::Alpha, CullMode::CounterClockwise, DepthMode::Read } };
        Effect<LevelShader> LevelWallAdditive = { &_shaders->Level, { BlendMode::Additive, CullMode::CounterClockwise, DepthMode::Read } };
        Effect<FlatLevelShader> LevelFlat = { &_shaders->LevelFlat };
        Effect<FlatLevelShader> LevelWallFlat = { &_shaders->LevelFlat, { BlendMode::Alpha } };
        Effect<ObjectShader> Object = { &_shaders->Object, { BlendMode::Alpha } };
        Effect<ObjectShader> ObjectGlow = { &_shaders->Object, { BlendMode::Additive, CullMode::None, DepthMode::Read } };
        Effect<UIShader> UserInterface = { &_shaders->UserInterface, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, false } };
        Effect<FlatShader> Flat = { &_shaders->Flat, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None } };
        Effect<FlatShader> FlatAdditive = { &_shaders->Flat, { BlendMode::Additive, CullMode::CounterClockwise, DepthMode::Read } };
        Effect<FlatShader> EditorSelection = { &_shaders->Flat, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None } };
        Effect<FlatShader> Line = { &_shaders->Flat, { BlendMode::StraightAlpha, CullMode::None, DepthMode::None, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE } };
        Effect<SpriteShader> Sprite = { &_shaders->Sprite, { BlendMode::Alpha, CullMode::CounterClockwise, DepthMode::Read } };
        Effect<SpriteShader> SpriteAdditive = { &_shaders->Sprite, { BlendMode::Additive, CullMode::CounterClockwise, DepthMode::Read } };

        void Compile(ID3D12Device* device, DXGI_FORMAT format, uint msaaSamples) {
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

            auto Compile = [&](auto& effect, uint renderTargets = 1, DXGI_FORMAT* formatOverride = nullptr) {
                try {
                    auto fmt = formatOverride ? *formatOverride : format;
                    auto psoDesc = BuildPipelineStateDesc(effect.Settings, effect.Shader, fmt, msaaSamples, renderTargets);
                    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&effect.PipelineState)));
                }
                catch (const std::exception& e) {
                    SPDLOG_ERROR("Unable to compile shader: {}", e.what());
                }
            };

            Compile(Level);
            Compile(LevelWall);
            Compile(LevelWallAdditive);

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

            auto backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // todo: pass this as a parameter. The debug UI is rendered on top of the scene.
            Compile(UserInterface, 1, &backBufferFormat);

            //msaaSamples = 1;
            //Compile(Emissive);
            //Compile(EmissiveWall);
        }
    };
}