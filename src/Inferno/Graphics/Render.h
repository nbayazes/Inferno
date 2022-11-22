#pragma once

#include "DeviceResources.h"
#include "ShaderLibrary.h"
#include "Heap.h"
#include "Camera.h"
#include "PostProcess.h"
#include "MaterialLibrary.h"
#include "LevelMesh.h"
#include "BitmapCache.h"
#include "Mesh.h"
#include "Render.Canvas.h"
#include "Graphics/CommandContext.h"

class CommandListManager;
class ContextManager;

namespace Inferno::Render {
    constexpr DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    // Smart pointers in a namespace makes no sense as they will never trigger
    inline Ptr<DeviceResources> Adapter;
    inline Ptr<ShaderResources> Shaders;
    inline Ptr<EffectResources> Effects;
    inline Ptr<Inferno::PostFx::Bloom> Bloom;
    inline Ptr<DirectX::PrimitiveBatch<ObjectVertex>> g_SpriteBatch;
    inline Ptr<Canvas2D<UIShader>> Canvas, BriefingCanvas;
    inline Ptr<HudCanvas2D> HudCanvas, HudGlowCanvas;

    inline bool DebugEmissive = false;
    inline Ptr<TextureCache> NewTextureCache;

    inline D3D12_GPU_DESCRIPTOR_HANDLE GetTextureSampler() {
        return Settings::Graphics.HighRes ? Heaps->States.AnisotropicWrap() : Heaps->States.PointWrap();
    }

    inline D3D12_GPU_DESCRIPTOR_HANDLE GetClampedTextureSampler() {
        return Settings::Graphics.HighRes ? Heaps->States.AnisotropicClamp() : Heaps->States.PointClamp();
    }

    enum class RenderPass {
        Opaque, // Solid level geometry or objects
        Walls, // Level walls, might be transparent
        Transparent // Sprites, transparent portions of models
    };

    //void DrawVClip(Graphics::GraphicsContext& ctx, const VClip& vclip, const Vector3& position, float radius, const Color& color, float elapsed, bool additive = false, float rotation = 0, const Vector3* up = nullptr);

    void Initialize(HWND hwnd, int width, int height);
    void Resize(int width, int height);
    void Shutdown();
    void Present();

    //void ReloadShaders();
    void ReloadTextures();

    void LoadModelDynamic(ModelID);
    void LoadTextureDynamic(LevelTexID);
    void LoadTextureDynamic(TexID);
    void LoadHUDTextures();
    void LoadTextureDynamic(VClipID);
    void LoadLevel(const Inferno::Level&);

    MeshIndex& GetMeshHandle(ModelID);
    MeshIndex& GetOutrageMeshHandle(ModelID);

    // Locates and loads an OOF by path. Returns -1 if not found.
    ModelID LoadOutrageModel(const string& path);

    inline ID3D12Device* Device;

    // Camera needs to be swappable (pointer).
    // Editor could have multiple cameras (switch between first person and edit)
    // Also need to switch camera for drawing sub-views, like guided missiles or rear view.
    //void SetCamera(Inferno::Camera&);

    inline Inferno::Camera Camera;
    inline Matrix ViewProjection;

    inline float FrameTime = 0; // Time of this frame in seconds
    inline float GameFrameTime = 0; // Time of this frame in seconds. 0 when paused.
    inline double ElapsedTime = 0; // Time elapsed in seconds. Stops updating when paused or animations are disabled.
    inline DirectX::BoundingFrustum CameraFrustum;

    // Returns the squared distance of an object to the camera
    inline float GetRenderDepth(const Vector3& pos) {
        return Vector3::DistanceSquared(Render::Camera.Position, pos);
    }

    void DrawBillboard(Graphics::GraphicsContext& ctx, 
                       TexID tid, 
                       const Vector3& position, 
                       float radius,
                       const Color& color, 
                       bool additive,
                       float rotation,
                       const Vector3* up);

    extern bool LevelChanged;
    void DrawLevel(Graphics::GraphicsContext& ctx, Level& level);
    PackedBuffer* GetLevelMeshBuffer();

    //const string TEST_MODEL = "robottesttube(orbot).OOF"; // mixed transparency test
    const string TEST_MODEL = "gyro.OOF";

    namespace Stats {
        inline uint16 VisitedSegments = 0;
        inline uint16 DrawCalls = 0;
        inline uint16 PolygonCount = 0;
    }
}
