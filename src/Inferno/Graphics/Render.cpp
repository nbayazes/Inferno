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
#include <ScopedTimer.h>

#include "Render.Level.h"

using namespace DirectX;
using namespace Inferno::Graphics;

namespace Inferno::Render {
    using VertexType = DirectX::VertexPositionTexture;

    Color ClearColor = { 0.1f, 0.1f, 0.1f, 1.0f };
    bool LevelChanged = false;
    constexpr int MATERIAL_COUNT = 3000;

    namespace {
        HWND _hwnd;

        // todo: put all of these resources into a class and use RAII
        Ptr<GraphicsMemory> _graphicsMemory;

        Ptr<MeshBuffer> _meshBuffer;
        Ptr<SpriteBatch> _postBatch;
        Ptr<PackedBuffer> _levelMeshBuffer;
    }

    PackedBuffer* GetLevelMeshBuffer() { return _levelMeshBuffer.get(); }

    // Applies an effect that uses the frame constants
    template <class T>
    void ApplyEffect(GraphicsContext& ctx, const Effect<T>& effect) {
        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, Adapter->FrameConstantsBuffer.GetGPUVirtualAddress());
    }

    void DrawBillboard(GraphicsContext& ctx,
                       TexID tid,
                       const Vector3& position,
                       float radius,
                       const Color& color,
                       bool additive,
                       float rotation,
                       const Vector3* up) {
        auto transform = up ? Matrix::CreateConstrainedBillboard(position, Camera.Position, *up) : Matrix::CreateBillboard(position, Camera.Position, Camera.Up);

        if (rotation != 0)
            transform = Matrix::CreateRotationZ(rotation) * transform;

        // create quad and transform it
        auto& ti = Resources::GetTextureInfo(tid);
        auto ratio = (float)ti.Height / (float)ti.Width;
        auto h = radius * ratio;
        auto w = radius;
        auto p0 = Vector3::Transform({ -w, h, 0 }, transform);  // bl
        auto p1 = Vector3::Transform({ w, h, 0 }, transform);   // br
        auto p2 = Vector3::Transform({ w, -h, 0 }, transform);  // tr
        auto p3 = Vector3::Transform({ -w, -h, 0 }, transform); // tl

        ObjectVertex v0(p0, { 0, 0 }, color);
        ObjectVertex v1(p1, { 1, 0 }, color);
        ObjectVertex v2(p2, { 1, 1 }, color);
        ObjectVertex v3(p3, { 0, 1 }, color);

        auto& effect = additive ? Effects->SpriteAdditive : Effects->Sprite;
        ApplyEffect(ctx, effect);
        auto& material = Materials->Get(tid);
        effect.Shader->SetDiffuse(ctx.CommandList(), material.Handle());
        effect.Shader->SetDepthTexture(ctx.CommandList(), Adapter->LinearizedDepthBuffer.GetSRV());
        auto sampler = Render::GetClampedTextureSampler();
        effect.Shader->SetSampler(ctx.CommandList(), sampler);

        Stats::DrawCalls++;
        g_SpriteBatch->Begin(ctx.CommandList());
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
                Bloom->LoadResources(batch);
            }
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR(e.what());
        }
        EndTextureUpload(batch);
    }

    // Initialize device dependent objects here (independent of window size).
    void CreateDeviceDependentResources() {
        Shaders = MakePtr<ShaderResources>();
        Effects = MakePtr<EffectResources>(Shaders.get());
        Bloom = MakePtr<PostFx::Bloom>();
        MaterialInfoUploadBuffer = MakePtr<UploadBuffer<MaterialInfo>>(MATERIAL_COUNT);
        MaterialInfoBuffer = MakePtr<StructuredBuffer>();
        MaterialInfoBuffer->Create(L"MaterialInfo", sizeof MaterialInfo, MATERIAL_COUNT);
        MaterialInfoBuffer->AddShaderResourceView();
        UploadBuffer<MaterialInfo> materialInfoUpload(MATERIAL_COUNT);

        //Materials2 = MakePtr<MaterialLibrary2>(Device, 64 * 64 * 4 * 1000);
        g_SpriteBatch = MakePtr<PrimitiveBatch<ObjectVertex>>(Device);
        Canvas = MakePtr<Canvas2D<UIShader>>(Device, Effects->UserInterface);
        BriefingCanvas = MakePtr<Canvas2D<UIShader>>(Device, Effects->UserInterface);
        HudCanvas = MakePtr<HudCanvas2D>(Device, Effects->Hud);
        HudGlowCanvas = MakePtr<HudCanvas2D>(Device, Effects->HudAdditive);
        _graphicsMemory = MakePtr<GraphicsMemory>(Device);
        LightGrid = MakePtr<FillLightGridCS>();
        //LightGrid->Load(L"shaders/FillLightGridCS.hlsl");
        NewTextureCache = MakePtr<TextureCache>();

        CreateDefaultTextures();

        Materials = MakePtr<MaterialLibrary>(MATERIAL_COUNT);
        Debug::Initialize();

        ImGuiBatch::Initialize(_hwnd, (float)Settings::Editor.FontSize);
        static_assert(sizeof(ImTextureID) >= sizeof(D3D12_CPU_DESCRIPTOR_HANDLE), "D3D12_CPU_DESCRIPTOR_HANDLE is too large to fit in an ImTextureID");
        g_ImGuiBatch = MakePtr<ImGuiBatch>(Adapter->GetBackBufferCount());

        CreateEditorResources();
        LoadFonts();

        ResourceUploadBatch resourceUpload(Device);

        resourceUpload.Begin();

        {
            RenderTargetState rtState(Adapter->GetBackBufferFormat(), Adapter->SceneDepthBuffer.GetFormat());
            SpriteBatchPipelineStateDescription pd(rtState);
            _postBatch = MakePtr<SpriteBatch>(Device, resourceUpload, pd);
        }

        auto task = resourceUpload.End(Adapter->GetCommandQueue());
        task.wait();
    }

    void CreateWindowSizeDependentResources(int width, int height) {
        Bloom->Create(width, height);
        LightGrid->CreateBuffers(width, height);
    }

    void Initialize(HWND hwnd, int width, int height) {
        assert(hwnd);
        _hwnd = hwnd;
        Adapter = MakePtr<DeviceResources>(BackBufferFormat);
        StaticTextures = MakePtr<StaticTextureDef>();
        Adapter->SetWindow(hwnd, width, height);
        Adapter->CreateDeviceResources();

        Render::Heaps = MakePtr<DescriptorHeaps>(10, 200, MATERIAL_COUNT * 5);
        Render::UploadHeap = MakePtr<UserDescriptorHeap>(MATERIAL_COUNT * 5, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, false);
        Render::UploadHeap->SetName(L"Upload Heap");
        Render::Uploads = MakePtr<DescriptorRange<5>>(*Render::UploadHeap, Render::UploadHeap->Size());

        Adapter->CreateWindowSizeDependentResources();
        CreateDeviceDependentResources();
        Adapter->ReloadResources();

        CreateWindowSizeDependentResources(width, height);
        Camera.SetViewport((float)width, (float)height);
        _levelMeshBuffer = MakePtr<PackedBuffer>(1024 * 1024 * 10);

        Editor::Events::LevelChanged += [] { LevelChanged = true; };
        Editor::Events::TexturesChanged += [] {
            //PendingTextures.push_back(id);
            Materials->LoadLevelTextures(Game::Level, false);
        };
    }

    void Shutdown() {
        if (Adapter)
            Adapter->WaitForGpu();

        Materials->Shutdown(); // wait for thread to terminate
        Materials.reset();
        NewTextureCache.reset();
        Render::Heaps.reset();
        Render::UploadHeap.reset();
        StaticTextures.reset();
        Effects.reset();
        Shaders.reset();
        Canvas.reset();
        BriefingCanvas.reset();
        HudCanvas.reset();
        HudGlowCanvas.reset();
        _graphicsMemory.reset();
        g_SpriteBatch.reset();
        g_ImGuiBatch.reset();
        MaterialInfoBuffer.reset();
        MaterialInfoUploadBuffer.reset();

        ReleaseEditorResources();
        _levelMeshBuffer.reset();
        _meshBuffer.reset();

        Adapter.reset();
        Bloom.reset();
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
        Camera.SetViewport((float)width, (float)height);
    }

    // Loads a single model at runtime
    void LoadModelDynamic(ModelID id) {
        if (!_meshBuffer) return;
        _meshBuffer->LoadModel(id);
        auto ids = GetTexturesForModel(id);
        Materials->LoadMaterials(ids, false);
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

    void LoadHUDTextures() {
        Materials->LoadMaterials(Resources::GameData.HiResGauges, false);
        Materials->LoadMaterials(Resources::GameData.Gauges, false);
    }

    void LoadTextureDynamic(VClipID id) {
        auto& vclip = Resources::GetVideoClip(id);
        Materials->LoadMaterials(vclip.GetFrames(), false);
    }

    ModelID LoadOutrageModel(const string& path) {
        auto id = Resources::LoadOutrageModel(path);
        if (auto model = Resources::GetOutrageModel(id)) {
            _meshBuffer->LoadOutrageModel(*model, id);
            //Materials->LoadOutrageModel(*model);
            NewTextureCache->MakeResident();
        }

        return id;
    }

    void LoadLevel(const Level& level) {
        Adapter->WaitForGpu();

        SPDLOG_INFO("Load models");
        // Load models for objects in the level
        constexpr int DESCENT3_MODEL_COUNT = 200;
        _meshBuffer = MakePtr<MeshBuffer>(Resources::GameData.Models.size(), DESCENT3_MODEL_COUNT);

        List<ModelID> modelIds;
        for (auto& obj : level.Objects)
            if (obj.Render.Type == RenderType::Model)
                _meshBuffer->LoadModel(obj.Render.Model.ID);

        //{
        //    LoadOutrageModel(TEST_MODEL);
        //}

        ResetLightCache();
        InitEffects(level);

        LevelChanged = true;
        //_levelMeshBuilder.Update(level, *_levelMeshBuffer);
    }

    MeshIndex& GetMeshHandle(ModelID id) {
        return _meshBuffer->GetHandle(id);
    }

    MeshIndex& GetOutrageMeshHandle(ModelID id) {
        return _meshBuffer->GetOutrageHandle(id);
    }

    void ClearMainRenderTarget(const GraphicsContext& ctx) {
        //ctx.BeginEvent(L"Clear");

        auto& target = Adapter->GetHdrRenderTarget();
        auto& depthBuffer = Adapter->GetHdrDepthBuffer();
        ctx.SetRenderTarget(target.GetRTV(), depthBuffer.GetDSV());
        //ctx.ClearColor(target);
        //ctx.ClearDepth(depthBuffer);
        ctx.SetViewportAndScissor((UINT)target.GetWidth(), (UINT)target.GetHeight());

        //ctx.EndEvent();
    }

    void PostProcess(const GraphicsContext& ctx) {
        ctx.BeginEvent(L"Post");
        // Post process
        auto backBuffer = Adapter->GetBackBuffer();
        ctx.ClearColor(*backBuffer);
        ctx.SetRenderTarget(backBuffer->GetRTV());

        Adapter->SceneColorBuffer.Transition(ctx.CommandList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        if (Settings::Graphics.EnableBloom && Adapter->TypedUAVLoadSupport_R11G11B10_FLOAT())
            Bloom->Apply(ctx.CommandList(), Adapter->SceneColorBuffer);

        // draw to backbuffer using a shader + polygon
        _postBatch->SetViewport(Adapter->GetScreenViewport());
        _postBatch->Begin(ctx.CommandList());
        auto size = Adapter->GetOutputSize();
        _postBatch->Draw(Adapter->SceneColorBuffer.GetSRV(), XMUINT2{ (uint)size.x, (uint)size.y }, XMFLOAT2{ 0, 0 });

        //if (DebugEmissive)
        //    draw with shader that subtracts 1 from all values;

        _postBatch->End();
    }

    void DrawUI(GraphicsContext& ctx) {
        ctx.BeginEvent(L"UI");
        ScopedTimer imguiTimer(&Metrics::ImGui);
        Canvas->Render(ctx);
        // Imgui batch modifies render state greatly. Normal geometry will likely not render correctly afterwards.
        g_ImGuiBatch->Render(ctx.CommandList());
        ctx.EndEvent();
    }

    void DrawBriefing(GraphicsContext& ctx, RenderTarget& target) {
        if (!Settings::Editor.Windows.BriefingEditor) return;

        ctx.BeginEvent(L"Briefing");
        ctx.ClearColor(target);
        ctx.SetRenderTarget(target.GetRTV());
        ctx.SetViewportAndScissor((UINT)target.GetWidth(), (UINT)target.GetHeight());
        auto& briefing = Editor::BriefingEditor::DebugBriefing;
        BriefingCanvas->SetSize((uint)target.GetWidth(), (uint)target.GetHeight());
        if (!briefing.Screens.empty() && !briefing.Screens[0].Pages.empty()) {
            BriefingCanvas->DrawGameText(briefing.Screens[1].Pages[1], 20, 20, FontSize::Small, { 0, 1, 0 });
        }
        BriefingCanvas->Render(ctx);

        Adapter->Scanline.Execute(ctx.CommandList(), target, Adapter->BriefingScanlineBuffer);
        Adapter->BriefingScanlineBuffer.Transition(ctx.CommandList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        target.Transition(ctx.CommandList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        ctx.EndEvent();
    }

    void CopyMaterialData(ID3D12GraphicsCommandList* cmdList) {
        MaterialInfoUploadBuffer->Begin();
        MaterialInfoUploadBuffer->Copy(Materials->GetAllMaterialInfo());
        MaterialInfoUploadBuffer->End();

        MaterialInfoBuffer->Transition(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->CopyResource(MaterialInfoBuffer->Get(), MaterialInfoUploadBuffer->Get());
        MaterialInfoBuffer->Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void Present() {
        Metrics::BeginFrame();
        ScopedTimer presentTimer(&Metrics::Present);
        Stats::DrawCalls = 0;
        Stats::PolygonCount = 0;

        UpdateEffects(FrameTime);

        auto& ctx = Adapter->GetGraphicsContext();
        ctx.Reset();

        if (LevelChanged)
            CopyMaterialData(ctx.CommandList());

        Heaps->SetDescriptorHeaps(ctx.CommandList());
        DrawBriefing(ctx, Adapter->BriefingColorBuffer);

        auto output = Adapter->GetOutputSize();
        Camera.Update(FrameTime);
        Camera.SetViewport(output.x, output.y);
        Camera.LookAtPerspective(Settings::Editor.FieldOfView, Game::Time);
        ViewProjection = Camera.ViewProj();
        CameraFrustum = Camera.GetFrustum();

        FrameConstants frameConstants{};
        frameConstants.ElapsedTime = (float)ElapsedTime;
        frameConstants.ViewProjection = Camera.ViewProj();
        frameConstants.NearClip = Camera.NearClip;
        frameConstants.FarClip = Camera.FarClip;
        frameConstants.Eye = Camera.Position;
        frameConstants.FrameSize = Adapter->GetOutputSize();
        frameConstants.GlobalDimming = Game::ControlCenterDestroyed ? float(sin(Game::CountdownTimer * 4) * 0.5 + 0.5) : 1;
        frameConstants.NewLightMode = Settings::Graphics.NewLightMode;
        frameConstants.FilterMode = Settings::Graphics.FilterMode;

        Adapter->FrameConstantsBuffer.Begin();
        Adapter->FrameConstantsBuffer.Copy({ &frameConstants, 1 });
        Adapter->FrameConstantsBuffer.End();

        DrawLevel(ctx, Game::Level);
        if (Game::GetState() == GameState::Game) {
            auto width = Adapter->GetWidth();
            auto height = Adapter->GetHeight();
            HudCanvas->SetSize(width, height);
            HudGlowCanvas->SetSize(width, height);

            if (auto player = Game::Level.TryGetObject(ObjID(0))) {
                DrawHUD(Render::FrameTime, player->Ambient.GetColor() + player->DirectLight.GetColor() * 3);
            }

            if (Game::ScreenFlash.ToVector3().LengthSquared() > 0) {
                CanvasBitmapInfo flash;
                flash.Size = Adapter->GetOutputSize();
                flash.Color = Game::ScreenFlash;
                flash.Texture = Materials->White().Handle();
                HudGlowCanvas->DrawBitmap(flash);
            }

            HudCanvas->Render(ctx);
            HudGlowCanvas->Render(ctx);
        }

        if (Settings::Graphics.MsaaSamples > 1) {
            Adapter->SceneColorBuffer.ResolveFromMultisample(ctx.CommandList(), Adapter->MsaaColorBuffer);
        }

        PostProcess(ctx);
        DrawUI(ctx);

        auto commandQueue = Adapter->GetCommandQueue();
        {
            ScopedTimer presentCallTimer(&Metrics::PresentCall);
            //PIXBeginEvent(commandQueue, PIX_COLOR_DEFAULT, L"Present");
            Adapter->Present();
            //PIXEndEvent(commandQueue);
        }

        Materials->Dispatch();
        _graphicsMemory->Commit(commandQueue);
    }

    void ReloadTextures() {
        Materials->Reload();
        //NewTextureCache->Reload();
    }
}
