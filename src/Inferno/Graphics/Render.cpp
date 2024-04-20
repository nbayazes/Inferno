#include "pch.h"
#include "Render.h"
#include "imgui_local.h"
#include "Level.h"
#include "Editor/Editor.h"
#include "Buffers.h"
#include "Utility.h"
#include "Mesh.h"
#include "Render.Gizmo.h"
#include "Render.Debug.h"
#include "Render.Editor.h"
#include "Settings.h"
#include "DirectX.h"
#include "Render.Particles.h"
#include "Game.Text.h"
#include "Editor/UI/BriefingEditor.h"
#include "HUD.h"
#include "ScopedTimer.h"
#include "LegitProfiler.h"
#include "MaterialLibrary.h"
#include "Procedural.h"
#include "Render.Level.h"
#include "Resources.h"

using namespace DirectX;
using namespace Inferno::Graphics;

namespace Inferno::Render {
    using VertexType = DirectX::VertexPositionTexture;

    Color ClearColor = { 0.1f, 0.1f, 0.1f, 1.0f };
    bool LevelChanged = false;
    constexpr uint MATERIAL_COUNT = 4000;
    constexpr uint VCLIP_COUNT = 150;

    struct GpuVClip {
        float PlayTime; // total time (in seconds) of clip
        int NumFrames; // Valid frames in Frames
        float FrameTime; // time (in seconds) of each frame
        int Pad;
        int Frames[30];
        int Pad1, Pad2;
    };

    static_assert(sizeof(GpuVClip) % 16 == 0);

    namespace {
        HWND _hwnd;

        // todo: put all of these resources into a class and use RAII
        Ptr<GraphicsMemory> _graphicsMemory;

        Ptr<MeshBuffer> _meshBuffer;
        Ptr<TerrainMesh> _terrainMesh;
        Ptr<SpriteBatch> _postBatch;
        Ptr<PackedBuffer> _levelMeshBuffer;

        Ptr<UploadBuffer<MaterialInfo>> MaterialInfoUploadBuffer;
        Ptr<UploadBuffer<GpuVClip>> VClipUploadBuffer;
        Ptr<FrameUploadBuffer> FrameUploadBuffers[2];

        //Inferno::Camera DEFAULT_CAMERA;
        //Inferno::Camera* pCam = &DEFAULT_CAMERA;
    }

    PackedBuffer* GetLevelMeshBuffer() { return _levelMeshBuffer.get(); }
    const TerrainMesh* GetTerrainMesh() { return _terrainMesh.get(); }

    //void SetCamera(Inferno::Camera& camera) {
    //    pCam = &camera;
    //}

    void DrawBillboard(GraphicsContext& ctx,
                       float ratio,
                       D3D12_GPU_DESCRIPTOR_HANDLE texture,
                       D3D12_GPU_VIRTUAL_ADDRESS frameConstants,
                       Inferno::Camera& camera,
                       const Vector3& position,
                       float radius,
                       const Color& color,
                       bool additive,
                       float rotation,
                       const Vector3* up) {
        auto transform = up ? Matrix::CreateConstrainedBillboard(position, camera.Position, *up) : Matrix::CreateBillboard(position, camera.Position, camera.Up);

        if (rotation != 0)
            transform = Matrix::CreateRotationZ(rotation) * transform;

        // create quad and transform it
        auto h = radius * ratio;
        auto w = radius;
        auto p0 = Vector3::Transform({ -w, h, 0 }, transform); // bl
        auto p1 = Vector3::Transform({ w, h, 0 }, transform); // br
        auto p2 = Vector3::Transform({ w, -h, 0 }, transform); // tr
        auto p3 = Vector3::Transform({ -w, -h, 0 }, transform); // tl

        ObjectVertex v0(p0, { 0, 0 }, color);
        ObjectVertex v1(p1, { 1, 0 }, color);
        ObjectVertex v2(p2, { 1, 1 }, color);
        ObjectVertex v3(p3, { 0, 1 }, color);

        auto& effect = additive ? Effects->SpriteAdditive : Effects->Sprite;
        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, frameConstants);
        effect.Shader->SetDiffuse(ctx.GetCommandList(), texture);
        effect.Shader->SetDepthTexture(ctx.GetCommandList(), Adapter->LinearizedDepthBuffer.GetSRV());
        auto sampler = Render::GetClampedTextureSampler();
        effect.Shader->SetSampler(ctx.GetCommandList(), sampler);

        // todo: replace horrible code with proper batching
        Stats::DrawCalls++;
        g_SpriteBatch->Begin(ctx.GetCommandList());
        g_SpriteBatch->DrawQuad(v0, v1, v2, v3);
        g_SpriteBatch->End();
    }

    void DrawBillboard(GraphicsContext& ctx,
                       TexID tid,
                       const Vector3& position,
                       float radius,
                       const Color& color,
                       bool additive,
                       float rotation,
                       const Vector3* up) {
        auto& ti = Resources::GetTextureInfo(tid);
        auto ratio = (float)ti.Height / (float)ti.Width;
        auto& material = Materials->Get(tid);

        DrawBillboard(ctx, ratio, material.Handle(), Adapter->GetFrameConstants().GetGPUVirtualAddress(), ctx.Camera, position, radius, color, additive, rotation, up);
    }

    void DrawDepthBillboard(GraphicsContext& ctx,
                            TexID tid,
                            const Vector3& position,
                            float radius,
                            float rotation,
                            const Vector3* up) {
        auto transform = up
            ? Matrix::CreateConstrainedBillboard(position, ctx.Camera.Position, *up)
            : Matrix::CreateBillboard(position, ctx.Camera.Position, ctx.Camera.Up);

        if (rotation != 0)
            transform = Matrix::CreateRotationZ(rotation) * transform;

        // create quad and transform it
        auto& ti = Resources::GetTextureInfo(tid);
        auto ratio = (float)ti.Height / (float)ti.Width;
        auto h = radius * ratio;
        auto w = radius;
        auto p0 = Vector3::Transform({ -w, h, 0 }, transform); // bl
        auto p1 = Vector3::Transform({ w, h, 0 }, transform); // br
        auto p2 = Vector3::Transform({ w, -h, 0 }, transform); // tr
        auto p3 = Vector3::Transform({ -w, -h, 0 }, transform); // tl

        Color color;
        ObjectVertex v0(p0, { 0, 0 }, color, {}, {}, {}, (int)tid);
        ObjectVertex v1(p1, { 1, 0 }, color, {}, {}, {}, (int)tid);
        ObjectVertex v2(p2, { 1, 1 }, color, {}, {}, {}, (int)tid);
        ObjectVertex v3(p3, { 0, 1 }, color, {}, {}, {}, (int)tid);

        // todo: replace horrible code with proper batching
        Stats::DrawCalls++;
        g_SpriteBatch->Begin(ctx.GetCommandList());
        g_SpriteBatch->DrawQuad(v0, v1, v2, v3);
        g_SpriteBatch->End();
    }

    void CreateDefaultTextures() {
        auto batch = BeginTextureUpload();
        uint normalData[] = { 0x00FF8080, 0x00FF8080, 0x00FF8080, 0x00FF8080 };
        StaticTextures->Normal.Load(batch, normalData, 2, 2, L"normal", false, DXGI_FORMAT_R8G8B8A8_UNORM);
        StaticTextures->Normal.AddShaderResourceView();

        uint whiteData[] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
        StaticTextures->White.Load(batch, whiteData, 2, 2, L"white", false, DXGI_FORMAT_R8G8B8A8_UNORM);
        StaticTextures->White.AddShaderResourceView();

        uint blackData[] = { 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000 };
        StaticTextures->Black.Load(batch, blackData, 2, 2, L"black", false, DXGI_FORMAT_R8G8B8A8_UNORM);
        StaticTextures->Black.AddShaderResourceView();

        uint missingData[] = { 0xFFFF00FF, 0xFF000000, 0xFF000000, 0xFFFF00FF };
        StaticTextures->Missing.Load(batch, missingData, 2, 2, L"missing", false, DXGI_FORMAT_R8G8B8A8_UNORM);
        StaticTextures->Missing.AddShaderResourceView();

        try {
            if (!filesystem::exists("tony_mc_mapface.dds")) {
                SPDLOG_ERROR("tony_mc_mapface.dds not found");
            }
            else {
                ToneMapping->LoadResources(batch);
            }
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR(e.what());
        }
        EndTextureUpload(batch, Render::Adapter->BatchUploadQueue->Get());
    }

    // Initialize device dependent objects here (independent of window size).
    void CreateDeviceDependentResources() {
        Shaders = make_unique<ShaderResources>();
        Effects = make_unique<EffectResources>(Shaders.get());
        ToneMapping = make_unique<PostFx::ToneMapping>();
        MaterialInfoUploadBuffer = make_unique<UploadBuffer<MaterialInfo>>(MATERIAL_COUNT, L"Material upload buffer");
        MaterialInfoBuffer = make_unique<StructuredBuffer>();
        MaterialInfoBuffer->Create(L"MaterialInfo", sizeof MaterialInfo, MATERIAL_COUNT);
        MaterialInfoBuffer->AddShaderResourceView();

        VClipUploadBuffer = make_unique<UploadBuffer<GpuVClip>>(VCLIP_COUNT, L"vclip buffer");
        VClipBuffer = make_unique<StructuredBuffer>();
        VClipBuffer->Create(L"VClips", sizeof GpuVClip, VCLIP_COUNT);
        VClipBuffer->AddShaderResourceView();

        for (auto& buffer : FrameUploadBuffers)
            buffer = make_unique<FrameUploadBuffer>(1024 * 1024 * 10);

        //Materials2 = MakePtr<MaterialLibrary2>(Device, 64 * 64 * 4 * 1000);
        g_SpriteBatch = make_unique<PrimitiveBatch<ObjectVertex>>(Device);
        Canvas = make_unique<Canvas2D<UIShader>>(Device, Effects->UserInterface);
        DebugCanvas = make_unique<Canvas2D<UIShader>>(Device, Effects->UserInterface);
        BriefingCanvas = make_unique<Canvas2D<BriefingShader>>(Device, Effects->Briefing);

        HudCanvas = make_unique<HudCanvas2D>(Device, Effects->Hud);
        HudGlowCanvas = make_unique<HudCanvas2D>(Device, Effects->HudAdditive);
        _graphicsMemory = make_unique<GraphicsMemory>(Device);
        LightGrid = make_unique<FillLightGridCS>();
        //LightGrid->Load(L"shaders/FillLightGridCS.hlsl");
        //NewTextureCache = MakePtr<TextureCache>();

        CreateDefaultTextures();

        Materials = make_unique<MaterialLibrary>(MATERIAL_COUNT);
        Debug::Initialize();

        InitializeImGui(_hwnd, (float)Settings::Editor.FontSize);
        static_assert(sizeof(ImTextureID) >= sizeof(D3D12_CPU_DESCRIPTOR_HANDLE), "D3D12_CPU_DESCRIPTOR_HANDLE is too large to fit in an ImTextureID");
        g_ImGuiBatch = make_unique<ImGuiBatch>(Adapter->GetBackBufferCount());

        CreateEditorResources();
        LoadFonts();

        ResourceUploadBatch resourceUpload(Device);

        resourceUpload.Begin();

        {
            RenderTargetState rtState(Adapter->GetBackBufferFormat(), Adapter->SceneDepthBuffer.GetFormat());
            SpriteBatchPipelineStateDescription pd(rtState);
            pd.samplerDescriptor = Heaps->States.PointClamp();
            _postBatch = make_unique<SpriteBatch>(Device, resourceUpload, pd);
        }

        auto task = resourceUpload.End(Adapter->GetCommandQueue());
        task.wait();
    }

    void CreateWindowSizeDependentResources(int width, int height) {
        ToneMapping->Create(width, height);
        LightGrid->CreateBuffers(width, height);
    }

    void Initialize(HWND hwnd, int width, int height) {
        assert(hwnd);
        _hwnd = hwnd;
        Adapter = make_unique<DeviceResources>(BackBufferFormat);
        StaticTextures = make_unique<StaticTextureDef>();
        Adapter->SetWindow(hwnd, width, height);
        Adapter->CreateDeviceResources();

        Adapter->CreateWindowSizeDependentResources();
        CreateDeviceDependentResources();
        Adapter->ReloadResources();

        CreateWindowSizeDependentResources(width, height);
        Editor::EditorCamera.SetViewport(Vector2((float)width, (float)height));
        Game::GameCamera.SetViewport(Vector2((float)width, (float)height));

        _levelMeshBuffer = make_unique<PackedBuffer>(1024 * 1024 * 20);

        Editor::Events::LevelChanged += [] { LevelChanged = true; };
        Editor::Events::TexturesChanged += [] {
            //PendingTextures.push_back(id);
            Materials->LoadLevelTextures(Game::Level, false);
        };

        StartProceduralWorker();
    }

    void Shutdown() {
        if (Adapter)
            Adapter->WaitForGpu();

        Materials->Shutdown(); // wait for thread to terminate
        Materials.reset();
        //NewTextureCache.reset();
        Render::Heaps.reset();
        Render::UploadHeap.reset();
        StaticTextures.reset();
        Effects.reset();
        Shaders.reset();
        Canvas.reset();
        DebugCanvas.reset();
        BriefingCanvas.reset();
        HudCanvas.reset();
        HudGlowCanvas.reset();
        _graphicsMemory.reset();
        g_SpriteBatch.reset();
        g_ImGuiBatch.reset();
        MaterialInfoBuffer.reset();
        MaterialInfoUploadBuffer.reset();
        VClipUploadBuffer.reset();
        VClipBuffer.reset();
        for (auto& buffer : FrameUploadBuffers)
            buffer.reset();

        ReleaseEditorResources();
        StopProceduralWorker();
        _levelMeshBuffer.reset();
        _meshBuffer.reset();
        _terrainMesh.reset();

        Adapter.reset();
        ToneMapping.reset();
        LightGrid.reset();
        _postBatch.reset();
        Debug::Shutdown();
        Device = nullptr;
        ReportLiveObjects();
    }

    void Resize(int width, int height) {
        //SPDLOG_INFO("Resize: {} {}", width, height);

        if (!Adapter->WindowSizeChanged(width, height))
            return;

        CreateWindowSizeDependentResources(width, height);
        Editor::EditorCamera.SetViewport(Vector2((float)width, (float)height));
        Game::GameCamera.SetViewport(Vector2((float)width, (float)height));
        //pCam->SetViewport((float)width, (float)height);
        // Reset frame upload buffers, otherwise they run out of memory.
        // For some reason resizing does not increment the adapter frame index, causing the same buffer to be used.
        FrameUploadBuffers[0]->ResetIndex();
        FrameUploadBuffers[1]->ResetIndex();
    }

    // Loads a single model at runtime
    void LoadModelDynamic(ModelID id) {
        if (!_meshBuffer) return;
        _meshBuffer->LoadModel(id);
        Set<TexID> ids;
        GetTexturesForModel(id, ids);
        auto tids = Seq::ofSet(ids);
        Materials->LoadMaterials(tids, false);
    }

    void LoadTextureDynamic(LevelTexID id) {
        List<TexID> list = { Resources::LookupTexID(id) };
        auto& eclip = Resources::GetEffectClip(id);
        Seq::append(list, eclip.VClip.GetFrames());
        Materials->LoadMaterials(list, false);
    }

    void LoadTextureDynamic(TexID id) {
        if (id <= TexID::None) return;
        List<TexID> list{ id };
        auto& eclip = Resources::GetEffectClip(id);
        Seq::append(list, eclip.VClip.GetFrames());
        Materials->LoadMaterials(list, false);
    }

    void LoadTextureDynamic(VClipID id) {
        auto& vclip = Resources::GetVideoClip(id);
        Materials->LoadMaterials(vclip.GetFrames(), false);
    }

    ModelID LoadOutrageModel(const string& path) {
        auto id = Resources::LoadOutrageModel(path);
        if (auto model = Resources::GetOutrageModel(id)) {
            _meshBuffer->LoadOutrageModel(*model, id);
            Materials->LoadTextures(model->Textures);

            //Materials->LoadOutrageModel(*model);
            //NewTextureCache->MakeResident();
        }

        return id;
    }

    void LoadVClips(ID3D12GraphicsCommandList* cmdList) {
        List<GpuVClip> vclips(VCLIP_COUNT);

        //tid = mesh->EffectClip == EClipID::None ? mesh->Texture : Resources::GetEffectClip(mesh->EffectClip).VClip.GetFrame(ElapsedTime + vclipOffset);

        // Flatten the embedded effect vclips that objects can use
        for (int i = 0; i < Resources::GameData.Effects.size(); i++) {
            auto& src = Resources::GameData.Effects[i].VClip;
            vclips[i].FrameTime = src.FrameTime;
            vclips[i].NumFrames = src.NumFrames;
            vclips[i].PlayTime = src.PlayTime;
            for (int j = 0; j < src.Frames.size(); j++)
                vclips[i].Frames[j] = (int)src.Frames[j];
        }

        VClipUploadBuffer->Begin();
        VClipUploadBuffer->Copy(vclips);
        VClipUploadBuffer->End();

        VClipBuffer->Transition(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->CopyResource(VClipBuffer->Get(), VClipUploadBuffer->Get());
        VClipBuffer->Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void LoadLevel(const Level& level) {
        Adapter->WaitForGpu();

        SPDLOG_INFO("Load models");
        // Load models for objects in the level
        constexpr int DESCENT3_MODEL_COUNT = 200;
        _meshBuffer = MakePtr<MeshBuffer>(Resources::GameData.Models.size(), DESCENT3_MODEL_COUNT);
        _terrainMesh = {};

        List<ModelID> modelIds;
        for (auto& obj : level.Objects) {
            if (obj.Render.Type == RenderType::Model) {
                _meshBuffer->LoadModel(obj.Render.Model.ID);
                _meshBuffer->LoadModel(Resources::GetDeadModelID(obj.Render.Model.ID));
                _meshBuffer->LoadModel(Resources::GetDyingModelID(obj.Render.Model.ID));
            }
        }

        //{
        //    LoadOutrageModel(TEST_MODEL);
        //}

        Graphics::Lights = {};
        ResetEffects();
        LevelChanged = true;
    }

    void LoadTerrain(const TerrainInfo& info) {
        std::array textures = { info.SatelliteTexture, info.SurfaceTexture };
        Render::Materials->LoadTextures(textures);
        _terrainMesh = make_unique<TerrainMesh>();
        _terrainMesh->AddTerrain(info.Vertices, info.Indices, info.SurfaceTexture);

        {
            //Matrix::CreateFromAxisAngle(Vector3::UnitY, info.SatelliteDir);
            auto satPosition = info.SatelliteDir * 1000 + Vector3(0, info.SatelliteHeight, 0);

            //const float planetRadius = 200; // Add a planet radius
            auto normal = /*Vector3(0, planetRadius, 0)*/ -satPosition;
            normal.Normalize();
            auto tangent = normal.Cross(Vector3::UnitY);
            tangent.Normalize();
            auto bitangent = tangent.Cross(normal);
            tangent = bitangent.Cross(normal);

            List<ObjectVertex> satVerts;

            auto addVertex = [&](const Vector3& position, const Vector2& uv) {
                ObjectVertex vertex{
                    .Position = position,
                    .UV = uv,
                    .Color = info.SatelliteColor,
                    .Normal = normal,
                    .Tangent = tangent,
                    .Bitangent = bitangent,
                    .TexID = (int)TexID::None // Rely on override
                };

                satVerts.push_back(vertex);
            };

            //auto delta = Vector3(-info.SatelliteSize, -info.SatelliteSize, 0);
            //auto transform = Matrix::CreateRotationZ(DirectX::XM_PIDIV2);
            //auto transform = Matrix::CreateFromAxisAngle(normal, DirectX::XM_PIDIV2);
            auto radius = info.SatelliteSize;
            auto ratio = info.SatelliteAspectRatio;

            addVertex(satPosition - tangent * radius - bitangent * radius * ratio, Vector2(1, 1)); // bl
            addVertex(satPosition + tangent * radius - bitangent * radius * ratio, Vector2(0, 1)); // br
            addVertex(satPosition + tangent * radius + bitangent * radius * ratio, Vector2(0, 0)); // tr
            addVertex(satPosition - tangent * radius + bitangent * radius * ratio, Vector2(1, 0)); // tl

            List<uint16> satIndices = { 0, 1, 2, 0, 2, 3 };

            _terrainMesh->AddSatellite(satVerts, satIndices, info.SatelliteTexture);

            //for (int i = 0; i < 4; i++) {
            //    ObjectVertex vertex{
            //        .Position = satPosition + delta,
            //        .UV = Vector2(0, 0),
            //        .Color = Color(1, 1, 1),
            //        .Normal = normal,
            //        .Tangent = tangent,
            //        .Bitangent = bitangent,
            //        .TexID = (int)TexID::None // Rely on override
            //    };

            //    vertices.push_back(vertex);

            //    delta = delta.Transform(delta, transform);
            //}
        }
    }

    MeshIndex& GetMeshHandle(ModelID id) {
        return _meshBuffer->GetHandle(id);
    }

    MeshIndex& GetOutrageMeshHandle(ModelID id) {
        return _meshBuffer->GetOutrageHandle(id);
    }

    void PostProcess(GraphicsContext& ctx) {
        PIXScopedEvent(ctx.GetCommandList(), PIX_COLOR_INDEX(8), "Post");
        // Post process
        auto backBuffer = Adapter->GetBackBuffer();
        ctx.ClearColor(*backBuffer);
        ctx.SetRenderTarget(backBuffer->GetRTV());
        ctx.SetViewportAndScissor((UINT)backBuffer->GetWidth(), (UINT)backBuffer->GetHeight());

        auto cmdList = ctx.GetCommandList();
        ToneMapping->Apply(cmdList, Adapter->SceneColorBuffer);
        Adapter->SceneColorBuffer.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        // draw to backbuffer using a shader + polygon
        _postBatch->SetViewport(Adapter->GetScreenViewport());
        _postBatch->Begin(cmdList);
        auto size = Adapter->GetOutputSize();
        _postBatch->Draw(Adapter->SceneColorBuffer.GetSRV(), XMUINT2{ (uint)(size.x / RenderScale), (uint)(size.y / RenderScale) }, XMFLOAT2{ 0, 0 });
        _postBatch->End();
    }

    void DrawUI(GraphicsContext& ctx) {
        PIXScopedEvent(ctx.GetCommandList(), PIX_COLOR_INDEX(9), "UI");
        ScopedTimer imguiTimer(&Metrics::ImGui);
        Canvas->Render(ctx);
        // Imgui batch modifies render state greatly. Normal geometry will likely not render correctly afterwards.
        g_ImGuiBatch->Render(ctx.GetCommandList());
    }

    void UpdateFrameConstants(const Inferno::Camera& camera, UploadBuffer<FrameConstants>& dest, float renderScale = 1) {
        //camera.Update(FrameTime);
        //camera.UpdatePerspective();
        auto size = camera.GetViewportSize();

        FrameConstants frameConstants{};
        frameConstants.ElapsedTime = (float)ElapsedTime;
        frameConstants.ViewProjection = camera.ViewProjection;
        frameConstants.NearClip = camera.GetNearClip();
        frameConstants.FarClip = camera.GetFarClip();
        frameConstants.Eye = camera.Position;
        frameConstants.EyeDir = camera.GetForward();
        frameConstants.EyeUp = camera.Up;
        frameConstants.Size = size * renderScale;
        frameConstants.RenderScale = renderScale;
        frameConstants.GlobalDimming = Game::GlobalDimming;
        frameConstants.NewLightMode = Settings::Graphics.NewLightMode;
        frameConstants.FilterMode = Settings::Graphics.FilterMode;

        dest.Begin();
        dest.Copy({ &frameConstants, 1 });
        dest.End();
    }

    Inferno::Camera BriefingCamera;

    void DrawBriefingModel(GraphicsContext& ctx,
                           const Object& object,
                           const UploadBuffer<FrameConstants>& frameConstants) {
        //if (object.IsCloaked() && Game::GetState() != GameState::Editor) {
        //    DrawCloakedModel(ctx, object, modelId, pass);
        //    return;
        //}

        auto& effect = Effects->BriefingObject;
        auto cmdList = ctx.GetCommandList();

        auto& model = Resources::GetModel(object.Render.Model.ID);

        if (ctx.ApplyEffect(effect)) {
            ctx.SetConstantBuffer(0, frameConstants.GetGPUVirtualAddress());
            effect.Shader->SetSampler(cmdList, GetWrappedTextureSampler());
            effect.Shader->SetNormalSampler(cmdList, GetNormalSampler());
            effect.Shader->SetTextureTable(cmdList, Render::Heaps->Materials.GetGpuHandle(0));
            effect.Shader->SetVClipTable(cmdList, Render::VClipBuffer->GetSRV());
            effect.Shader->SetMaterialInfoBuffer(cmdList, Render::MaterialInfoBuffer->GetSRV());
            effect.Shader->SetLightGrid(cmdList, *Render::LightGrid);
            auto cubeSrv = Render::Materials->EnvironmentCube.GetCubeSRV().GetGpuHandle();
            if (!cubeSrv.ptr)cubeSrv = Render::Adapter->NullCube.GetGpuHandle();
            effect.Shader->SetEnvironmentCube(cmdList, cubeSrv);
            effect.Shader->SetDissolveTexture(cmdList, Render::Materials->White().Handle());
        }

        ObjectShader::Constants constants = {};
#ifdef DEBUG_DISSOLVE
        constants.PhaseColor = object.Effects.PhaseColor;
        effect.Shader->SetDissolveTexture(cmdList, Render::Materials->Get("noise").Handle());
        effect.Shader->SetSampler(cmdList, GetWrappedTextureSampler());
        double x;
        constants.PhaseAmount = (float)std::modf(Clock.GetTotalTimeSeconds() * 0.5, &x);
#else
        if (object.IsPhasing()) {
            effect.Shader->SetDissolveTexture(cmdList, Render::Materials->Get("noise").Handle());
            constants.PhaseAmount = std::max(1 - object.Effects.GetPhasePercent(), 0.001f); // Shader checks for 0 to skip effect
            constants.PhaseColor = object.Effects.PhaseColor;
        }
#endif

        if (object.Render.Emissive != Color(0, 0, 0)) {
            // Ignore ambient if object is emissive
            constants.Ambient = Color(0, 0, 0);
            constants.EmissiveLight = object.Render.Emissive;
        }
        else {
            constants.Ambient = object.Ambient.GetColor().ToVector4();
            constants.EmissiveLight = Color(0, 0, 0);
        }

        //constants.TimeOffset = GetTimeOffset(object);
        constants.TimeOffset = 0;

        Matrix transform = Matrix::CreateScale(object.Scale) * object.GetTransform(Game::LerpAmount);
        bool transparentOverride = false;
        auto texOverride = TexID::None;

        if (object.Render.Model.TextureOverride != LevelTexID::None) {
            texOverride = Resources::LookupTexID(object.Render.Model.TextureOverride);
            if (texOverride != TexID::None)
                transparentOverride = Resources::GetTextureInfo(texOverride).Transparent;
        }

        constants.TexIdOverride = -1;

        if (texOverride != TexID::None) {
            if (auto effectId = Resources::GetEffectClipID(texOverride); effectId > EClipID::None)
                constants.TexIdOverride = (int)effectId + VCLIP_RANGE;
            else
                constants.TexIdOverride = (int)texOverride;
        }

        auto& meshHandle = GetMeshHandle(object.Render.Model.ID);

        for (int submodel = 0; submodel < model.Submodels.size(); submodel++) {
            auto world = GetSubmodelTransform(object, model, submodel) * transform;
            constants.World = world;

            // get the mesh associated with the submodel
            auto& subMesh = meshHandle.Meshes[submodel];

            for (int i = 0; i < subMesh.size(); i++) {
                auto mesh = subMesh[i];
                if (!mesh) continue;

                bool isTransparent = mesh->IsTransparent || transparentOverride;
                //if (isTransparent && pass != RenderPass::Transparent) continue;
                //if (!isTransparent && pass != RenderPass::Opaque) continue;

                if (isTransparent) {
                    auto& material = Resources::GetMaterial(mesh->Texture);
                    if (material.Additive)
                        ctx.ApplyEffect(Effects->ObjectGlow); // Additive blend
                    else
                        ctx.ApplyEffect(Effects->Object); // Alpha blend
                }
                else {
                    ctx.ApplyEffect(effect);
                }

                effect.Shader->SetConstants(cmdList, constants);

                cmdList->IASetVertexBuffers(0, 1, &mesh->VertexBuffer);
                cmdList->IASetIndexBuffer(&mesh->IndexBuffer);
                cmdList->DrawIndexedInstanced(mesh->IndexCount, 1, 0, 0, 0);
                Stats::DrawCalls++;
            }
        }
    }

    void DrawBriefingObject(GraphicsContext& ctx, const Object& object) {
        auto& target = Adapter->GetBriefingRobotBuffer();
        target.Transition(ctx.GetCommandList(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto& depthTarget = Adapter->GetBriefingRobotDepthBuffer();
        ctx.ClearColor(target);
        ctx.ClearDepth(depthTarget);
        ctx.SetRenderTarget(target.GetRTV(), depthTarget.GetDSV());

        Vector2 size((float)target.GetWidth(), (float)target.GetHeight());

        ctx.SetViewport(UINT(size.x), UINT(size.y));
        ctx.SetScissor(UINT(target.GetWidth()), UINT(target.GetHeight()));

        auto& model = Resources::GetModel(object.Render.Model.ID);
        if (model.DataSize == 0) return;

        auto& frameConstants = Adapter->GetBriefingFrameConstants();
        BriefingCamera.SetPosition(Vector3(0, model.Radius * .5f, -model.Radius * 3.0f));
        BriefingCamera.SetFov(45);
        BriefingCamera.SetViewport(size);
        BriefingCamera.UpdatePerspectiveMatrices();
        UpdateFrameConstants(BriefingCamera, frameConstants);

        ctx.GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        Render::DrawBriefingModel(ctx, object, frameConstants);

        if (Settings::Graphics.MsaaSamples > 1) {
            Adapter->BriefingRobot.ResolveFromMultisample(ctx.GetCommandList(), Adapter->BriefingRobotMsaa);
        }

        //target.Transition(ctx.GetCommandList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Adapter->BriefingRobot.Transition(ctx.GetCommandList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        // spin and animate
    }

    void DrawBriefing(GraphicsContext& ctx, RenderTarget& target, const BriefingState& briefing) {
        PIXScopedEvent(ctx.GetCommandList(), PIX_COLOR_INDEX(10), "Briefing");
        ctx.ClearColor(target);

        if (auto screen = briefing.GetScreen()) {
            if (auto page = briefing.GetPage()) {
                Vector2 scale(1, 1);
                if (Game::Level.IsDescent1()) {
                    scale.x = 640.0f / 320;
                    scale.y = 480.0f / 200;
                }

                if (auto object = briefing.GetObject())
                    DrawBriefingObject(ctx, *object);

                ctx.SetRenderTarget(target.GetRTV());
                ctx.SetViewport(UINT(target.GetWidth()), UINT(target.GetHeight()));
                ctx.SetScissor(UINT(target.GetWidth()), UINT(target.GetHeight()));
                BriefingCanvas->SetSize((uint)target.GetWidth(), (uint)target.GetHeight());

                if (screen->Background.empty()) {
                    BriefingCanvas->DrawRectangle({ 0, 0 }, { 640, 480 }, Color(0, 0, 0));
                }
                else {
                    auto& bg = Materials->Get(screen->Background);
                    BriefingCanvas->DrawBitmap(bg.Handle(), { 0, 0 }, { 640, 480 });
                }

                if (page->Robot != -1 || page->Model != ModelID::None) {
                    BriefingCanvas->DrawBitmap(Adapter->BriefingRobot.GetSRV(), Vector2(138, 55) * scale, Vector2(166, 138) * scale, Color(1, 1, 1), 1);
                }

                D3D12_GPU_DESCRIPTOR_HANDLE imageHandle{};

                if (page->Door != DClipID::None) {
                    // Draw a door
                    auto& dclip = Resources::GetDoorClip(page->Door);

                    // ping-pong the door animation
                    if (dclip.NumFrames > 0) {
                        auto frameTime = dclip.PlayTime / dclip.NumFrames;
                        auto frame = int(Inferno::Clock.GetTotalTimeSeconds() / frameTime);
                        frame %= dclip.NumFrames * 2;

                        if (frame >= dclip.NumFrames)
                            frame = (dclip.NumFrames - 1) - (frame % dclip.NumFrames);
                        else
                            frame %= dclip.NumFrames;

                        imageHandle = Render::Materials->Get(dclip.Frames[frame]).Handle();
                    }
                }
                else if (!page->Image.empty()) {
                    // Draw a static image (BBM, etc)
                    imageHandle = Render::Materials->Get(page->Image).Handle();
                }

                if (imageHandle.ptr)
                    BriefingCanvas->DrawBitmap(imageHandle, Vector2(220, 45) * scale, Vector2(64 * scale.x, 64 * scale.x), Color(1, 1, 1), 1);

                Render::DrawTextInfo info;
                info.Position = Vector2((float)screen->x, (float)screen->y) * scale;
                info.Font = FontSize::Small;
                //info.Scale = 0.5f;
                info.Color = Color(0, 1, 0);
                info.TabStop = screen->TabStop * scale.x;
                BriefingCanvas->DrawFadingText(page->Text, info,
                                               Game::Briefing.GetElapsed(),
                                               BRIEFING_TEXT_SPEED, screen->Cursor);

                BriefingCanvas->Render(ctx);

                //Adapter->Scanline.Execute(ctx.GetCommandList(), target, Adapter->BriefingScanlineBuffer);
                //Adapter->BriefingScanlineBuffer.Transition(ctx.GetCommandList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }
        }

        target.Transition(ctx.GetCommandList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void CopyMaterialData(ID3D12GraphicsCommandList* cmdList) {
        MaterialInfoUploadBuffer->Begin();
        MaterialInfoUploadBuffer->Copy(Resources::Materials.GetAllMaterialInfo());
        MaterialInfoUploadBuffer->End();

        MaterialInfoBuffer->Transition(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->CopyResource(MaterialInfoBuffer->Get(), MaterialInfoUploadBuffer->Get());
        MaterialInfoBuffer->Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void DrawHud(GraphicsContext& ctx) {
        auto width = Adapter->GetWidth();
        auto height = Adapter->GetHeight();
        HudCanvas->SetSize(width, height);
        HudGlowCanvas->SetSize(width, height);

        if (auto player = Game::Level.TryGetObject(ObjID(0))) {
            DrawHUD(Game::FrameTime, player->Ambient.GetColor());
        }

        if (Game::ScreenFlash != Color(0, 0, 0)) {
            CanvasBitmapInfo flash;
            flash.Size = Adapter->GetOutputSize();
            flash.Color = Game::ScreenFlash;
            flash.Texture = Materials->White().Handle();
            HudGlowCanvas->DrawBitmap(flash);
        }

        HudCanvas->Render(ctx);
        HudGlowCanvas->Render(ctx);
    }

    FrameUploadBuffer* GetFrameUploadBuffer() {
        return FrameUploadBuffers[Adapter->GetCurrentFrameIndex()].get();
    }

    void BindTempConstants(ID3D12GraphicsCommandList* cmdList, const void* data, uint64 size, uint32 rootParameter) {
        auto memory = GetFrameUploadBuffer()->GetMemory(size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        memcpy(memory.CPU, data, size);
        cmdList->SetGraphicsRootConstantBufferView(rootParameter, memory.GPU);
    }

    struct RenderInfo {
        Vector2 Size;
    };

    void RenderProbe(uint /*index*/) {
        //Adapter->WaitForGpu();
        //auto& ctx = Adapter->GetGraphicsContext();
        //ctx.Reset();
        //auto cmdList = ctx.GetCommandList();
        //Heaps->SetDescriptorHeaps(cmdList);
        //UpdateFrameConstants(Vector2(PROBE_RESOLUTION, PROBE_RESOLUTION), 90);
        //DrawLevel(ctx, Game::Level, true, index);
        //if (Settings::Graphics.MsaaSamples > 1) {
        //    Adapter->ProbeRenderCube.ResolveFromMultisample(cmdList, Adapter->ProbeRenderCubeMsaa);
        //}

        //if (Settings::Graphics.EnableBloom && Adapter->TypedUAVLoadSupport_R11G11B10_FLOAT())
        //    Bloom->Apply(cmdList, Adapter->ProbeRenderCube, index);

        //////Render::Adapter->GetProbeCube().Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        //Render::Adapter->ProbeRenderCube.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        //ctx.Execute();
        //Adapter->WaitForGpu();
    }

    void RenderProbe(const Vector3& position, Inferno::Camera& camera) {
        camera.Position = position;

        for (uint i = 0; i < 6; i++) {
            if (i == 0 || i == 1 || i == 4 || i == 5) {
                camera.Up = Vector3::UnitY;
            }

            if (i == 0)
                camera.Target = position + Vector3::UnitX;
            if (i == 1)
                camera.Target = position - Vector3::UnitX;

            // top and bottom
            if (i == 2) {
                camera.Target = position + Vector3::UnitY;
                camera.Up = -Vector3::UnitZ;
            }
            if (i == 3) {
                camera.Target = position - Vector3::UnitY;
                camera.Up = Vector3::UnitZ;
            }

            if (i == 4) {
                camera.Target = position + Vector3::UnitZ;
            }
            if (i == 5) {
                camera.Target = position - Vector3::UnitZ;
            }

            RenderProbe(i);
        }
    }

    void Present(const Camera& camera) {
        Metrics::BeginFrame();
        ScopedTimer presentTimer(&Metrics::Present);
        Stats::DrawCalls = 0;
        Stats::PolygonCount = 0;

        auto& ctx = Adapter->GetGraphicsContext();
        ctx.Reset();
        ctx.Camera = camera;
        auto cmdList = ctx.GetCommandList();
        Heaps->SetDescriptorHeaps(cmdList);
        //auto outputSize = Adapter->GetOutputSize();

        if (LevelChanged) {
            Adapter->WaitForGpu();
            RebuildLevelResources(Game::Level);

            if (Game::GetState() == GameState::Editor) {
                ResetEffects(); // prevent crashes due to ids changing
                // Reattach object lights
                for (auto& obj : Game::Level.Objects) {
                    auto ref = Game::GetObjectRef(obj);
                    Game::AttachLight(obj, ref);
                }
            }

            CopyMaterialData(cmdList);
            LoadVClips(cmdList); // todo: only load on initial level load
        }

        if (Game::BriefingVisible)
            DrawBriefing(ctx, Adapter->BriefingColorBuffer, Game::Briefing);


        // Create a terrain camera at the origin and orient it with the terrain
        // Always positioning it at the origin prevents any parallax effects on the planets
        Camera terrainCamera = ctx.Camera;
        terrainCamera.SetClipPlanes(50, 30'000);
        auto terrainInverse = ctx.Camera.GetOrientation() * Game::Terrain.InverseTransform;
        terrainCamera.MoveTo(Vector3::Zero, terrainInverse.Forward(), terrainInverse.Up());
        terrainCamera.UpdatePerspectiveMatrices();

        UpdateFrameConstants(terrainCamera, Adapter->GetTerrainConstants(), Render::RenderScale);
        UpdateFrameConstants(ctx.Camera, Adapter->GetFrameConstants(), Render::RenderScale);

        DrawLevel(ctx, Game::Level);
        Debug::EndFrame(ctx);

        if ((Game::GetState() == GameState::Game || Game::GetState() == GameState::GameMenu) && !Game::Player.IsDead)
            DrawHud(ctx);

        //LegitProfiler::ProfilerTask resolve("Resolve multisample", LegitProfiler::Colors::CLOUDS);
        if (Settings::Graphics.MsaaSamples > 1) {
            Adapter->SceneColorBuffer.ResolveFromMultisample(cmdList, Adapter->SceneColorBufferMsaa);
        }

        LegitProfiler::ProfilerTask postProcess("Post process");
        PostProcess(ctx);
        LegitProfiler::AddCpuTask(std::move(postProcess));
        DebugCanvas->Render(ctx);
        DrawUI(ctx);

        LegitProfiler::ProfilerTask present("Present", LegitProfiler::Colors::NEPHRITIS);
        Adapter->Present();
        GetFrameUploadBuffer()->ResetIndex();

        LegitProfiler::AddCpuTask(std::move(present));
        //Adapter->WaitForGpu();

        LegitProfiler::ProfilerTask copy("Copy materials", LegitProfiler::Colors::BELIZE_HOLE);
        Materials->Dispatch();
        CopyProceduralsToMainThread();
        _graphicsMemory->Commit(Adapter->BatchUploadQueue->Get());
        LegitProfiler::AddCpuTask(std::move(copy));
    }

    void ReloadTextures() {
        Materials->Reload();
        //NewTextureCache->Reload();
    }
}
