#include "pch.h"
#include "Render.Level.h"

#include "Game.h"
#include "Game.Segment.h"
#include "Render.Debug.h"
#include "Render.Editor.h"
#include "Render.h"
#include "Render.Queue.h"
#include "Resources.h"
#include "ScopedTimer.h"
#include "ShaderLibrary.h"
#include "Object.h"
#include "DirectX.h"
#include "Game.Wall.h"
#include "MaterialLibrary.h"
#include "Physics.h"
#include "Render.Object.h"
#include "Shell.h"
#include "Procedural.h"
#include "SoundSystem.h"

namespace Inferno::Render {
    using Graphics::GraphicsContext;

    namespace {
        RenderQueue _renderQueue;
        LevelMeshBuilder _levelMeshBuilder;
    }

    bool SideIsDoor(const SegmentSide* side) {
        if (!side) return false;
        if (auto wall = Game::Level.TryGetWall(side->Wall)) {
            return wall->Type == WallType::Door || wall->Type == WallType::Destroyable;
        }
        return false;
    }

    ProceduralTextureBase* GetLevelProcedural(LevelTexID id) {
        if (!Settings::Graphics.EnableProcedurals) return nullptr;
        return GetProcedural(Resources::LookupTexID(id));
    }

    void LevelDepthCutout(ID3D12GraphicsCommandList* cmdList, const RenderCommand& cmd) {
        assert(cmd.Type == RenderCommandType::LevelMesh);
        auto& mesh = *cmd.Data.LevelMesh;
        if (!mesh.Chunk) return;
        auto& chunk = *mesh.Chunk;
        if (chunk.Blend == BlendMode::Additive) return;

        DepthCutoutShader::Constants constants{};
        constants.Threshold = 0.01f;
        constants.HasOverlay = chunk.TMap2 > LevelTexID::Unset;

        auto& effect = Effects->DepthCutout;
        effect.Apply(cmdList);
        effect.Shader->SetSampler(cmdList, GetWrappedTextureSampler());
        effect.Shader->SetTextureTable(cmdList, Render::Heaps->Materials.GetGpuHandle(0));

        auto side = Game::Level.TryGetSide(chunk.Tag);

        // Same as level mesh texid lookup
        if (SideIsDoor(side)) {
            // Use the current texture for this side, as walls are drawn individually
            effect.Shader->SetDiffuse1(cmdList, Materials->Get(side->TMap).Handle());
            if (constants.HasOverlay) {
                auto& tmap2 = Materials->Get(side->TMap2);
                effect.Shader->SetDiffuse2(cmdList, tmap2.Handle());
                effect.Shader->SetSuperTransparent(cmdList, tmap2);
            }
        }
        else {
            if (auto proc = GetLevelProcedural(chunk.TMap1)) {
                // For procedural textures the animation is baked into it
                effect.Shader->SetDiffuse1(cmdList, proc->GetHandle());
            }
            else {
                auto& map1 = chunk.EffectClip1 == EClipID::None ? Materials->Get(chunk.TMap1) : Materials->Get(chunk.EffectClip1, ElapsedTime, false);
                effect.Shader->SetDiffuse1(cmdList, map1.Handles[0]);
            }

            if (constants.HasOverlay) {
                if (auto proc = GetLevelProcedural(chunk.TMap2)) {
                    auto& map2 = Materials->Get(chunk.TMap2);
                    effect.Shader->SetDiffuse2(cmdList, proc->GetHandle());
                    effect.Shader->SetSuperTransparent(cmdList, map2);
                }
                else {
                    auto& map2 = chunk.EffectClip2 == EClipID::None ? Materials->Get(chunk.TMap2) : Materials->Get(chunk.EffectClip2, ElapsedTime, Game::ControlCenterDestroyed);
                    effect.Shader->SetDiffuse2(cmdList, map2.Handles[0]);
                    effect.Shader->SetSuperTransparent(cmdList, map2);
                }
            }
        }

        auto& ti = Resources::GetLevelTextureInfo(chunk.TMap1);
        constants.Scroll = ti.Slide;
        constants.Scroll2 = chunk.OverlaySlide;
        effect.Shader->SetConstants(cmdList, constants);

        mesh.Draw(cmdList);
        Stats::DrawCalls++;
    }

    void ClearDepthPrepass(const Graphics::GraphicsContext& ctx) {
        auto& target = Adapter->GetHdrRenderTarget();
        auto& depthBuffer = Adapter->GetHdrDepthBuffer();
        auto& linearDepthBuffer = Adapter->GetLinearDepthBuffer();
        //D3D12_CPU_DESCRIPTOR_HANDLE targets[] = {
        //    linearDepthBuffer.GetRTV(),
        //    linearDepthBuffer.GetRTV()
        //};

        ctx.SetRenderTarget(linearDepthBuffer.GetRTV(), depthBuffer.GetDSV());
        ctx.ClearColor(target);
        ctx.ClearDepth(depthBuffer);
        ctx.ClearColor(linearDepthBuffer);
        ctx.SetViewportAndScissor((UINT)target.GetWidth(), (UINT)target.GetHeight());
        linearDepthBuffer.Transition(ctx.GetCommandList(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    void DepthPrepass(GraphicsContext& ctx) {
        ctx.BeginEvent(L"Depth prepass");
        // Depth prepass
        ClearDepthPrepass(ctx);
        auto cmdList = ctx.GetCommandList();

        // Opaque geometry prepass
        for (auto& cmd : _renderQueue.Opaque()) {
            switch (cmd.Type) {
                case RenderCommandType::LevelMesh:
                    ctx.ApplyEffect(Effects->Depth);
                    ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
                    cmd.Data.LevelMesh->Draw(cmdList);
                    Stats::DrawCalls++;
                    break;

                case RenderCommandType::Object:
                {
                    // Models
                    auto& object = *cmd.Data.Object;
                    if (object.Render.Type != RenderType::Model) continue;
                    auto model = object.Render.Model.ID;

                    if (object.Render.Model.Outrage) {
                        ctx.ApplyEffect(Effects->DepthObject);
                        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
                        OutrageModelDepthPrepass(ctx, object);
                    }
                    else {
                        if (cmd.Data.Object->Type == ObjectType::Robot)
                            model = Resources::GetRobotInfo(object.ID).Model;

                        auto& effect = Effects->DepthObject;

                        // todo: fix bug with this causing *all* objects to be rendered as flipped after firing lasers
                        //if (object.Type == ObjectType::Weapon) {
                        //    // Flip outer model of weapons with inner models so the Z buffer will allow drawing them
                        //    auto inner = Resources::GameData.Weapons[object.ID].ModelInner;
                        //    if (inner > ModelID::None && inner != ModelID(255))
                        //        effect = Effects->DepthObjectFlipped;
                        //}

                        ctx.ApplyEffect(effect);
                        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
                        ModelDepthPrepass(cmdList, object, model);
                    }

                    break;
                }

                case RenderCommandType::Effect:
                {
                    cmd.Data.Effect->DepthPrepass(ctx);
                    break;
                }

                default:
                    throw Exception("Render command not supported in depth prepass");
            }
        }

        if (Settings::Editor.RenderMode != RenderMode::Flat) {
            // Level walls (potentially transparent)
            ctx.ApplyEffect(Effects->DepthCutout);
            ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());

            for (auto& cmd : _renderQueue.Transparent()) {
                if (cmd.Type != RenderCommandType::LevelMesh) continue;
                LevelDepthCutout(cmdList, cmd);
            }
        }

        if (Settings::Graphics.MsaaSamples > 1) {
            // must resolve MS target to allow shader sampling
            Adapter->LinearizedDepthBuffer.ResolveFromMultisample(cmdList, Adapter->MsaaLinearizedDepthBuffer);
            Adapter->MsaaLinearizedDepthBuffer.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }

        Adapter->LinearizedDepthBuffer.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Adapter->GetHdrDepthBuffer().Transition(cmdList, D3D12_RESOURCE_STATE_DEPTH_READ);
        ctx.EndEvent();
    }

    void DrawLevelMesh(const GraphicsContext& ctx, const Inferno::LevelMesh& mesh) {
        if (!mesh.Chunk) return;
        auto& chunk = *mesh.Chunk;

        LevelShader::InstanceConstants constants{};
        constants.LightingScale = Settings::Editor.RenderMode == RenderMode::Shaded ? 1.0f : 0.0f; // How much light to apply

        auto cmdList = ctx.GetCommandList();
        Shaders->Level.SetDepthTexture(cmdList, Adapter->LinearizedDepthBuffer.GetSRV());
        Shaders->Level.SetMaterialInfoBuffer(cmdList, MaterialInfoBuffer->GetSRV());
        Shaders->Level.SetTextureTable(cmdList, Render::Heaps->Materials.GetGpuHandle(0));

        auto& ti = Resources::GetLevelTextureInfo(chunk.TMap1);

        if (chunk.Cloaked) {
            // todo: cloaked walls will have to be rendered with a different shader -> prefer glass / distortion
            Shaders->Level.SetMaterial1(cmdList, Materials->Black());
            Shaders->Level.SetMaterial2(cmdList, Materials->Black());
            constants.LightingScale = 1;
        }
        else {
            constants.Overlay = chunk.TMap2 > LevelTexID::Unset;

            // Only walls have tags
            auto side = Game::Level.TryGetSide(chunk.Tag);

            if (SideIsDoor(side)) {
                // Use the current texture for this side, as walls are drawn individually
                auto& map1 = Materials->Get(side->TMap);
                Shaders->Level.SetDiffuse1(cmdList, map1.Handles[0]);
                Shaders->Level.SetMaterial1(cmdList, map1);

                if (constants.Overlay) {
                    auto& map2 = Materials->Get(side->TMap2);
                    Shaders->Level.SetDiffuse2(cmdList, map2.Handles[0]);
                    Shaders->Level.SetMaterial2(cmdList, map2);
                }
            }
            else {
                if (auto proc = GetLevelProcedural(chunk.TMap1)) {
                    // For procedural textures the animation is baked into it
                    auto& map1 = Materials->Get(chunk.TMap1);
                    Shaders->Level.SetDiffuse1(cmdList, proc->GetHandle());
                    Shaders->Level.SetMaterial1(cmdList, map1);
                }
                else {
                    auto& map1 = chunk.EffectClip1 == EClipID::None ? Materials->Get(chunk.TMap1) : Materials->Get(chunk.EffectClip1, ElapsedTime, false);
                    Shaders->Level.SetDiffuse1(cmdList, map1.Handles[0]);
                    Shaders->Level.SetMaterial1(cmdList, map1);
                }

                if (constants.Overlay) {
                    if (auto proc = GetLevelProcedural(chunk.TMap2)) {
                        auto& map2 = Materials->Get(chunk.TMap2);
                        Shaders->Level.SetDiffuse2(cmdList, proc->GetHandle());
                        Shaders->Level.SetMaterial2(cmdList, map2);
                    }
                    else {
                        auto& map2 = chunk.EffectClip2 == EClipID::None ? Materials->Get(chunk.TMap2) : Materials->Get(chunk.EffectClip2, ElapsedTime, Game::ControlCenterDestroyed);
                        Shaders->Level.SetDiffuse2(cmdList, map2.Handles[0]);
                        Shaders->Level.SetMaterial2(cmdList, map2);
                    }
                }
            }
        }

        constants.Scroll = ti.Slide;
        constants.Scroll2 = chunk.OverlaySlide;
        constants.Distort = ti.Slide != Vector2::Zero;
        constants.Tex1 = (int)ti.TexID;

        if (chunk.TMap2 > LevelTexID::Unset) {
            auto tid2 = Resources::LookupTexID(chunk.TMap2);
            constants.Tex2 = (int)tid2;
        }
        else {
            constants.Tex2 = -1;
        }

        Shaders->Level.SetInstanceConstants(cmdList, constants);
        Shaders->Level.SetLightGrid(cmdList, *Render::LightGrid);
        mesh.Draw(cmdList);
        Stats::DrawCalls++;
    }

    void ExecuteRenderCommand(GraphicsContext& ctx, const RenderCommand& cmd, RenderPass pass) {
        switch (cmd.Type) {
            case RenderCommandType::LevelMesh:
            {
                auto& mesh = *cmd.Data.LevelMesh;

                if (Settings::Editor.RenderMode == RenderMode::Flat) {
                    if (mesh.Chunk->Blend == BlendMode::Alpha || mesh.Chunk->Blend == BlendMode::Additive) {
                        if (pass != RenderPass::Walls) return;
                        ctx.ApplyEffect(Effects->LevelWallFlat);
                    }
                    else {
                        if (pass != RenderPass::Opaque) return;
                        ctx.ApplyEffect(Effects->LevelFlat);
                    }

                    ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
                    cmd.Data.LevelMesh->Draw(ctx.GetCommandList());
                    Stats::DrawCalls++;
                }
                else {
                    if (mesh.Chunk->Blend == BlendMode::Alpha) {
                        if (pass != RenderPass::Walls) return;
                        ctx.ApplyEffect(Effects->LevelWall);
                    }
                    else if (mesh.Chunk->Blend == BlendMode::Additive) {
                        if (pass != RenderPass::Transparent) return;
                        ctx.ApplyEffect(Effects->LevelWallAdditive);
                    }
                    else {
                        if (pass != RenderPass::Opaque) return;
                        ctx.ApplyEffect(Effects->Level);
                    }

                    ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
                    auto cmdList = ctx.GetCommandList();
                    Shaders->Level.SetSampler(cmdList, GetWrappedTextureSampler());
                    Shaders->Level.SetNormalSampler(cmdList, GetNormalSampler());

                    DrawLevelMesh(ctx, *cmd.Data.LevelMesh);
                }

                break;
            }
            case RenderCommandType::Object:
                DrawObject(ctx, *cmd.Data.Object, pass);
                break;

            case RenderCommandType::Effect:
                if ((pass == RenderPass::Opaque && !cmd.Data.Effect->IsTransparent) ||
                    (pass == RenderPass::Transparent && cmd.Data.Effect->IsTransparent))
                    cmd.Data.Effect->Draw(ctx);
                break;
        }
    }

    void DrawDebug(const Level& level) {
        //Debug::DrawPoint(Inferno::Debug::ClosestPoint, Color(1, 0, 0));
        if (Settings::Editor.EnablePhysics) {
            for (auto& point : Inferno::Debug::ClosestPoints) {
                Debug::DrawPoint(point, Color(1, 0, 0));
            }
        }

        for (auto& emitter : Inferno::Sound::Debug::Emitters) {
            Debug::DrawPoint(emitter, { 0, 1, 0 });
        }

        for (auto& room : level.Rooms) {
            for (auto& node : room.NavNodes) {
                for (auto& conn : node.Connections) {
                    auto& other = room.NavNodes[conn];
                    Debug::DrawLine(node.Position, other.Position, { 1.0f, 0.25f, 0 });
                }
            }
        }
    }

    using namespace Graphics;
    List<LightData> LevelLights;

    void DrawLevel(Graphics::GraphicsContext& ctx, Level& level) {
        if (Settings::Editor.ShowFlickeringLights)
            UpdateFlickeringLights(level, (float)ElapsedTime, FrameTime);

        if (LevelChanged) {
            Adapter->WaitForGpu();
            _levelMeshBuilder.Update(level, *GetLevelMeshBuffer());
            LevelLights = Graphics::GatherLightSources(level);
            LevelChanged = false;
        }

        _renderQueue.Update(level, _levelMeshBuilder.GetMeshes(), _levelMeshBuilder.GetWallMeshes());
        for (auto& id : _renderQueue.GetVisibleRooms()) {
            if (auto room = level.GetRoom(id))
                Debug::OutlineRoom(level, *room, Color(1, 1, 1, 0.5f));
        }


        ctx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        DepthPrepass(ctx);

        // todo: only add visible lights
        for (auto& light : LevelLights) {
            Graphics::Lights.AddLight(light);
        }

        Graphics::Lights.Dispatch(ctx.GetCommandList());

        {
            ctx.BeginEvent(L"Level");
            auto& target = Adapter->GetHdrRenderTarget();
            auto& depthBuffer = Adapter->GetHdrDepthBuffer();
            ctx.SetRenderTarget(target.GetRTV(), depthBuffer.GetDSV());
            ctx.SetViewportAndScissor((UINT)target.GetWidth(), (UINT)target.GetHeight());

            ScopedTimer execTimer(&Metrics::ExecuteRenderCommands);
            LightGrid->SetLightConstants((UINT)target.GetWidth(), (UINT)target.GetHeight());

            ctx.BeginEvent(L"Opaque queue");
            for (auto& cmd : _renderQueue.Opaque())
                ExecuteRenderCommand(ctx, cmd, RenderPass::Opaque);
            ctx.EndEvent();

            ctx.BeginEvent(L"Wall queue");
            for (auto& cmd : _renderQueue.Transparent() | views::reverse)
                ExecuteRenderCommand(ctx, cmd, RenderPass::Walls);
            ctx.EndEvent();

            ctx.BeginEvent(L"Decals");
            DrawDecals(ctx, Render::FrameTime);
            ctx.EndEvent();

            ctx.BeginEvent(L"Transparent queue");
            for (auto& cmd : _renderQueue.Transparent() | views::reverse)
                ExecuteRenderCommand(ctx, cmd, RenderPass::Transparent);
            ctx.EndEvent();

            ctx.EndEvent(); // level

            //for (auto& cmd : _transparentQueue) // draw transparent geometry on models
            //    ExecuteRenderCommand(cmdList, cmd, true);

            // Draw heat volumes
            //    _levelResources->Volumes.Draw(cmdList);

            DrawBeams(ctx);
            Canvas->SetSize(Adapter->GetWidth(), Adapter->GetHeight());

            if (!Settings::Inferno.ScreenshotMode && Game::GetState() == GameState::Editor) {
                ctx.BeginEvent(L"Editor");
                DrawEditor(ctx.GetCommandList(), level);
                DrawDebug(level);
                ctx.EndEvent();
            }
            else {
                //Canvas->DrawGameText(level.Name, 0, 20 * Shell::DpiScale, FontSize::Big, { 1, 1, 1 }, 0.5f, AlignH::Center, AlignV::Top);
                Canvas->DrawGameText("Inferno\nEngine", -10 * Shell::DpiScale, -10 * Shell::DpiScale, FontSize::MediumGold, { 1, 1, 1 }, 0.5f, AlignH::Right, AlignV::Bottom);
            }
        }
    }

    int GetTransparentQueueSize() {
        return (int)_renderQueue.Transparent().size();
    }
}
