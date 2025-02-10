#include "pch.h"
#include "Render.h"
#include "imgui_local.h"
#include "Level.h"
#include "Editor/Editor.h"
#include "Buffers.h"
#include "Mesh.h"
#include "Render.Gizmo.h"
#include "Render.Debug.h"
#include "Render.Editor.h"
#include "Settings.h"
#include "DirectX.h"
#include "Render.Particles.h"
#include "Game.Text.h"
#include "Editor/UI/BriefingEditor.h"
#include "Graphics.h"
#include "HUD.h"
#include "ScopedTimer.h"
#include "LegitProfiler.h"
#include "MaterialLibrary.h"
#include "Procedural.h"
#include "Render.Automap.h"
#include "Render.Briefing.h"
#include "Render.Level.h"
#include "Render.MainMenu.h"
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

        Ptr<SpriteBatch> _postBatch;

        Ptr<UploadBuffer<MaterialInfo>> MaterialInfoUploadBuffer;
        Ptr<UploadBuffer<GpuVClip>> VClipUploadBuffer;
        Ptr<FrameUploadBuffer> FrameUploadBuffers[2];

        //Inferno::Camera DEFAULT_CAMERA;
        //Inferno::Camera* pCam = &DEFAULT_CAMERA;
    }

    //void SetCamera(Inferno::Camera& camera) {
    //    pCam = &camera;
    //}

    void DrawBillboard(GraphicsContext& ctx,
                       D3D12_GPU_DESCRIPTOR_HANDLE texture,
                       D3D12_GPU_VIRTUAL_ADDRESS frameConstants,
                       Inferno::Camera& camera,
                       const Vector3& position,
                       BillboardInfo& info) {
        auto transform = info.Up ? Matrix::CreateConstrainedBillboard(position, camera.Position, *info.Up) : Matrix::CreateBillboard(position, camera.Position, camera.Up);

        if (info.Rotation != 0)
            transform = Matrix::CreateRotationZ(info.Rotation) * transform;

        // create quad and transform it
        auto h = info.Radius * info.Ratio;
        auto w = info.Radius;
        auto p0 = Vector3::Transform({ -w, h, 0 }, transform); // bl
        auto p1 = Vector3::Transform({ w, h, 0 }, transform); // br
        auto p2 = Vector3::Transform({ w, -h, 0 }, transform); // tr
        auto p3 = Vector3::Transform({ -w, -h, 0 }, transform); // tl

        ObjectVertex v0(p0, { 0, 0 }, info.Color);
        ObjectVertex v1(p1, { 1, 0 }, info.Color);
        ObjectVertex v2(p2, { 1, 1 }, info.Color);
        ObjectVertex v3(p3, { 0, 1 }, info.Color);

        auto cmdList = ctx.GetCommandList();

        auto& effect = info.Terrain
            ? (info.Additive ? Effects->SpriteAdditiveTerrain : Effects->SpriteTerrain)
            : (info.Additive ? Effects->SpriteAdditive : Effects->Sprite);

        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, frameConstants);
        effect.Shader->SetDiffuse(cmdList, texture);
        effect.Shader->SetDepthTexture(cmdList, Adapter->LinearizedDepthBuffer.GetSRV());
        auto sampler = Render::GetClampedTextureSampler();
        effect.Shader->SetSampler(cmdList, sampler);
        effect.Shader->SetDepthBias(cmdList, info.Radius);

        // todo: replace horrible code with proper batching
        Stats::DrawCalls++;
        g_SpriteBatch->Begin(cmdList);
        g_SpriteBatch->DrawQuad(v0, v1, v2, v3);
        g_SpriteBatch->End();
    }

    void DrawBillboard(GraphicsContext& ctx,
                       TexID tid,
                       const Vector3& position,
                       BillboardInfo& info) {
        auto& ti = Resources::GetTextureInfo(tid);
        info.Ratio = (float)ti.Height / (float)ti.Width;
        auto& material = Materials->Get(tid);

        DrawBillboard(ctx, material.Handle(), Adapter->GetFrameConstants().GetGPUVirtualAddress(), ctx.Camera, position, info);
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
        StaticTextures->Normal.Load(batch, normalData, 2, 2, "normal", false, DXGI_FORMAT_R8G8B8A8_UNORM);
        StaticTextures->Normal.AddShaderResourceView();

        uint whiteData[] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
        StaticTextures->White.Load(batch, whiteData, 2, 2, "white", false, DXGI_FORMAT_R8G8B8A8_UNORM);
        StaticTextures->White.AddShaderResourceView();

        uint blackData[] = { 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000 };
        StaticTextures->Black.Load(batch, blackData, 2, 2, "black", false, DXGI_FORMAT_R8G8B8A8_UNORM);
        StaticTextures->Black.AddShaderResourceView();

        uint missingData[] = { 0xFFFF00FF, 0xFF000000, 0xFF000000, 0xFFFF00FF };
        StaticTextures->Missing.Load(batch, missingData, 2, 2, "missing", false, DXGI_FORMAT_R8G8B8A8_UNORM);
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
        MaterialInfoUploadBuffer = make_unique<UploadBuffer<MaterialInfo>>(MATERIAL_COUNT, "Material upload buffer");
        MaterialInfoBuffer = make_unique<StructuredBuffer>();
        MaterialInfoBuffer->Create("MaterialInfo", sizeof MaterialInfo, MATERIAL_COUNT);
        MaterialInfoBuffer->AddShaderResourceView();

        VClipUploadBuffer = make_unique<UploadBuffer<GpuVClip>>(VCLIP_COUNT, "vclip buffer");
        VClipBuffer = make_unique<StructuredBuffer>();
        VClipBuffer->Create("VClips", sizeof GpuVClip, VCLIP_COUNT);
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
        UICanvas = make_unique<HudCanvas2D>(Device, Effects->Hud);
        _graphicsMemory = make_unique<GraphicsMemory>(Device);
        //LightGrid->Load(L"shaders/FillLightGridCS.hlsl");
        //NewTextureCache = MakePtr<TextureCache>();

        CreateDefaultTextures();

        Materials = make_unique<MaterialLibrary>(MATERIAL_COUNT);
        Debug::Initialize();

        InitializeImGui(_hwnd, (float)Settings::Editor.FontSize);
        static_assert(sizeof(ImTextureID) >= sizeof(D3D12_CPU_DESCRIPTOR_HANDLE), "D3D12_CPU_DESCRIPTOR_HANDLE is too large to fit in an ImTextureID");
        g_ImGuiBatch = make_unique<ImGuiBatch>(Adapter->GetBackBufferCount());

        CreateEditorResources();
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
    }

    void Initialize(HWND hwnd, uint width, uint height) {
        assert(hwnd);
        _hwnd = hwnd;
        Adapter = make_unique<DeviceResources>(BackBufferFormat);
        StaticTextures = make_unique<StaticTextureDef>();
        Adapter->SetWindow(hwnd, width, height);
        Adapter->CreateDeviceResources();

        Adapter->CreateWindowSizeDependentResources();
        CreateDeviceDependentResources();
        Adapter->ReloadResources();

        GlobalMeshes = make_unique<GenericMeshes>();
        CreateMainMenuResources();
        CreateWindowSizeDependentResources(width, height);
        Editor::EditorCamera.SetViewport({ width, height });
        Game::MainCamera.SetViewport({ width, height });

        Editor::Events::LevelChanged += [] { LevelChanged = true; };
        Editor::Events::MaterialsChanged += [] { MaterialsChanged = true; };
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
        UICanvas.reset();
        _graphicsMemory.reset();
        g_SpriteBatch.reset();
        g_ImGuiBatch.reset();
        MaterialInfoBuffer.reset();
        MaterialInfoUploadBuffer.reset();
        VClipUploadBuffer.reset();
        VClipBuffer.reset();
        GlobalMeshes.reset();

        for (auto& buffer : FrameUploadBuffers)
            buffer.reset();

        ReleaseEditorResources();
        StopProceduralWorker();
        LevelResources = {};

        ToneMapping.reset();
        _postBatch.reset();
        Debug::Shutdown();
        Adapter.reset();
        Device = nullptr;
        ReportLiveObjects();
    }

    void Resize(uint width, uint height) {
        //SPDLOG_INFO("Resize: {} {}", width, height);

        if (!Adapter->WindowSizeChanged(width, height))
            return;

        CreateWindowSizeDependentResources(width, height);
        Editor::EditorCamera.SetViewport({ width, height });
        Game::MainCamera.SetViewport({ width, height });
        //pCam->SetViewport((float)width, (float)height);
        // Reset frame upload buffers, otherwise they run out of memory.
        // For some reason resizing does not increment the adapter frame index, causing the same buffer to be used.
        FrameUploadBuffers[0]->ResetIndex();
        FrameUploadBuffers[1]->ResetIndex();
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
        LevelResources = {};
        LevelResources.LevelMeshes = make_unique<PackedBuffer>(1024 * 1024 * 20);
        LevelResources.ObjectMeshes = MakePtr<MeshBuffer>(Resources::GameData.Models.size(), DESCENT3_MODEL_COUNT);
        auto& objectMeshes = LevelResources.ObjectMeshes;

        List<ModelID> modelIds;
        for (auto& obj : level.Objects) {
            if (obj.Render.Type == RenderType::Model) {
                objectMeshes->LoadModel(obj.Render.Model.ID);
                objectMeshes->LoadModel(Resources::GetDeadModelID(obj.Render.Model.ID));
                objectMeshes->LoadModel(Resources::GetDyingModelID(obj.Render.Model.ID));
            }
        }

        objectMeshes->LoadModel(Resources::GameData.ExitModel);
        objectMeshes->LoadModel(Resources::GameData.DestroyedExitModel);

        //{
        //    LoadOutrageModel(TEST_MODEL);
        //}

        Graphics::Lights = {};
        ResetEffects();
        LevelChanged = true;
    }

    MeshIndex& GetMeshHandle(ModelID id) {
        return LevelResources.ObjectMeshes->GetHandle(id);
    }

    MeshIndex& GetOutrageMeshHandle(ModelID id) {
        return LevelResources.ObjectMeshes->GetOutrageHandle(id);
    }

    void PostProcess(const GraphicsContext& ctx, PixelBuffer& source) {
        PIXScopedEvent(ctx.GetCommandList(), PIX_COLOR_INDEX(8), "Post");
        auto cmdList = ctx.GetCommandList();
        ToneMapping->ToneMap.Exposure = Game::Exposure;
        ToneMapping->ToneMap.BloomStrength = Game::BloomStrength;
        ToneMapping->Apply(cmdList, source);
        // draw to backbuffer using a shader + polygon

        source.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        _postBatch->SetViewport(Adapter->GetScreenViewport());
        _postBatch->Begin(cmdList);
        auto size = Adapter->GetOutputSize();
        _postBatch->Draw(source.GetSRV(), size, XMFLOAT2{ 0, 0 });
        _postBatch->End();
    }

    void DrawImguiBatch(GraphicsContext& ctx) {
        PIXScopedEvent(ctx.GetCommandList(), PIX_COLOR_INDEX(9), "UI");
        ScopedTimer imguiTimer(&Metrics::ImGui);
        Canvas->Render(ctx);
        // Imgui batch modifies render state greatly. Normal geometry will likely not render correctly afterwards.
        g_ImGuiBatch->Render(ctx.GetCommandList());
    }

    void UpdateFrameConstants(const Inferno::Camera& camera, UploadBuffer<FrameConstants>& dest, float renderScale) {
        auto size = camera.GetViewportSize();


        FrameConstants frameConstants{};
        frameConstants.ElapsedTime = Game::GetState() == GameState::MainMenu || Game::GetState() == GameState::Briefing
            ? (float)Inferno::Clock.GetTotalTimeSeconds()
            : (float)Game::Time;
        frameConstants.ViewProjection = camera.ViewProjection;
        frameConstants.NearClip = camera.GetNearClip();
        frameConstants.FarClip = camera.GetFarClip();
        frameConstants.Eye = camera.Position;
        frameConstants.EyeDir = camera.GetForward();
        frameConstants.EyeUp = camera.Up;
        frameConstants.Size = Vector2{ size.x * renderScale, size.y * renderScale };
        frameConstants.RenderScale = renderScale;
        frameConstants.GlobalDimming = Game::GlobalDimming;
        frameConstants.NewLightMode = Settings::Graphics.NewLightMode;
        frameConstants.FilterMode = Settings::Graphics.FilterMode;

        dest.Begin();
        dest.Copy({ &frameConstants, 1 });
        dest.End();
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
            DrawHUD(Game::FrameTime, player->Ambient.GetValue());
        }

        if (Game::ScreenFlash != Color(0, 0, 0)) {
            CanvasBitmapInfo flash;
            flash.Size = Vector2((float)width, (float)height);
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

    Color ApplyGamma(const Color& color, float gamma = 2.2f) {
        return { pow(color.x, gamma), pow(color.y, gamma), pow(color.z, gamma), color.w };
    }


    void SetRenderTarget(GraphicsContext& ctx, RenderTarget& target, DepthBuffer* depthBuffer = nullptr) {
        auto cmdList = ctx.GetCommandList();

        // Clear depth and color buffers
        ctx.SetViewportAndScissor(target.GetSize());
        target.Transition(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);

        ctx.ClearColor(target);
        ctx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        if (depthBuffer) {
            ctx.ClearDepth(*depthBuffer);
            ctx.SetRenderTarget(target.GetRTV(), depthBuffer->GetDSV());
        }
        else {
            ctx.SetRenderTarget(target.GetRTV());
        }
    }

    // Clears and binds depth buffers as the render target
    void BeginDepthPrepass(GraphicsContext& ctx) {
        auto& depthBuffer = Adapter->GetDepthBuffer();
        auto& linearDepthBuffer = Adapter->GetLinearDepthBuffer();
        ctx.ClearDepth(depthBuffer);
        ctx.ClearColor(linearDepthBuffer);
        ctx.ClearStencil(Adapter->GetDepthBuffer(), 0);
        ctx.GetCommandList()->OMSetStencilRef(0);

        linearDepthBuffer.Transition(ctx.GetCommandList(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        ctx.SetRenderTarget(linearDepthBuffer.GetRTV(), depthBuffer.GetDSV());
        ctx.SetViewportAndScissor(linearDepthBuffer.GetSize());
        ctx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    void Present(const Camera& camera) {
        ScopedTimer presentTimer(&Metrics::Present);
        Stats::DrawCalls = 0;
        Stats::PolygonCount = 0;

        auto& ctx = Adapter->GetGraphicsContext();
        ctx.Reset();
        ctx.Camera = camera;
        ctx.Camera.SetViewport(Adapter->GetOutputSize());
        ctx.Camera.UpdatePerspectiveMatrices();

        auto cmdList = ctx.GetCommandList();
        Heaps->SetDescriptorHeaps(cmdList);
        UpdateFrameConstants(ctx.Camera, Adapter->GetFrameConstants(), Settings::Graphics.RenderScale);
        //auto outputSize = Adapter->GetOutputSize();

        auto width = Adapter->GetWidth();
        auto height = Adapter->GetHeight();
        UICanvas->SetSize(width, height);

        SetRenderTarget(ctx, Adapter->GetRenderTarget(), &Adapter->GetDepthBuffer());
        ctx.ClearStencil(Adapter->GetDepthBuffer(), 0);

        if (MaterialsChanged) {
            CopyMaterialData(cmdList);
            LoadVClips(cmdList);
            MaterialsChanged = false;
        }

        if (Game::BriefingVisible) {
            DrawBriefing(ctx, Adapter->BriefingColorBuffer, Game::Briefing);
        }

        auto gameState = Game::GetState();

        if (gameState == GameState::Automap) {
            DrawAutomap(ctx);
        }
        else if (gameState == GameState::MainMenu) {
            DrawMainMenuBackground(ctx);
        }
        else if (gameState == GameState::Game ||
                 gameState == GameState::PauseMenu ||
                 gameState == GameState::Editor ||
                 gameState == GameState::PhotoMode ||
                 gameState == GameState::ExitSequence) {
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
            }

            if (TerrainChanged) {
                Adapter->WaitForGpu();
                Graphics::LoadTerrain(Game::Terrain);
                TerrainChanged = false;
            }

            // Create a terrain camera at the origin and orient it with the terrain
            // Always positioning it at the origin prevents any parallax effects on the planets
            Camera terrainCamera = ctx.Camera;
            terrainCamera.SetClipPlanes(50, 30'000);
            auto terrainInverse = ctx.Camera.GetOrientation() * Game::Terrain.InverseTransform;
            terrainCamera.MoveTo(Vector3::Zero, terrainInverse.Forward(), terrainInverse.Up());
            terrainCamera.UpdatePerspectiveMatrices();

            UpdateFrameConstants(terrainCamera, Adapter->GetTerrainConstants(), Settings::Graphics.RenderScale);


            DrawLevel(ctx, Game::Level);
        }

        EndUpdateEffects();

        if (!Settings::Inferno.ScreenshotMode && Game::GetState() == GameState::Editor) {
            PIXScopedEvent(cmdList, PIX_COLOR_INDEX(6), "Editor");
            //DrawEditor(ctx, Game::Level);
            DrawLevelDebug(Game::Level, ctx.Camera);
            DrawEditor(ctx, Game::Level);
            //LegitProfiler::ProfilerTask editor("Draw editor", LegitProfiler::Colors::CLOUDS);
            //LegitProfiler::AddCpuTask(std::move(editor));
        }

        Debug::EndFrame(ctx);

        /*
         * Resolve scene buffer 
         */

        //LegitProfiler::ProfilerTask resolve("Resolve multisample", LegitProfiler::Colors::CLOUDS);
        if (Settings::Graphics.MsaaSamples > 1) {
            Adapter->SceneColorBuffer.ResolveFromMultisample(cmdList, Adapter->SceneColorBufferMsaa);
        }

        /*
         * Switch to full screen, HDR, non-MSAA, composition render target
         */
        SetRenderTarget(ctx, Adapter->CompositionBuffer);

        Canvas->SetSize(Adapter->GetWidth(), Adapter->GetHeight());
        DebugCanvas->SetSize(Adapter->GetWidth(), Adapter->GetHeight());

        // Copy the scene into the composition buffer
        Adapter->SceneColorBuffer.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        auto& compose = Effects->Compose.Shader;
        ctx.ApplyEffect(Effects->Compose);
        cmdList->SetGraphicsRootSignature(compose->RootSignature.Get());
        compose->SetSource(cmdList, Adapter->SceneColorBuffer.GetSRV());
        compose->SetSampler(cmdList, Settings::Graphics.UpscaleFilter == UpscaleFilterMode::Point ? Heaps->States.PointClamp() : Heaps->States.LinearClamp());
        cmdList->DrawInstanced(3, 1, 0, 0);

        // Create a screenshot without the HUD
        if (TakeScoreScreenshot) {
            TakeScoreScreenshot = false;
            Adapter->BlurBufferTemp.CopyFrom(cmdList, Adapter->CompositionBuffer);
            Adapter->CompositionBuffer.Transition(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET); // Copying changes state, reset it back to RT

            Render::ToneMapping->Downsample.Execute(cmdList, Adapter->BlurBufferTemp, Adapter->BlurBufferDownsampled);
            //Render::ToneMapping->Blur.Execute(cmdList, Adapter->BlurBufferDownsampled, Adapter->BlurBuffer);
            //Render::ToneMapping->Blur.Execute(cmdList, Adapter->BlurBuffer, Adapter->ScoreBackground);
            Render::ToneMapping->Blur.Execute(cmdList, Adapter->BlurBufferDownsampled, Adapter->ScoreBackground);

            Adapter->ScoreBackground.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            Adapter->BlurBuffer.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }

        // Draw UI elements
        if (((Game::GetState() == GameState::Game || Game::GetState() == GameState::PauseMenu) && !Game::Player.IsDead) ||
            Game::GetState() == GameState::MainMenu ||
            GetEscapeScene() == EscapeScene::Start)
            DrawHud(ctx);

        if (Settings::Inferno.ScreenshotMode || Game::GetState() != GameState::Editor) {
            //Canvas->DrawGameText(level.Name, 0, 20 * Shell::DpiScale, FontSize::Big, { 1, 1, 1 }, 0.5f, AlignH::Center, AlignV::Top);
            if (Game::GetState() == GameState::Automap) {
                DrawAutomapText(ctx);
            }
            else if (Game::GetState() != GameState::MainMenu) {
                Render::DrawTextInfo info;
                info.Position = Vector2(-10 * Shell::DpiScale, -10 * Shell::DpiScale);
                info.HorizontalAlign = AlignH::Right;
                info.VerticalAlign = AlignV::Bottom;
                info.Font = FontSize::MediumGold;
                info.Scale = 0.5f;
                Canvas->DrawGameText("Inferno\nEngine", info);
            }
        }

        // Create the blurred menu background texture
        if (Game::GetState() == GameState::PauseMenu) {
            Adapter->BlurBufferTemp.CopyFrom(cmdList, Adapter->CompositionBuffer);
            Adapter->CompositionBuffer.Transition(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET); // Copying changes state, reset it back to RT

            Render::ToneMapping->Downsample.Execute(cmdList, Adapter->BlurBufferTemp, Adapter->BlurBufferDownsampled);
            Render::ToneMapping->Blur.Execute(cmdList, Adapter->BlurBufferDownsampled, Adapter->BlurBuffer);
            Render::ToneMapping->Blur.Execute(cmdList, Adapter->BlurBuffer, Adapter->BlurBufferDownsampled);

            Adapter->BlurBufferDownsampled.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            Adapter->BlurBuffer.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }

        UICanvas->Render(ctx);

        LegitProfiler::ProfilerTask postProcess("Post process");

        // Draw to the back buffer
        SetRenderTarget(ctx, Adapter->GetBackBuffer());

        PostProcess(ctx, Adapter->CompositionBuffer);
        LegitProfiler::AddCpuTask(std::move(postProcess));
        DebugCanvas->Render(ctx);
        DrawImguiBatch(ctx);

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
}
