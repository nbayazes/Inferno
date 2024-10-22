#pragma once

#include "DeviceResources.h"
#include "ShaderLibrary.h"
#include "Heap.h"
#include "Camera.h"
#include "PostProcess.h"
#include "BitmapCache.h"
#include "Mesh.h"
#include "Render.Canvas.h"
#include "Lighting.h"

class CommandListManager;
class ContextManager;

namespace Inferno::Render {
    constexpr DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    // Smart pointers in a namespace makes no sense as they will never trigger
    inline Ptr<DeviceResources> Adapter;
    inline Ptr<ShaderResources> Shaders;
    inline Ptr<EffectResources> Effects;
    inline Ptr<Inferno::PostFx::ToneMapping> ToneMapping;
    inline Ptr<Inferno::PostFx::ScanlineCS> Scanline;
    inline Ptr<DirectX::PrimitiveBatch<ObjectVertex>> g_SpriteBatch;
    inline Ptr<Canvas2D<UIShader>> Canvas, DebugCanvas;
    inline Ptr<Canvas2D<BriefingShader>> BriefingCanvas;

    inline Ptr<HudCanvas2D> HudCanvas, HudGlowCanvas;
    inline Ptr<Graphics::FillLightGridCS> LightGrid;

    inline Ptr<StructuredBuffer> MaterialInfoBuffer;
    inline Ptr<StructuredBuffer> VClipBuffer;

    inline bool TerrainChanged = false;
    inline bool DebugEmissive = false;
    inline Ptr<TextureCache> NewTextureCache;

    inline D3D12_GPU_DESCRIPTOR_HANDLE GetWrappedTextureSampler() {
        return Settings::Graphics.FilterMode == TextureFilterMode::Point ? Heaps->States.PointWrap() : Heaps->States.AnisotropicWrap();
    }

    inline D3D12_GPU_DESCRIPTOR_HANDLE GetNormalSampler() {
        return Settings::Graphics.FilterMode == TextureFilterMode::Smooth ? Heaps->States.AnisotropicWrap() : Heaps->States.PointWrap();
    }

    inline D3D12_GPU_DESCRIPTOR_HANDLE GetClampedTextureSampler() {
        return Settings::Graphics.FilterMode == TextureFilterMode::Point ? Heaps->States.PointClamp() : Heaps->States.AnisotropicClamp();
    }

    enum class RenderPass {
        Opaque, // Solid level geometry or objects
        Decals, // Solid level geometry decals
        Walls, // Level walls, might be transparent
        Transparent, // Sprites, transparent portions of models
        Distortion, // Cloaked enemies, shockwaves
    };

    //void DrawVClip(GraphicsContext& ctx, const VClip& vclip, const Vector3& position, float radius, const Color& color, float elapsed, bool additive = false, float rotation = 0, const Vector3* up = nullptr);

    void Initialize(HWND hwnd, int width, int height);
    void Resize(int width, int height);
    void Shutdown();
    void Present(const Camera& camera);

    //void RenderProbe(const Vector3& position);

    void LoadLevel(const Inferno::Level&);

    MeshIndex& GetMeshHandle(ModelID);
    MeshIndex& GetOutrageMeshHandle(ModelID);
    void UpdateFrameConstants(const Inferno::Camera& camera, UploadBuffer<FrameConstants>& dest, float renderScale = 1);

    inline ID3D12Device* Device;

    // Camera needs to be swappable (pointer).
    // Editor could have multiple cameras (switch between first person and edit)
    // Also need to switch camera for drawing sub-views, like guided missiles or rear view.
    //void SetCamera(Inferno::Camera&);

    inline float FrameTime = 0; // Time of this frame in seconds

    // Returns the squared distance of an object to the camera
    inline float GetRenderDepth(const Vector3& pos, const Camera& camera) {
        return Vector3::DistanceSquared(camera.Position, pos);
    }

    struct BillboardInfo {
        float Ratio;
        float Radius;
        Color Color;
        bool Additive = false;
        float Rotation = 0;
        const Vector3* Up = nullptr;
        bool Terrain = false;
    };

    void DrawBillboard(GraphicsContext& ctx,
                       D3D12_GPU_DESCRIPTOR_HANDLE texture,
                       D3D12_GPU_VIRTUAL_ADDRESS frameConstants,
                       Inferno::Camera& camera,
                       const Vector3& position,
                       BillboardInfo& info);

    void DrawBillboard(GraphicsContext& ctx,
                       TexID tid,
                       const Vector3& position,
                       BillboardInfo& info);

    // Call ApplyEffect and SetConstantBuffer first
    void DrawDepthBillboard(GraphicsContext& ctx,
                            TexID tid,
                            const Vector3& position,
                            float radius,
                            float rotation,
                            const Vector3* up);

    extern bool LevelChanged;
    //PackedBuffer* GetLevelMeshBuffer();
    //const TerrainMesh* GetTerrainMesh();

    struct PackedMesh {
        D3D12_VERTEX_BUFFER_VIEW VertexBuffer;
        D3D12_INDEX_BUFFER_VIEW IndexBuffer;
        uint IndexCount;

        bool IsValid() const {
            return VertexBuffer.BufferLocation && IndexBuffer.BufferLocation && IndexCount > 0;
        }

        void Draw(ID3D12GraphicsCommandList* cmdList) const {
            if (!IsValid()) return;
            cmdList->IASetVertexBuffers(0, 1, &VertexBuffer);
            cmdList->IASetIndexBuffer(&IndexBuffer);
            cmdList->DrawIndexedInstanced(IndexCount, 1, 0, 0, 0);
            //Render::Stats::DrawCalls++;
        }
    };

    enum class AutomapType {
        Normal,
        Unrevealed,
        FullMap,
        Door,
        LockedDoor,
        BlueDoor,
        GoldDoor,
        RedDoor,
        Fuelcen,
        Matcen,
        Reactor,
    };

    struct AutomapMeshInstance {
        TexID Texture = TexID::None, Decal = TexID::None;
        PackedMesh Mesh;
        AutomapType Type = AutomapType::Normal;
    };

    struct AutomapMeshes {
        PackedBuffer Buffer;

        List<AutomapMeshInstance> Walls;
        List<AutomapMeshInstance> FullmapWalls;
        List<AutomapMeshInstance> TransparentWalls;
    };


    struct LevelGpuResources {
        Ptr<TerrainMesh> TerrainMesh;
        Ptr<PackedBuffer> LevelMeshes;
        Ptr<MeshBuffer> ObjectMeshes;
        Ptr<AutomapMeshes> AutomapMeshes;
    };

    struct ModelMesh {
        List<ObjectVertex> Vertices;
        List<uint16> Indices;
    };

    struct GenericMeshes {
        PackedBuffer Buffer{ 5 * 1024 * 1024 };
        List<PackedMesh> Meshes;

        uint AddMesh(ModelMesh& mesh) {
            auto packed = Render::PackedMesh{
                .VertexBuffer = Buffer.PackVertices(span{ mesh.Vertices }),
                .IndexBuffer = Buffer.PackIndices(span{ mesh.Indices }),
                .IndexCount = (uint)mesh.Indices.size()
            };

            Meshes.push_back(packed);
            return uint(Meshes.size() - 1);
        }
    };

    inline LevelGpuResources LevelResources;
    inline Ptr<GenericMeshes> GlobalMeshes; // Globally loaded object meshes. e.g. for the main menu or cutscenes.

    //const string TEST_MODEL = "robottesttube(orbot).OOF"; // mixed transparency test
    const string TEST_MODEL = "gyro.OOF";

    // returns a vector perpendicular to the camera and the start/end points
    inline Vector3 GetBeamNormal(const Vector3& start, const Vector3 end, const Camera& camera) {
        auto tangent = start - end;
        auto dirToBeam = start - camera.Position;
        auto normal = dirToBeam.Cross(tangent);
        normal.Normalize();
        return normal;
    }

    //void SetCamera(Camera& camera);

    namespace Stats {
        inline uint16 DrawCalls = 0;
        inline uint16 PolygonCount = 0;
    }
}
