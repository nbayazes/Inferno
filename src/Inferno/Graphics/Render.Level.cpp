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
#include "Physics.h"
#include "Render.Object.h"
#include "Shell.h"
#include "OpenSimplex2.h"

namespace Inferno::Render {
    using Graphics::GraphicsContext;

    namespace {
        RenderQueue _renderQueue;
        LevelMeshBuilder _levelMeshBuilder;
    }

    void LevelDepthCutout(ID3D12GraphicsCommandList* cmdList, const RenderCommand& cmd) {
        assert(cmd.Type == RenderCommandType::LevelMesh);
        auto& mesh = *cmd.Data.LevelMesh;
        if (!mesh.Chunk) return;
        auto& chunk = *mesh.Chunk;
        if (chunk.Blend == BlendMode::Additive) return;

        DepthCutoutShader::Constants consts{};
        consts.Threshold = 0.01f;

        auto& effect = Effects->DepthCutout;
        effect.Apply(cmdList);
        effect.Shader->SetSampler(cmdList, GetTextureSampler());

        {
            auto& map1 = chunk.EffectClip1 == EClipID::None ? Materials->Get(chunk.TMap1) : Materials->Get(chunk.EffectClip1, (float)ElapsedTime, Game::ControlCenterDestroyed);

            effect.Shader->SetMaterial1(cmdList, map1);
        }

        if (chunk.TMap2 > LevelTexID::Unset) {
            consts.HasOverlay = true;

            auto& map2 = chunk.EffectClip2 == EClipID::None ? Materials->Get(chunk.TMap2) : Materials->Get(chunk.EffectClip2, (float)ElapsedTime, Game::ControlCenterDestroyed);

            effect.Shader->SetMaterial2(cmdList, map2);
        }

        auto& ti = Resources::GetLevelTextureInfo(chunk.TMap1);
        consts.Scroll = ti.Slide;
        consts.Scroll2 = chunk.OverlaySlide;
        effect.Shader->SetConstants(cmdList, consts);

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
        linearDepthBuffer.Transition(ctx.CommandList(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    void DepthPrepass(GraphicsContext& ctx) {
        ctx.BeginEvent(L"Depth prepass");
        // Depth prepass
        ClearDepthPrepass(ctx);
        auto cmdList = ctx.CommandList();

        // Opaque geometry prepass
        for (auto& cmd : _renderQueue.Opaque()) {
            switch (cmd.Type) {
                case RenderCommandType::LevelMesh:
                    ctx.ApplyEffect(Effects->Depth);
                    ctx.SetConstantBuffer(0, Adapter->FrameConstantsBuffer.GetGPUVirtualAddress());
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
                        ctx.SetConstantBuffer(0, Adapter->FrameConstantsBuffer.GetGPUVirtualAddress());
                        OutrageModelDepthPrepass(ctx, object);
                    }
                    else {
                        if (cmd.Data.Object->Type == ObjectType::Robot)
                            model = Resources::GetRobotInfo(object.ID).Model;

                        auto& effect = Effects->DepthObject;
                        if (object.Type == ObjectType::Weapon) {
                            // Flip outer model of weapons with inner models so the Z buffer will allow drawing them
                            auto inner = Resources::GameData.Weapons[object.ID].ModelInner;
                            if (inner > ModelID::None && inner != ModelID(255))
                                effect = Effects->DepthObjectFlipped;
                        }

                        ctx.ApplyEffect(effect);
                        ctx.SetConstantBuffer(0, Adapter->FrameConstantsBuffer.GetGPUVirtualAddress());
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
            ctx.SetConstantBuffer(0, Adapter->FrameConstantsBuffer.GetGPUVirtualAddress());

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

        auto cmdList = ctx.CommandList();
        Shaders->Level.SetDepthTexture(cmdList, Adapter->LinearizedDepthBuffer.GetSRV());

        if (chunk.Cloaked) {
            Shaders->Level.SetMaterial1(cmdList, Materials->Black);
            Shaders->Level.SetMaterial2(cmdList, Materials->Black);
            constants.LightingScale = 1;
        }
        else {
            {
                auto& map1 = chunk.EffectClip1 == EClipID::None ? Materials->Get(chunk.TMap1) : Materials->Get(Resources::GetEffectClip(chunk.EffectClip1).VClip.GetFrame(ElapsedTime));

                Shaders->Level.SetMaterial1(cmdList, map1);
            }

            if (chunk.TMap2 > LevelTexID::Unset) {
                constants.Overlay = true;

                auto& map2 = chunk.EffectClip2 == EClipID::None ? Materials->Get(chunk.TMap2) : Materials->Get(chunk.EffectClip2, (float)ElapsedTime, Game::ControlCenterDestroyed);

                Shaders->Level.SetMaterial2(cmdList, map2);
            }
        }

        auto& ti = Resources::GetLevelTextureInfo(chunk.TMap1);
        constants.Scroll = ti.Slide;
        constants.Scroll2 = chunk.OverlaySlide;
        constants.Distort = ti.Slide != Vector2::Zero;

        constants.Mat1 = Materials->GetMaterialInfo(ti.TexID);
        if (chunk.TMap2 > LevelTexID::Unset) {
            auto tid2 = Resources::LookupTexID(chunk.TMap2);
            constants.Mat2 = Materials->GetMaterialInfo(tid2);
        }
        else {
            constants.Mat2 = {};
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

                    ctx.SetConstantBuffer(0, Adapter->FrameConstantsBuffer.GetGPUVirtualAddress());
                    cmd.Data.LevelMesh->Draw(ctx.CommandList());
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

                    ctx.SetConstantBuffer(0, Adapter->FrameConstantsBuffer.GetGPUVirtualAddress());
                    auto cmdList = ctx.CommandList();
                    Shaders->Level.SetSampler(cmdList, GetTextureSampler());

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

    void DrawDebug(Level&) {
        //Debug::DrawPoint(Inferno::Debug::ClosestPoint, Color(1, 0, 0));
        if (Settings::Editor.EnablePhysics) {
            for (auto& point : Inferno::Debug::ClosestPoints) {
                Debug::DrawPoint(point, Color(1, 0, 0));
            }
        }

        for (auto& emitter : Inferno::Sound::Debug::Emitters) {
            Debug::DrawPoint(emitter, { 0, 1, 0 });
        }
    }

    void CopyMaterialData() {
        MaterialInfoBuffer->Begin();
        MaterialInfoBuffer->Copy(Materials->GetAllMaterialInfo());
        MaterialInfoBuffer->End();
    }

    List<Graphics::LightData> LevelLights;
    std::array<Graphics::LightData, Graphics::MAX_LIGHTS> LIGHT_BUFFER{};

    void GatherDecalLight(int& lightIndex, DecalInfo& decal) {
        if (lightIndex >= LIGHT_BUFFER.size()) return;
        if (decal.LightRadius <= 0 || decal.Color == Color(0, 0, 0) || !decal.IsAlive()) return;

        auto t = std::clamp((decal.Duration - decal.FadeTime + decal.Elapsed) * 1.5f / decal.FadeTime, 0.0f, 1.0f);
        if (t <= 0) return;

        auto radius = std::lerp(decal.LightRadius, decal.LightRadius * 0.75f, t);
        auto color = Color::Lerp(decal.LightColor, Color(0, 0, 0), t);

        auto& light = LIGHT_BUFFER[lightIndex++];
        light.color = color.ToVector3();
        light.radiusSq = radius * radius;
        light.pos = decal.Position + decal.Normal * 2; // shift light out of surface
        light.type = LightType::Point;
    }

    void UpdateDynamicLights(const Level& level) {
        constexpr auto reserved = Graphics::MAX_LIGHTS - Graphics::RESERVED_LIGHTS;
        for (int i = 0; i < LevelLights.size() && i < reserved; i++) {
            LIGHT_BUFFER[i] = LevelLights[i];
        }

        int lightIndex = reserved;

        for (auto& obj : level.Objects) {
            if (lightIndex >= LIGHT_BUFFER.size()) break;
            if (!obj.IsAlive()) continue;

            auto& light = LIGHT_BUFFER[lightIndex++];
            light.color = obj.LightColor.ToVector3();
            light.radiusSq = obj.LightRadius * obj.LightRadius;
            auto mode = obj.LightMode;

            if (mode == DynamicLightMode::Flicker || mode == DynamicLightMode::FastFlicker) {
                //constexpr float FLICKER_INTERVAL = 15; // hz
                //float interval = std::floor(Render::ElapsedTime * FLICKER_INTERVAL + (float)obj.Signature * 0.1747f) / FLICKER_INTERVAL;
                const float flickerSpeed = mode == DynamicLightMode::Flicker ? 4.0f : 6.0f;
                const float flickerRadius = mode == DynamicLightMode::Flicker ? 0.03f : 0.04f;
                // slightly randomize the radius and brightness on an interval
                auto noise = OpenSimplex2::Noise2((int)obj.Signature, Render::ElapsedTime * flickerSpeed, 0);
                light.radiusSq += light.radiusSq * noise * flickerRadius;

                if (mode == DynamicLightMode::FastFlicker)
                    light.color *= 1 + noise * 0.025f;
            }
            else if (mode == DynamicLightMode::Pulse) {
                light.radiusSq += light.radiusSq * sinf((float)Render::ElapsedTime * 3.14f * 1.25f + (float)obj.Signature * 0.1747f) * 0.125f;
            }
            else if (mode == DynamicLightMode::BigPulse) {
                light.radiusSq += light.radiusSq * sinf((float)Render::ElapsedTime * 3.14f * 1.25f + (float)obj.Signature * 0.1747f) * 0.25f;
            }

            light.pos = obj.GetPosition(Game::LerpAmount);

            if (obj.Type == ObjectType::Weapon && obj.ID == (int)WeaponID::Flare) {
                // shift light position of flares backwards to move outside of walls
                light.pos += obj.Rotation.Backward() * 2.5f;
            }
            light.type = LightType::Point;
        }

        for (auto& decal : GetAdditiveDecals())
            GatherDecalLight(lightIndex, decal);

        //for (auto& decal : GetDecals())
        //    GatherDecalLight(lightIndex, decal);

        for (auto& room : _renderQueue.GetVisibleSegments()) {
            if (lightIndex >= LIGHT_BUFFER.size()) break;

            for (auto& effect : GetEffectsInSegment(room)) {
                if (lightIndex >= LIGHT_BUFFER.size()) break;
                if (effect->LightRadius == 0 || effect->LightColor == Color(0, 0, 0) || !effect->IsAlive()) continue;

                float t = 0;

                // TODO: move fade calc to effect
                const float duration = effect->Duration * 0.75f;
                //if (effect->Elapsed < duration * 0.5f)
                //    t = effect->Elapsed / (duration * 0.5f) ;
                //else
                //    t = (duration - effect->Elapsed) / (duration * 0.5f); // reverse direction

                t = (duration - effect->Elapsed) / duration; // reverse direction
                t = std::clamp(t, 0.0f, 1.0f);

                if (t == 0) continue;

                auto color = Color::Lerp(Color(0, 0, 0), effect->LightColor, t);
                auto radius = effect->LightRadius;

                auto& light = LIGHT_BUFFER[lightIndex++];
                light.color = color.ToVector3();
                light.radiusSq = radius * radius;
                light.pos = effect->Position;
                light.type = LightType::Point;
            }
        }

        for (int i = lightIndex; i < LIGHT_BUFFER.size(); i++) {
            LIGHT_BUFFER[i].radiusSq = 0; // clear remaining lights
        }
    }

    void DrawLevel(Graphics::GraphicsContext& ctx, Level& level) {
        if (Settings::Editor.ShowFlickeringLights)
            UpdateFlickeringLights(level, (float)ElapsedTime, FrameTime);

        if (LevelChanged) {
            Adapter->WaitForGpu();
            _levelMeshBuilder.Update(level, *GetLevelMeshBuffer());
            LevelLights = Graphics::GatherLightSources(level);
            CopyMaterialData();
            LevelChanged = false;
        }

        _renderQueue.Update(level, _levelMeshBuilder.GetMeshes(), _levelMeshBuilder.GetWallMeshes());

        ctx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        DepthPrepass(ctx);

        UpdateDynamicLights(level);
        LightGrid->SetLights(ctx.CommandList(), LIGHT_BUFFER);
        LightGrid->Dispatch(ctx.CommandList(), Adapter->LinearizedDepthBuffer);

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

            DrawDecals(ctx, Render::FrameTime);

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
                DrawEditor(ctx.CommandList(), level);
                DrawDebug(level);
                ctx.EndEvent();
            }
            else {
                //Canvas->DrawGameText(level.Name, 0, 20 * Shell::DpiScale, FontSize::Big, { 1, 1, 1 }, 0.5f, AlignH::Center, AlignV::Top);
                Canvas->DrawGameText("Inferno\nEngine", -10 * Shell::DpiScale, -10 * Shell::DpiScale, FontSize::MediumGold, { 1, 1, 1 }, 0.5f, AlignH::Right, AlignV::Bottom);
            }
            Debug::EndFrame(ctx.CommandList());
        }
    }

    void ResetLightCache() {
        for (auto& light : LIGHT_BUFFER) {
            light.radiusSq = 0;
        }
    }
}
