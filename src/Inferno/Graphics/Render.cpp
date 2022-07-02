#include "pch.h"
#include "Render.h"
#include "imgui_local.h"
#include "Level.h"
#include "Editor/Editor.h"
#include "Buffers.h"
#include "Utility.h"
#include "Graphics/LevelMesh.h"
#include "Mesh.h"
#include "Render.Gizmo.h"
#include "Render.Debug.h"
#include "Render.Editor.h"
#include "Settings.h"
#include "DirectX.h"
#include "Physics.h"

using namespace DirectX;

namespace Inferno::Render {
    Color ClearColor = { 0.1f, 0.1f, 0.1f, 1.0f };
    BoundingFrustum CameraFrustum;
    bool LevelChanged = false;

    // Dynamic render batches
    // Usage: Batch vertices / indices then use returned structs to render later
    template<class TVertex, class TIndex = unsigned short>
    class RenderBatch {
        ID3D12Device* _device;

        ComPtr<ID3D12Resource> _indexBuffer;
        ComPtr<ID3D12Resource> _vertexBuffer;

        //Buffer<TVertex> _vertexBuffer;

        int _vertexOffset = 0, _indexOffset = 0;
        int _indexBufferSize;
        int _vertexBufferSize;
        bool _inBatch = false;
        void* _pVertexBuffer;
        void* _pIndexBuffer;
        //int _requestedIndexBufferSize = _indexBufferSize;
        //int _requestedVertexBufferSize = _vertexBufferSize;

    public:
        RenderBatch(ID3D12Device* device, uint vertexCapacity = 5000, uint indexCapacity = 10000)
            : _device(device), _indexBufferSize(indexCapacity), _vertexBufferSize(vertexCapacity) {
            //CreateUploadBuffer(_vertexBuffer, vertexCapacity * sizeof(TVertex));
            //CreateUploadBuffer(_indexBuffer, indexCapacity * sizeof(TIndex));
        }

        void Begin() {
            if (_inBatch) throw Exception("Cannot start batch if already began");

            _inBatch = true;
            _vertexOffset = 0;
            _indexOffset = 0;

            ThrowIfFailed(_vertexBuffer->Map(0, &CPU_READ_NONE, &_pVertexBuffer));
            ThrowIfFailed(_indexBuffer->Map(0, &CPU_READ_NONE, &_pIndexBuffer));
        }

        //RenderBatchHandle Batch(List<TVertex>& vertices, List<TIndex>& index) {
        //    if (_indexOffset + index.size() > _indexBufferSize) {
        //        SPDLOG_WARN("Batch capacity reached");
        //        return {};
        //        // grow buffers if too small, but can't in the middle of a frame...
        //        // request to grow on next frame? warning
        //    }

        //    std::memcpy(_pVertexBuffer + _vertexOffset * sizeof(TVertex), vertices.data(), vertices.size() * sizeof(TVertex));
        //    std::memcpy(_pIndexBuffer + _indexOffset * sizeof(TIndex), index.data(), index.size() * sizeof(TIndex));
        //    //std::copy(vertices.begin(), vertices.end(), _pVertexBuffer + _vertexOffset * sizeof(TVertex));

        //    RenderBatchHandle handle = { _indexOffset, _vertexOffset, vertices.size() };
        //    _vertexOffset += vertices.size();
        //    _indexOffset += index.size();
        //    return handle;
        //}

        void End() {
            _inBatch = false;

            _vertexBuffer->Unmap(0, &CPU_READ_NONE);
            _indexBuffer->Unmap(0, &CPU_READ_NONE);
        }
    };


}

namespace Inferno::Render {

    using VertexType = DirectX::VertexPositionTexture;

    namespace {
        HWND _hwnd;

        // todo: put all of these resources into a class and use RAII
        Ptr<GraphicsMemory> _graphicsMemory;
        Ptr<PrimitiveBatch<ObjectVertex>> _spriteBatch;

        Ptr<PrimitiveBatch<CanvasVertex>> _canvasBatch;
        Ptr<MeshBuffer> _meshBuffer;
        Ptr<SpriteBatch> _tempBatch;
        List<Object> MatcenEffects;
    }

    struct RenderBatchHandle {
        int IndexOffset;
        int BufferOffset;
        int Size;
    };

    //struct PolygonDrawData {
    //    int Buffer; // src buffer id
    //    RenderBatchHandle Handle;
    //    IEffect* Effect;
    //    void* EffectData; // cbuffer parameters to effect... needs to be packed
    //};

    LevelMeshBuilder _levelMeshBuilder;
    Ptr<PackedBuffer> _levelMeshBuffer;

    void DrawObject(double alpha, const Object& object, ID3D12GraphicsCommandList* cmd);

    List<RenderCommand> _opaqueQueue;
    List<RenderCommand> _transparentQueue;

    void DrawOpaque(RenderCommand cmd) {
        _opaqueQueue.push_back(cmd);
    }

    void DrawTransparent(RenderCommand cmd) {
        _transparentQueue.push_back(cmd);
    }

    void DrawModel(float t, const Object& object, ID3D12GraphicsCommandList* cmd, ModelID modelId, TexID texOverride = TexID::None) {
        auto& effect = Effects->Object;
        effect.Apply(cmd);
        auto& model = Resources::GetModel(modelId);
        if (model.DataSize == 0) {
            DrawObjectOutline(object);
            return;
        }
        auto& meshHandle = _meshBuffer->GetHandle(modelId);

        effect.Shader->SetSampler(cmd, GetTextureSampler());
        ObjectShader::Constants constants = {};
        constants.Eye = Camera.Position;

        auto& seg = Game::Level.GetSegment(object.Segment);
        constants.LightColor[0] = Settings::RenderMode == RenderMode::Shaded ? seg.VolumeLight : Color(1, 1, 1);

        //Matrix transform = object.GetTransform(t);
        Matrix transform = Matrix::Lerp(object.PrevTransform, object.Transform, t);
        transform.Forward(-transform.Forward()); // flip z axis to correct for LH models

        if (object.Control.Type == ControlType::Weapon) {
            // Multiply angular velocities by 2PI to decompress them from fixed point form
            auto r = Matrix::CreateFromYawPitchRoll(object.Movement.Physics.AngularVelocity * (float)ElapsedTime * 6.28f);
            auto translation = transform.Translation();
            transform *= Matrix::CreateTranslation(translation);
            transform = r * transform;
            transform *= Matrix::CreateTranslation(-translation);
        }

        int submodelIndex = 0;
        for (auto& submodel : model.Submodels) {
            // accumulate the offsets for each submodel
            auto submodelOffset = Vector3::Zero;
            auto* smc = &submodel;
            while (smc->Parent != ROOT_SUBMODEL) {
                submodelOffset += smc->Offset;
                smc = &model.Submodels[smc->Parent];
            }

            auto world = Matrix::CreateTranslation(submodelOffset) * transform;
            constants.World = world;
            constants.Projection = world * ViewProjection;
            //constants.Time = (float)ElapsedTime;
            effect.Shader->SetConstants(cmd, constants);

            // get the mesh associated with the submodel
            auto& subMesh = meshHandle.Meshes[submodelIndex++];

            for (int i = 0; i < subMesh.size(); i++) {
                auto mesh = subMesh[i];
                if (!mesh) continue;

                TexID tid = texOverride;
                if (texOverride == TexID::None)
                    tid = mesh->EffectClip == EClipID::None ? mesh->Texture : Resources::GetEffectClip(mesh->EffectClip).GetFrame(ElapsedTime);

                const Material2D& material = tid == TexID::None ? Materials->White : Materials->Get(tid);
                effect.Shader->SetMaterial(cmd, material);

                cmd->IASetVertexBuffers(0, 1, &mesh->VertexBuffer);
                cmd->IASetIndexBuffer(&mesh->IndexBuffer);
                cmd->DrawIndexedInstanced(mesh->IndexCount, 1, 0, 0, 0);
                DrawCalls++;
            }
        }
    }

    void DrawVClip(ID3D12GraphicsCommandList* cmd, const VClip& vclip, const Matrix& transform, float radius, bool aligned) {
        auto frame = vclip.NumFrames - (int)std::floor(ElapsedTime / vclip.FrameTime) % vclip.NumFrames - 1;
        auto tid = vclip.Frames[frame];
        auto forward = Camera.GetForward();

        Matrix billboard = [&] {
            if (aligned) {
                auto objectUp = transform.Up();
                return Matrix::CreateConstrainedBillboard(transform.Translation(), Camera.Position, objectUp, &forward);
            }
            else {
                return Matrix::CreateBillboard(transform.Translation(), Camera.Position, Camera.Up, &forward);
            }
        }();

        // create quad and transform it
        auto& ti = Resources::GetTextureInfo(tid);
        auto ratio = (float)ti.Height / (float)ti.Width;
        auto h = radius * ratio;
        auto w = radius;
        auto p0 = Vector3::Transform({ -w, h, 0 }, billboard); // bl
        auto p1 = Vector3::Transform({ w, h, 0 }, billboard); // br
        auto p2 = Vector3::Transform({ w, -h, 0 }, billboard); // tr
        auto p3 = Vector3::Transform({ -w, -h, 0 }, billboard); // tl

        const Color white = { 1, 1, 1 };
        ObjectVertex v0(p0, { 0, 0 }, white);
        ObjectVertex v1(p1, { 1, 0 }, white);
        ObjectVertex v2(p2, { 1, 1 }, white);
        ObjectVertex v3(p3, { 0, 1 }, white);

        auto& effect = Effects->Sprite;
        effect.Apply(cmd);
        auto& material = Materials->Get(tid);
        effect.Shader->SetWorldViewProjection(cmd, ViewProjection);
        effect.Shader->SetDiffuse(cmd, material.Handles[0]);
        auto sampler = Render::GetClampedTextureSampler();
        effect.Shader->SetSampler(cmd, sampler);

        DrawCalls++;
        _spriteBatch->Begin(cmd);
        _spriteBatch->DrawQuad(v0, v1, v2, v3);
        _spriteBatch->End();
    }

    void DrawSprite(const Object& object, ID3D12GraphicsCommandList* cmd, bool aligned = false) {
        auto& vclip = Resources::GetVideoClip(object.Render.VClip.ID);
        if (vclip.NumFrames == 0) {
            DrawObjectOutline(object);
            return;
        }

        DrawVClip(cmd, vclip, object.Transform, object.Radius, aligned);
    }

    void DrawLevelMesh(ID3D12GraphicsCommandList* cmdList, const Inferno::LevelMesh& mesh) {
        assert(mesh.Chunk);
        auto& chunk = *mesh.Chunk;

        LevelShader::InstanceConstants consts{};
        consts.FrameTime = (float)FrameTime;
        consts.Time = (float)ElapsedTime;
        consts.LightingScale = Settings::RenderMode == RenderMode::Shaded ? 1.0f : 0.1f;

        {
            auto& map1 = chunk.EffectClip1 == EClipID::None ?
                Materials->Get(chunk.MapID1) :
                Materials->Get(Resources::GetEffectClip(chunk.EffectClip1).GetFrame(ElapsedTime));

            Shaders->Level.SetMaterial1(cmdList, map1);
        }

        if (chunk.Map2 != TexID::None) {
            consts.Overlay = true;

            auto& map2 = chunk.EffectClip2 == EClipID::None ?
                Materials->Get(chunk.MapID2) :
                Materials->Get(Resources::GetEffectClip(chunk.EffectClip2).GetFrame(ElapsedTime));

            Shaders->Level.SetMaterial2(cmdList, map2);
        }

        auto& ti = Resources::GetLevelTextureInfo(chunk.MapID1);
        consts.ScrollU = ti.SlideU;
        consts.ScrollV = ti.SlideV;
        consts.Distort = ti.SlideU != 0 || ti.SlideV != 0;

        Shaders->Level.SetInstanceConstants(cmdList, consts);
        mesh.Draw(cmdList);
        DrawCalls++;
    }

    FontAtlas Atlas(1024, 512);

    // Loads fonts from the d2 hog file as they are higher resolution
    void LoadFonts() {
        auto hogPath = FileSystem::TryFindFile("descent2.hog");
        if (!hogPath) return;

        auto hog = HogFile::Read(*hogPath);

        // Only load high res fonts. Ordered from small to large to simplify atlas code.
        const Tuple<string, FontSize> fonts[] = {
            { "font3-1h.fnt", FontSize::Small },
            { "font2-1h.fnt", FontSize::Medium },
            { "font2-2h.fnt", FontSize::MediumGold },
            { "font2-3h.fnt", FontSize::MediumBlue },
            { "font1-1h.fnt", FontSize::Big }
        };

        List<Palette::Color> buffer(Atlas.Width() * Atlas.Height());
        std::fill(buffer.begin(), buffer.end(), Palette::Color{ 0, 0, 0, 0 });

        for (auto& [f, sz] : fonts) {
            if (!hog.Exists(f)) continue;
            auto data = hog.ReadEntry(f);
            auto font = Font::Read(data);
            Atlas.AddFont(buffer, font, sz, 2);
        }

        auto batch = BeginTextureUpload();
        StaticTextures->Font.Load(batch, buffer.data(), Atlas.Width(), Atlas.Height(), L"Font");
        StaticTextures->Font.AddShaderResourceView();
        EndTextureUpload(batch);
    }

    // todo: alignment, wrapping
    void DrawString(string_view str, float x, float y, FontSize size) {
        float xOffset = 0;
        uint32 color = 0xFFFFFFFF;
        auto font = Atlas.GetFont(size);
        if (!font) return;

        for (int i = 0; i < str.size(); i++) {
            auto c = str[i];
            char next = i + 1 >= str.size() ? 0 : str[i + 1];
            auto& ci = Atlas.GetCharacter(c, size);
            auto width = font->GetWidth(c);
            auto x0 = xOffset + x;
            auto x1 = xOffset + x + width;
            auto y0 = y;
            auto y1 = y + font->Height;

            Render::DrawQuadPayload payload{};
            payload.V0 = { Vector2{ x0, y1 }, { ci.X0, ci.Y1 }, color }; // bottom left
            payload.V1 = { Vector2{ x1, y1 }, { ci.X1, ci.Y1 }, color }; // bottom right
            payload.V2 = { Vector2{ x1, y0 }, { ci.X1, ci.Y0 }, color }; // top right
            payload.V3 = { Vector2{ x0, y0 }, { ci.X0, ci.Y0 }, color }; // top left
            payload.Texture = &StaticTextures->Font;
            Render::DrawQuad2D(payload);

            auto kerning = Atlas.GetKerning(c, next, size);
            xOffset += width + kerning;
        }
    }

    Vector2 MeasureString(string_view str, FontSize size) {
        float width = 0;
        auto font = Atlas.GetFont(size);
        if (!font) return {};

        for (int i = 0; i < str.size(); i++) {
            char next = i + 1 >= str.size() ? 0 : str[i + 1];
            auto kerning = Atlas.GetKerning(str[i], next, size);
            width += font->GetWidth(str[i]) + kerning;
        }

        return { width, (float)font->Height };
    }

    void DrawCenteredString(string_view str, float x, float y, FontSize size) {
        auto font = Atlas.GetFont(size);
        if (!font) return;
        auto [width, height] = MeasureString(str, size);
        DrawString(str, x - (width / 2), y - (height / 2), size);
    }

    // Initialize device dependent objects here (independent of window size).
    void CreateDeviceDependentResources() {
        Shaders = MakePtr<ShaderResources>();
        Effects = MakePtr<EffectResources>(Shaders.get());
        Materials = MakePtr<MaterialLibrary>(3000);

        _spriteBatch = MakePtr<PrimitiveBatch<ObjectVertex>>(Device);
        _canvasBatch = MakePtr<PrimitiveBatch<CanvasVertex>>(Device);
        _graphicsMemory = MakePtr<GraphicsMemory>(Device);
        Bloom = MakePtr<PostFx::Bloom>();

        Debug::Initialize();

        ImGuiBatch::Initialize(_hwnd, (float)Settings::FontSize);
        static_assert(sizeof(ImTextureID) >= sizeof(D3D12_CPU_DESCRIPTOR_HANDLE), "D3D12_CPU_DESCRIPTOR_HANDLE is too large to fit in an ImTextureID");
        g_ImGuiBatch = MakePtr<ImGuiBatch>(Adapter->GetBackBufferCount());

        CreateEditorResources();
        LoadFonts();

        ResourceUploadBatch resourceUpload(Device);

        resourceUpload.Begin();

        {
            RenderTargetState rtState(Adapter->GetBackBufferFormat(), Adapter->SceneDepthBuffer.GetFormat());
            SpriteBatchPipelineStateDescription pd(rtState);
            _tempBatch = MakePtr<SpriteBatch>(Device, resourceUpload, pd);
        }

        auto task = resourceUpload.End(Adapter->GetCommandQueue());
        task.wait();
    }

    void CreateWindowSizeDependentResources(int width, int height) {
        Bloom->Create(width, height);
    }

    // Set the viewport and scissor rect.
    void SetRenderTarget(ID3D12GraphicsCommandList* commandList, RenderTarget& target, Inferno::DepthBuffer* depthBuffer = nullptr) {
        target.SetAsRenderTarget(commandList, depthBuffer);

        auto width = target.GetWidth();
        auto height = target.GetHeight();

        D3D12_RECT scissor = {};
        scissor.right = (LONG)width;
        scissor.bottom = (LONG)height;

        D3D12_VIEWPORT viewport = {};
        viewport.Width = (float)width;
        viewport.Height = (float)height;
        viewport.MinDepth = D3D12_MIN_DEPTH;
        viewport.MaxDepth = D3D12_MAX_DEPTH;

        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissor);
    }

    void Clear() {
        auto cmdList = Adapter->GetCommandList();
        PIXBeginEvent(cmdList, PIX_COLOR_DEFAULT, L"Clear");

        auto target = Adapter->GetHdrRenderTarget();
        auto depthBuffer = Adapter->GetHdrDepthBuffer();
        auto baseRtv = target->GetRTV();
        auto dsv = depthBuffer->GetDSV();

        //D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {
        //    baseRtv, emissiveBuffer->GetRTV()
        //};

        //cmdList->OMSetRenderTargets(2, rtvs, false, &dsv);

        cmdList->OMSetRenderTargets(1, &baseRtv, true, &dsv);
        //target->SetAsRenderTarget(cmdList, depthBuffer);
        target->Transition(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
        target->Clear(cmdList);
        depthBuffer->Clear(cmdList);

        auto width = target->GetWidth();
        auto height = target->GetHeight();

        D3D12_RECT scissor = {};
        scissor.right = (LONG)width;
        scissor.bottom = (LONG)height;

        D3D12_VIEWPORT viewport = {};
        viewport.Width = (float)width;
        viewport.Height = (float)height;
        viewport.MinDepth = D3D12_MIN_DEPTH;
        viewport.MaxDepth = D3D12_MAX_DEPTH;

        cmdList->RSSetViewports(1, &viewport);
        cmdList->RSSetScissorRects(1, &scissor);

        auto output = Adapter->GetOutputSize();
        Camera.SetViewport((float)output.right, (float)output.bottom);
        Camera.LookAtPerspective();
        ViewProjection = Camera.ViewProj();
        CameraFrustum = Camera.GetFrustum();

        PIXEndEvent(cmdList);
    }

    //List<LevelTexID> PendingTextures;
    //List<ModelID> PendingModels;

    void Initialize(HWND hwnd, int width, int height) {
        assert(hwnd);
        _hwnd = hwnd;
        Adapter = MakePtr<DeviceResources>(BackBufferFormat);
        StaticTextures = MakePtr<StaticTextureDef>();
        Adapter->SetWindow(hwnd, width, height);
        Adapter->CreateDeviceResources();
        Render::Heaps = MakePtr<DescriptorHeaps>(20000, 100, 10);
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
        Render::Heaps.reset();
        StaticTextures.reset();
        Effects.reset();
        Shaders.reset();
        _graphicsMemory.reset();
        _spriteBatch.reset();
        _canvasBatch.reset();
        g_ImGuiBatch.reset();

        ReleaseEditorResources();
        _levelMeshBuffer.reset();
        _meshBuffer.reset();

        Adapter.reset();
        Bloom.reset();
        _tempBatch.reset();
        Debug::Shutdown();
        DeviceResources::ReportLiveObjects();
        Device = nullptr;

        //#if defined(_DEBUG)
        //        ID3D12DebugDevice* debugInterface;
        //        ThrowIfFailed(device->QueryInterface(&debugInterface));
        //        debugInterface->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
        //        debugInterface->Release();
        //#endif
                //device->Release();
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
        List<TexID> list = { Resources::LookupLevelTexID(id) };
        if (auto eclip = Resources::TryGetEffectClip(id))
            Seq::append(eclip->VClip.GetFrames(), list);
        Materials->LoadMaterials(list, false);
    }

    void LoadTextureDynamic(VClipID id) {
        auto& vclip = Resources::GetVideoClip(id);
        Materials->LoadMaterials(vclip.GetFrames(), false);
    }

    void CreateMatcenEffects(const Level& level) {
        MatcenEffects.clear();

        for (auto& seg : level.Segments) {
            if (seg.Type == SegmentType::Matcen) {
                const auto& top = seg.GetSide(SideID::Top).Center;
                const auto& bottom = seg.GetSide(SideID::Bottom).Center;

                Object vfx{};
                auto up = top - bottom;
                vfx.Type = ObjectType::Fireball;
                vfx.Radius = up.Length() / 2;
                up.Normalize();
                vfx.Transform.Translation(seg.Center);
                vfx.Transform.Up(up);
                vfx.Render.Type = RenderType::Fireball;
                vfx.Render.VClip.ID = VClips::Matcen;
                MatcenEffects.push_back(vfx);
            }
        }
    }

    void LoadLevel(Level& level) {
        Adapter->WaitForGpu();

        SPDLOG_INFO("Load models");
        // Load models for objects in the level
        _meshBuffer = MakePtr<MeshBuffer>(Resources::GameData.Models.size());

        List<ModelID> modelIds;
        for (auto& model : level.Objects)
            if (model.Render.Type == RenderType::Polyobj)
                _meshBuffer->LoadModel(model.Render.Model.ID);

        _levelMeshBuilder.Update(level, *_levelMeshBuffer);
        CreateMatcenEffects(level);
    }

    Dictionary<Texture2D*, List<DrawQuadPayload>> CanvasCommands;

    void DrawQuad2D(DrawQuadPayload& payload) {
        // add to batch based on texture id
        // batch renders between imgui and 3d layer
        if (!payload.Texture) return;
        CanvasCommands[payload.Texture].push_back(payload);
    }

    void DrawObject(double alpha, const Object& object, ID3D12GraphicsCommandList* cmd) {
        switch (object.Type) {
            case ObjectType::Robot:
            {
                auto& info = Resources::GetRobotInfo(object.ID);
                auto texOverride = Resources::LookupLevelTexID(object.Render.Model.TextureOverride);
                DrawModel(alpha, object, cmd, info.Model, texOverride);
                break;
            }

            case ObjectType::Hostage:
                DrawSprite(object, cmd, true);
                break;

            case ObjectType::Coop:
            case ObjectType::Player:
            case ObjectType::Reactor:
            case ObjectType::SecretExitReturn:
            case ObjectType::Marker:
            {
                auto texOverride = Resources::LookupLevelTexID(object.Render.Model.TextureOverride);
                DrawModel(alpha, object, cmd, object.Render.Model.ID, texOverride);
                break;
            }

            case ObjectType::Weapon:
                if (object.Render.Type == RenderType::Polyobj) {
                    auto texOverride = Resources::LookupLevelTexID(object.Render.Model.TextureOverride);
                    DrawModel(alpha, object, cmd, object.Render.Model.ID, texOverride);
                }
                else {
                    DrawSprite(object, cmd, false);
                }
                break;

            case ObjectType::Fireball:
            {
                bool axisAligned = object.Render.VClip.ID == VClips::Matcen;
                DrawSprite(object, cmd, axisAligned);
                break;
            }

            case ObjectType::Powerup:
            {
                DrawSprite(object, cmd);
                break;
            }

            case ObjectType::Debris:
                break;
            case ObjectType::Clutter:
                break;
            default:
                break;
        }
    }

    void DrawBatchedText(ID3D12GraphicsCommandList* cmdList) {
        // draw batched text
        auto orthoProj = Matrix::CreateOrthographicOffCenter(0, (float)GetWidth(), (float)GetHeight(), 0.0, 0.0, -2.0f);

        Effects->UserInterface.Apply(cmdList);
        Shaders->UserInterface.SetWorldViewProjection(cmdList, orthoProj);
        for (auto& [texture, group] : CanvasCommands) {
            Shaders->UserInterface.SetDiffuse(cmdList, texture->GetSRV());
            _canvasBatch->Begin(cmdList);
            for (auto& c : group)
                _canvasBatch->DrawQuad(c.V0, c.V1, c.V2, c.V3);

            _canvasBatch->End();
            group.clear();
        }
    }

    IEffect* _activeEffect;

    void ExecuteRenderCommand(double alpha, ID3D12GraphicsCommandList* cmdList, const RenderCommand& cmd) {
        switch (cmd.Type) {
            case RenderCommandType::LevelMesh:
            {
                auto& mesh = *cmd.Data.LevelMesh;

                LevelShader::Constants consts = {};
                consts.WVP = ViewProjection;
                consts.Eye = Camera.Position;
                consts.LightDirection = -Vector3::UnitY;

                if (Settings::RenderMode == RenderMode::Flat) {
                    if (mesh.Chunk->Blend == BlendMode::Alpha || mesh.Chunk->Blend == BlendMode::Additive)
                        Effects->LevelWallFlat.Apply(cmdList);
                    else
                        Effects->LevelFlat.Apply(cmdList);
                }
                else {
                    if (mesh.Chunk->Blend == BlendMode::Alpha)
                        Effects->LevelWall.Apply(cmdList);
                    else if (mesh.Chunk->Blend == BlendMode::Additive)
                        Effects->LevelWallAdditive.Apply(cmdList);
                    else
                        Effects->Level.Apply(cmdList); // effect must be applied before setting any shader parameters
                }

                Effects->Level.Shader->SetConstants(cmdList, consts);
                Effects->Level.Shader->SetSampler(cmdList, GetTextureSampler());

                DrawLevelMesh(cmdList, *cmd.Data.LevelMesh);
                break;
            }
            case RenderCommandType::Object:
                DrawObject(alpha, *cmd.Data.Object, cmdList);
                break;
        }
    }

    void ChangeLight(Level& level, const LightDeltaIndex& index, float multiplier = 1.0f) {
        for (int j = 0; j < index.Count; j++) {
            auto& dlp = level.LightDeltas[index.Index + j];

            for (int k = 0; k < 4; k++) {
                assert(level.SegmentExists(dlp.Tag));
                auto& side = level.GetSide(dlp.Tag);
                side.Light[k] += dlp.Color[k] * multiplier;
                ClampColor(side.Light[k], 0.0f, 5.0f);
            }
        }

        Render::LevelChanged = true;
    }

    void SubtractLight(Level& level, Tag light, Segment& seg) {
        auto index = level.GetLightDeltaIndex(light);
        if (!index) return;

        if (seg.LightIsSubtracted(light.Side))
            return;

        seg.LightSubtracted |= (1 << (int)light.Side);
        ChangeLight(level, *index, -1);
    }

    void AddLight(Level& level, Tag light, Segment& seg) {
        auto index = level.GetLightDeltaIndex(light);
        if (!index) return;

        if (!seg.LightIsSubtracted(light.Side))
            return;

        seg.LightSubtracted &= ~(1 << (int)light.Side);
        ChangeLight(level, *index, 1);
    }

    void ToggleLight(Level& level, Tag light) {
        auto index = level.GetLightDeltaIndex(light);
        if (!index) return;

        auto& seg = level.GetSegment(light);
        if (seg.LightSubtracted & (1 << (int)light.Side)) {
            AddLight(level, light, seg);
        }
        else {
            SubtractLight(level, light, seg);
        }
    }

    void FlickerLights(Level& level) {
        for (auto& light : level.FlickeringLights) {
            auto& seg = level.GetSegment(light.Tag);

            if (seg.SideHasConnection(light.Tag.Side) && !seg.SideIsWall(light.Tag.Side))
                continue;

            if (light.Timer == FLT_MAX || light.Delay <= 0.001f)
                continue; // disabled

            light.Timer -= FrameTime;

            if (light.Timer < 0) {
                while (light.Timer < 0) light.Timer += light.Delay;

                auto bit = 32 - (int)std::floor(ElapsedTime / light.Delay) % 32;

                if ((light.Mask >> bit) & 0x1) // shift to the bit and test it
                    AddLight(level, light.Tag, seg);
                else
                    SubtractLight(level, light.Tag, seg);
            }
        }
    }

    // Queues draw commands for the level
    void DrawLevel() {
        for (auto& mesh : _levelMeshBuilder.GetMeshes())
            DrawOpaque({ &mesh, 0 });

        for (auto& mesh : _levelMeshBuilder.GetWallMeshes()) {
            float depth = (mesh.Chunk->Center - Camera.Position).LengthSquared();
            //Debug::DrawPoint(mesh.Chunk->Center, { 0, 1, 1 });
            DrawTransparent({ &mesh, depth });
        }
    }

    void DrawObject(Level& level, Object& obj, float distSquared, double alpha) {
        auto position = obj.Position(alpha);

        BoundingSphere bounds(position, obj.Radius); // might should use GetBoundingSphere
        if (!CameraFrustum.Contains(bounds))
            return;

        if (auto seg = level.TryGetSegment(obj.Segment)) {
            auto vec = position - seg->Center;
            vec.Normalize();
            position = seg->Center + vec; // Shift slightly away from center so objects within seg are sorted correctly
        }

        // shift depth closer to camera to draw them after walls.
        // Flat value is for nearby objects and multiplier is for distant ones
        float depth = (position - Camera.Position).LengthSquared() * 0.98f - 100;

        if (depth > distSquared)
            DrawObjectOutline(obj);
        else
            DrawTransparent({ &obj, depth });
    }

    void DrawDebug(Level& level) {
        Debug::DrawPoint(Inferno::Debug::ClosestPoint, Color(1, 0, 0));
    }

    void Present(double alpha) {
        //SPDLOG_INFO("Begin Frame");
        Metrics::BeginFrame();
        ScopedTimer presentTimer(&Metrics::Present);
        DrawCalls = 0;
        PolygonCount = 0;
        CameraFrustum = Camera.GetFrustum();

        if (Settings::ShowFlickeringLights)
            FlickerLights(Game::Level);

        if (LevelChanged) {
            Adapter->WaitForGpu();
            _levelMeshBuilder.Update(Game::Level, *_levelMeshBuffer);
            CreateMatcenEffects(Game::Level);
            LevelChanged = false;
        }

        // Prepare the command list to render a new frame.
        Adapter->Prepare();
        Clear();

        auto cmdList = Adapter->GetCommandList();
        PIXBeginEvent(cmdList, PIX_COLOR_DEFAULT, L"Render");
        Heaps->SetDescriptorHeaps(cmdList);

        Debug::BeginFrame();

        ScopedTimer levelTimer(&Metrics::QueueLevel);
        if (Settings::RenderMode != RenderMode::None)
            DrawLevel();

        if (Settings::ShowObjects) {
            auto distSquared = Settings::ObjectRenderDistance * Settings::ObjectRenderDistance;
            for (auto& obj : Game::Level.Objects)
                DrawObject(Game::Level, obj, distSquared, alpha);
        }

        if (Settings::ShowMatcenEffects) {
            auto distSquared = Settings::ObjectRenderDistance * Settings::ObjectRenderDistance;
            for (auto& effect : MatcenEffects)
                DrawObject(Game::Level, effect, distSquared, alpha);
        }

        {
            ScopedTimer execTimer(&Metrics::ExecuteRenderCommands);
            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            for (auto& cmd : _opaqueQueue)
                ExecuteRenderCommand(alpha, cmdList, cmd);

            Seq::sortBy(_transparentQueue, [](const RenderCommand& l, const RenderCommand& r) {
                return l.Depth > r.Depth;
            });

            for (auto& cmd : _transparentQueue)
                ExecuteRenderCommand(alpha, cmdList, cmd);

            // Draw heat volumes
            //    _levelResources->Volumes.Draw(cmdList);

            DrawEditor(cmdList, Game::Level);
            DrawDebug(Game::Level);

            Debug::EndFrame(cmdList);
        }


        if (Settings::MsaaSamples > 1) {
            Adapter->SceneColorBuffer.ResolveFromMultisample(cmdList, Adapter->MsaaColorBuffer);
        }

        // Post process
        auto backBuffer = Adapter->GetBackBuffer();
        SetRenderTarget(cmdList, *backBuffer);

        Adapter->SceneColorBuffer.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        if (Settings::EnableBloom && Adapter->TypedUAVLoadSupport_R11G11B10_FLOAT())
            Bloom->Apply(cmdList, Adapter->SceneColorBuffer);

        // draw to backbuffer using a shader + polygon
        _tempBatch->SetViewport(Adapter->GetScreenViewport());
        _tempBatch->Begin(cmdList);
        XMUINT2 size = { GetWidth(), GetHeight() };
        _tempBatch->Draw(Adapter->SceneColorBuffer.GetSRV(), size, XMFLOAT2{ 0, 0 });
        //if (DebugEmissive)
        //    draw with shader that subtracts 1 from all values;

        _tempBatch->End();

        {
            ScopedTimer imguiTimer(&Metrics::ImGui);
            DrawBatchedText(cmdList);
            // Imgui batch modifies render state greatly. Normal geometry will likely not render correctly afterwards.
            g_ImGuiBatch->Render(cmdList);
        }

        PIXEndEvent(cmdList);

        auto commandQueue = Adapter->GetCommandQueue();
        {
            ScopedTimer presentCallTimer(&Metrics::PresentCall);
            // Show the new frame.
            //SPDLOG_INFO("Present");
            PIXBeginEvent(commandQueue, PIX_COLOR_DEFAULT, L"Present");
            Adapter->Present();
            PIXEndEvent(commandQueue);
        }
        Materials->Dispatch();
        _graphicsMemory->Commit(commandQueue);
        _opaqueQueue.clear();
        _transparentQueue.clear();
    }

    void ReloadTextures() {
        Materials->Reload();
    }
}
