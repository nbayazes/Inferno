#include "pch.h"
#include "Render.Level.h"
#include "DirectX.h"
#include "Editor/Editor.h"
#include "Game.h"
#include "Game.Segment.h"
#include "LegitProfiler.h"
#include "MaterialLibrary.h"
#include "Object.h"
#include "OpenSimplex2.h"
#include "Physics.h"
#include "Procedural.h"
#include "Render.Debug.h"
#include "Render.Editor.h"
#include "Render.h"
#include "Render.Object.h"
#include "Render.Queue.h"
#include "Resources.h"
#include "ScopedTimer.h"
#include "ShaderLibrary.h"
#include "Shell.h"
#include "SoundSystem.h"

namespace Inferno::Render {
    using namespace Graphics;

    namespace {
        RenderQueue _renderQueue;
        LevelMeshBuilder _levelMeshBuilder;

        List<SegmentLight> SegmentLights;
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
        auto proc = GetProcedural(Resources::LookupTexID(id));
        if (proc && proc->Enabled) return proc;
        return nullptr;
    }

    void AnimateLight(SegmentLight::SideLighting& side, DynamicLightMode mode) {
        const auto hash = ((float)side.Tag.Segment + (float)side.Tag.Side) * 0.1747f;

        side.AnimatedColor = side.Color;
        side.AnimatedRadius = side.Radius;

        if (mode == DynamicLightMode::Flicker || mode == DynamicLightMode::StrongFlicker || mode == DynamicLightMode::WeakFlicker) {
            int index = mode == DynamicLightMode::WeakFlicker ? 0 : mode == DynamicLightMode::Flicker ? 1 : 2;
            float flickerSpeeds[] = { 1.2f, 1.9f, 2.25f };
            float mults[] = { .25f, .4f, .55f };

            auto noise = OpenSimplex2::Noise2((uint)side.Tag.Segment, Render::ElapsedTime * flickerSpeeds[index], hash);
            //const float flickerRadius = lt.mode == DynamicLightMode::Flicker ? 0.05f : (lt.mode == DynamicLightMode::StrongFlicker ? 0.08f : 0.0125f);
            //lt.radius += lt.radius * noise * flickerRadius;
            auto t = 1.0f - abs(noise * noise * noise - .05f) * mults[index] * (Game::ControlCenterDestroyed ? 2 : 1);
            side.AnimatedColor.w *= t;
        }
        else if (mode == DynamicLightMode::Pulse) {
            float t = 1 + sinf((float)Render::ElapsedTime * 3.14f * 1.25f + hash) * 0.125f;
            side.AnimatedRadius *= t;
            side.AnimatedColor.w *= t;
        }
        else if (mode == DynamicLightMode::BigPulse) {
            float t = 1 + sinf((float)Render::ElapsedTime * 3.14f * 1.25f + hash) * 0.25f;
            side.AnimatedRadius *= t;
            side.AnimatedColor.w *= t;
        }

        if (Game::GlobalDimming != 1)
            side.AnimatedColor.w *= Game::GlobalDimming;

        // Copy to each light on this side
        for (auto& light : side.Lights) {
            light.radius = side.AnimatedRadius;
            light.color = side.AnimatedColor;
        }
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
        Adapter->GetGraphicsContext().ApplyEffect(effect);

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
    }

    void ClearDepthPrepass(GraphicsContext& ctx) {
        auto& depthBuffer = Adapter->GetDepthBuffer();
        auto& linearDepthBuffer = Adapter->GetLinearDepthBuffer();
        ctx.ClearDepth(depthBuffer);
        ctx.ClearColor(linearDepthBuffer);
        ctx.ClearStencil(Adapter->GetDepthBuffer(), 0);
        ctx.GetCommandList()->OMSetStencilRef(0);

        linearDepthBuffer.Transition(ctx.GetCommandList(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        ctx.SetRenderTarget(linearDepthBuffer.GetRTV(), depthBuffer.GetDSV());

        auto& target = Adapter->GetRenderTarget();
        ctx.ClearColor(target);
        ctx.SetViewportAndScissor(UINT(target.GetWidth() * Settings::Graphics.RenderScale), UINT(target.GetHeight() * Settings::Graphics.RenderScale));
    }

    void DepthPrepass(GraphicsContext& ctx) {
        auto cmdList = ctx.GetCommandList();
        PIXScopedEvent(cmdList, PIX_COLOR_DEFAULT, "Depth prepass");

        // Depth prepass
        ClearDepthPrepass(ctx);

        if (!Game::Terrain.EscapePath.empty() && Settings::Editor.ShowTerrain) {
            StaticModelDepthPrepass(ctx, Game::Terrain.ExitModel, Game::Terrain.ExitTransform);
            //auto dsv = Adapter->GetDepthBuffer().GetDSV();
            //cmdList->OMSetRenderTargets(0, nullptr, false, &dsv);

            cmdList->OMSetStencilRef(1);
            ctx.ApplyEffect(Effects->TerrainPortal);
            ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
            _levelMeshBuilder.GetExitPortal().Draw(cmdList); // Mask the exit portal to 1
            //ctx.SetRenderTarget(Adapter->GetLinearDepthBuffer().GetRTV(), Adapter->GetDepthBuffer().GetDSV());
        }

        //Adapter->LinearizedDepthBuffer.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        //Adapter->GetDepthBuffer().Transition(cmdList, D3D12_RESOURCE_STATE_DEPTH_READ);
        //return;

        // Opaque geometry prepass
        for (auto& cmd : _renderQueue.Opaque()) {
            switch (cmd.Type) {
                case RenderCommandType::LevelMesh:
                    ctx.ApplyEffect(Effects->Depth);
                    ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
                    cmd.Data.LevelMesh->Draw(cmdList);
                    break;

                case RenderCommandType::Object:
                {
                    // Models
                    auto& object = *cmd.Data.Object;
                    if (object.Render.Type == RenderType::Model) {
                        if (object.IsCloaked() && Game::GetState() != GameState::Editor)
                            continue; // Don't depth prepass cloaked objects unless in editor mode

                        auto model = object.Render.Model.ID;

                        if (object.Render.Model.Outrage) {
                            if (ctx.ApplyEffect(Effects->DepthObject))
                                ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
                            OutrageModelDepthPrepass(ctx, object);
                        }
                        else {
                            if (cmd.Data.Object->Type == ObjectType::Robot)
                                model = Resources::GetRobotInfo(object.ID).Model;

                            // todo: fix bug with this causing *all* objects to be rendered as flipped after firing lasers
                            //if (object.Type == ObjectType::Weapon) {
                            //    // Flip outer model of weapons with inner models so the Z buffer will allow drawing them
                            //    auto inner = Resources::GameData.Weapons[object.ID].ModelInner;
                            //    if (inner > ModelID::None && inner != ModelID(255))
                            //        effect = Effects->DepthObjectFlipped;
                            //}

                            ModelDepthPrepass(ctx, object, model);
                        }
                    }
                    //else if (object.Render.Type == RenderType::Powerup ||
                    //    //object.Render.Type == RenderType::WeaponVClip ||
                    //    //object.Render.Type == RenderType::Fireball ||
                    //    object.Render.Type == RenderType::Hostage) {
                    //    auto& effect = Effects->DepthObject;
                    //    if (ctx.ApplyEffect(effect)) {
                    //        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());

                    //        ObjectDepthShader::Constants constants = {};
                    //        effect.Shader->SetConstants(cmdList, constants);
                    //        auto sampler = Render::GetClampedTextureSampler();
                    //        effect.Shader->SetSampler(cmdList, sampler);
                    //        effect.Shader->SetTextureTable(cmdList, Render::Heaps->Materials.GetGpuHandle(0));
                    //        effect.Shader->SetVClipTable(cmdList, Render::VClipBuffer->GetSRV());
                    //    }

                    //    auto up = object.Rotation.Up();
                    //    SpriteDepthPrepass(cmdList, object, object.Render.Type == RenderType::Hostage ? &up : nullptr);
                    //}
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
        Adapter->GetDepthBuffer().Transition(cmdList, D3D12_RESOURCE_STATE_DEPTH_READ);
    }

    void DrawLevelMesh(const GraphicsContext& ctx, const Inferno::LevelMesh& mesh, bool decalSubpass) {
        if (!mesh.Chunk) return;
        auto& chunk = *mesh.Chunk;

        LevelShader::InstanceConstants constants{};
        constants.LightingScale = Settings::Editor.RenderMode == RenderMode::Shaded ? 1.0f : 0.0f; // How much light to apply

        auto cmdList = ctx.GetCommandList();
        auto& ti = Resources::GetLevelTextureInfo(chunk.TMap1);

        if (decalSubpass && chunk.TMap2 == LevelTexID::Unset) return;

        auto* mat1 = &Materials->Black();
        auto mat1Handle = Materials->Black().Handle();

        auto* mat2 = &Materials->Black();
        auto mat2Handle = Materials->Black().Handle();

        if (chunk.Cloaked) {
            // todo: cloaked walls will have to be rendered with a different shader -> prefer glass / distortion
            constants.LightingScale = 1;
        }
        else {
            constants.HasOverlay = !decalSubpass && chunk.TMap2 > LevelTexID::Unset;
            constants.IsOverlay = decalSubpass;

            // Only walls and decals have tags
            auto side = Game::Level.TryGetSide(chunk.Tag);

            if (SideIsDoor(side)) {
                // Use the current texture for this side, as walls are drawn individually

                if (!decalSubpass) {
                    mat1 = &Materials->Get(side->TMap);
                    mat1Handle = mat1->Handle();
                }
                else {
                    mat1 = &Materials->Get(side->TMap2);
                    mat1Handle = mat1->Handle();
                }
            }
            else {
                if (!decalSubpass) {
                    if (auto proc = GetLevelProcedural(chunk.TMap1)) {
                        // For procedural textures the animation is baked into it
                        mat1 = &Materials->Get(chunk.TMap1);
                        mat1Handle = proc->GetHandle();
                    }
                    else {
                        auto& map1 = chunk.EffectClip1 == EClipID::None ? Materials->Get(chunk.TMap1) : Materials->Get(chunk.EffectClip1, ElapsedTime, false);
                        mat1 = &map1;
                        mat1Handle = map1.Handle();
                    }
                }
                else {
                    if (auto proc = GetLevelProcedural(chunk.TMap2)) {
                        mat1 = &Materials->Get(chunk.TMap2);
                        mat1Handle = proc->GetHandle();
                    }
                    else {
                        auto decal = chunk.TMap2;
                        auto effect = chunk.EffectClip2;
                        if (side) {
                            decal = side->TMap2;
                            effect = Resources::GetEffectClipID(side->TMap2);
                        }

                        auto& map2 = effect == EClipID::None ? Materials->Get(decal) : Materials->Get(effect, ElapsedTime, Game::ControlCenterDestroyed);
                        mat1 = &map2;
                        mat1Handle = map2.Handle();
                    }
                }
            }
        }

        constants.Scroll = ti.Slide;
        constants.Scroll2 = chunk.OverlaySlide;
        constants.Distort = ti.Slide != Vector2::Zero;
        constants.Tex1 = (int)ti.TexID;
        constants.LightColor = Color(0, 0, 0);

        if (auto segment = Seq::tryItem(SegmentLights, (uint)chunk.Tag.Segment)) {
            auto& side = segment->Sides[(uint)chunk.Tag.Side];
            constants.LightColor = side.AnimatedColor;
        }

        // Tell the shader to skip discards because procedurals do not handle transparency
        if (chunk.SkipDecalCull) constants.HasOverlay = false;

        if (decalSubpass) {
            constants.Tex1 = (int)Resources::LookupTexID(chunk.TMap2);
        }
        else if (constants.HasOverlay) {
            // Pass tex2 when drawing base texture to discard pixels behind the decal
            auto decal = chunk.EffectClip2 == EClipID::None ? Resources::LookupTexID(chunk.TMap2) : Resources::GetEffectClip(chunk.TMap2).VClip.GetFrame(ElapsedTime);\
            constants.Tex2 = (int)decal;
            mat2 = &Materials->Get(decal);
            mat2Handle = mat2->Handle();
        }

        Shaders->Level.SetDiffuse1(cmdList, mat1Handle);
        Shaders->Level.SetMaterial1(cmdList, *mat1);
        Shaders->Level.SetDiffuse2(cmdList, mat2Handle);
        Shaders->Level.SetMaterial2(cmdList, *mat2);
        Shaders->Level.SetInstanceConstants(cmdList, constants);
        Shaders->Level.SetLightGrid(cmdList, *Render::LightGrid);
        mesh.Draw(cmdList);
    }

    void ExecuteRenderCommand(GraphicsContext& ctx, const RenderCommand& cmd, RenderPass pass, bool decals = false) {
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
                        if (pass != RenderPass::Opaque && pass != RenderPass::Decals) return;
                        ctx.ApplyEffect(Effects->LevelFlat);
                    }

                    ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
                    cmd.Data.LevelMesh->Draw(ctx.GetCommandList());
                }
                else {
                    bool effectChanged = false;

                    if (mesh.Chunk->Blend == BlendMode::Alpha) {
                        if (pass != RenderPass::Walls) return;
                        effectChanged = ctx.ApplyEffect(Effects->LevelWall);
                    }
                    else if (mesh.Chunk->Blend == BlendMode::Additive) {
                        if (pass != RenderPass::Transparent) return;
                        effectChanged = ctx.ApplyEffect(Effects->LevelWallAdditive);
                    }
                    else {
                        if (pass == RenderPass::Opaque)
                            effectChanged = ctx.ApplyEffect(Effects->Level);
                        else if (pass == RenderPass::Decals)
                            effectChanged = ctx.ApplyEffect(Effects->LevelWall); // Level wall has alpha enabled
                        else
                            return;
                    }

                    ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
                    auto cmdList = ctx.GetCommandList();
                    if (effectChanged) {
                        Shaders->Level.SetSampler(cmdList, GetWrappedTextureSampler());
                        Shaders->Level.SetNormalSampler(cmdList, GetNormalSampler());
                        auto cubeSrv = Render::Materials->EnvironmentCube.GetCubeSRV().GetGpuHandle();
                        if (!cubeSrv.ptr) cubeSrv = Render::Adapter->NullCube.GetGpuHandle();
                        Shaders->Level.SetEnvironment(cmdList, cubeSrv);

                        Shaders->Level.SetDepthTexture(cmdList, Adapter->LinearizedDepthBuffer.GetSRV());
                        Shaders->Level.SetMaterialInfoBuffer(cmdList, MaterialInfoBuffer->GetSRV());
                        Shaders->Level.SetTextureTable(cmdList, Render::Heaps->Materials.GetGpuHandle(0));
                    }

                    DrawLevelMesh(ctx, *cmd.Data.LevelMesh, decals);
                }

                break;
            }
            case RenderCommandType::Object:
                DrawObject(ctx, *cmd.Data.Object, pass);
                break;

            case RenderCommandType::Effect:
                if ((pass == RenderPass::Opaque && cmd.Data.Effect->Queue == RenderQueueType::Opaque) ||
                    (pass == RenderPass::Transparent && cmd.Data.Effect->Queue == RenderQueueType::Transparent))
                    cmd.Data.Effect->Draw(ctx);
                break;
        }
    }

    void DrawDebug(const Level& level, const Camera& camera) {
        //Debug::DrawPoint(Inferno::Debug::ClosestPoint, Color(1, 0, 0));
        if (Settings::Editor.EnablePhysics) {
            for (auto& point : Inferno::Debug::ClosestPoints) {
                Debug::DrawPoint(point, Color(1, 0, 0), camera);
            }
        }

        for (auto& emitter : Inferno::Sound::Debug::Emitters) {
            Debug::DrawPoint(emitter, { 0, 1, 0 }, camera);
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

    void RebuildLevelResources(Level& level) {
        _levelMeshBuilder.Update(level, *LevelResources.LevelMeshes.get());

        for (auto& room : level.Rooms) {
            room.WallMeshes.clear();
        }

        // Update wall meshes in the room
        auto wallMeshes = _levelMeshBuilder.GetWallMeshes();
        for (int i = 0; i < wallMeshes.size(); i++) {
            if (auto room = level.GetRoom(wallMeshes[i].Chunk->Tag.Segment)) {
                room->WallMeshes.push_back(i);
            }
        }

        SegmentLights = Graphics::GatherSegmentLights(level);
        LevelChanged = false;
    }

    void DrawTerrain(GraphicsContext& ctx) {
        auto terrainMesh = LevelResources.TerrainMesh.get();
        if (!terrainMesh) return;

        auto cmdList = ctx.GetCommandList();
        ctx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        const auto& terrain = Game::Terrain;

        if (!terrain.SatelliteTexture.empty()) {
            // Draw satellites
            auto& effect = terrain.SatelliteAdditive ? Effects->Sun : Effects->Sprite;
            ctx.ApplyEffect(effect);
            ctx.SetConstantBuffer(0, Adapter->GetTerrainConstants().GetGPUVirtualAddress());
            //effect.Shader->SetDepthTexture(ctx.GetCommandList(), Adapter->LinearizedDepthBuffer.GetSRV());
            effect.Shader->SetSampler(ctx.GetCommandList(), Render::GetClampedTextureSampler());

            for (auto& sat : terrainMesh->GetSatellites()) {
                auto& texture = Render::Materials->Get(sat.TextureName);
                effect.Shader->SetDiffuse(cmdList, texture.Handle());

                cmdList->IASetVertexBuffers(0, 1, &sat.VertexBuffer);
                cmdList->IASetIndexBuffer(&sat.IndexBuffer);
                cmdList->DrawIndexedInstanced(sat.IndexCount, 1, 0, 0, 0);
            }

            //Game::Terrain.PlanetTexture = "sun.bbm";

            //auto& planet = Render::Materials->Get(terrain.SatelliteTexture);
            //auto pos = terrain.SatelliteDir * 1000 + Vector3(0, terrain.SatelliteHeight, 0);
            //pos = Vector3::Transform(pos, terrain.Transform);
            //auto up = terrain.Transform.Up();
            //up = Vector3::Up;
            ////auto color = isSun ? Color(1, 1, 1) * 1 : Color(3, 3, 3);
            //auto color = Color(3, 3, 3);
            //color.A(1);
            //DrawBillboard(ctx, terrain.SatelliteAspectRatio, planet.Handle(), Adapter->GetTerrainConstants().GetGPUVirtualAddress(), TerrainCamera, pos, terrain.SatelliteSize, color, terrain.SatelliteAdditive, 0, &up);
        }

        auto& effect = Effects->Terrain;
        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
        effect.Shader->SetSampler(cmdList, GetWrappedTextureSampler());
        effect.Shader->SetNormalSampler(cmdList, GetNormalSampler());
        //effect.Shader->SetLightGrid(cmdList, *Render::LightGrid);

        TerrainShader::Constants constants = {};
        constants.World = Game::Terrain.Transform;
        constants.Ambient = Vector4(1, 1, 1, 1);
        effect.Shader->SetConstants(cmdList, constants);

        auto& depthBuffer = Adapter->GetDepthBuffer();
        depthBuffer.Transition(cmdList, D3D12_RESOURCE_STATE_DEPTH_WRITE);

        {
            // Draw terrain
            auto& mesh = terrainMesh->GetTerrain();
            auto& terrainTexture = Render::Materials->Get(mesh.TextureName);
            effect.Shader->SetDiffuse(cmdList, terrainTexture.Handle());

            cmdList->IASetVertexBuffers(0, 1, &mesh.VertexBuffer);
            cmdList->IASetIndexBuffer(&mesh.IndexBuffer);
            cmdList->DrawIndexedInstanced(mesh.IndexCount, 1, 0, 0, 0);
        }

        //ctx.GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        Color ambient = { 4, 4, 4 };
        DrawStaticModel(ctx, Game::Terrain.ExitModel, RenderPass::Opaque, ambient, Adapter->GetFrameConstants(), Game::Terrain.ExitTransform);
    }

    void DrawStars(GraphicsContext& ctx) {
        auto cmdList = ctx.GetCommandList();
        ctx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx.ApplyEffect(Effects->Stars);
        ctx.SetConstantBuffer(0, Adapter->GetTerrainConstants().GetGPUVirtualAddress());
        Shaders->Stars.SetParameters(cmdList, { Game::Terrain.AtmosphereColor });
        cmdList->DrawInstanced(3, 1, 0, 0);
    }

    void DrawLevel(GraphicsContext& ctx, Level& level) {
        if (Settings::Editor.ShowFlickeringLights)
            UpdateFlickeringLights(level, (float)ElapsedTime, Game::FrameTime);

        bool drawObjects = true;
        if (Game::GetState() == GameState::Editor && !Settings::Editor.ShowObjects)
            drawObjects = false;

        _renderQueue.Update(level, _levelMeshBuilder, drawObjects, ctx.Camera);

        for (auto& id : _renderQueue.GetVisibleRooms()) {
            auto room = level.GetRoom(id);
            if (!room) continue;

            for (auto& segid : room->Segments) {
                auto lights = Seq::tryItem(SegmentLights, (int)segid);
                if (!lights) continue;

                // Add lights on each side
                for (auto& sideLights : lights->Sides) {
                    for (int lid = 0; lid < sideLights.Lights.size(); lid++) {
                        auto& light = sideLights.Lights[lid];
                        DynamicLightMode mode = light.mode;

                        if (sideLights.Color.w <= 0 || sideLights.Radius <= 0 || light.mode == DynamicLightMode::Off)
                            continue;

                        if (Game::ControlCenterDestroyed) {
                            if (lid % 3 == 0) mode = DynamicLightMode::StrongFlicker;
                            else if (lid % 2 == 0) mode = DynamicLightMode::StrongFlicker;
                        }

                        AnimateLight(sideLights, mode);
                        Graphics::Lights.AddLight(light);

                        if (Settings::Editor.ShowLights) {
                            Color lineColor(1, .6f, .2f);
                            if (light.type == LightType::Rectangle) {
                                Debug::DrawLine(light.pos + light.right + light.up, light.pos + light.right - light.up, lineColor); // right
                                Debug::DrawLine(light.pos + light.right - light.up, light.pos - light.right - light.up, lineColor); // bottom
                                Debug::DrawLine(light.pos - light.right + light.up, light.pos - light.right - light.up, lineColor); // left
                                Debug::DrawLine(light.pos - light.right + light.up, light.pos + light.right + light.up, lineColor); // top
                            }
                            else {
                                Debug::DrawPoint(light.pos, lineColor, Game::GameCamera);
                                //Debug::DrawLine(light.pos, light.pos + light.normal * light.radius/2, color);
                                if (light.normal != Vector3::Zero) {
                                    auto transform = Matrix(VectorToRotation(light.normal));
                                    transform.Translation(light.pos);
                                    Debug::DrawCircle(5 /*light.radius*/, transform, lineColor);
                                }
                            }
                        }
                    }
                }

                // Add lights inside the segment
                for (auto& light : lights->Lights) {
                    LightData l = light;
                    l.color *= Game::GlobalDimming;
                    Graphics::Lights.AddLight(l);
                }
            }

            if (Settings::Graphics.OutlineVisibleRooms && Game::GetState() != GameState::Editor)
                Debug::OutlineRoom(level, *room, Color(1, 1, 1, 0.5f));
        }

        // Debug active rooms
        //for (auto& id : Game::Debug::ActiveRooms) {
        //    if (auto room = level.GetRoom(id))
        //        Debug::OutlineRoom(level, *room, Color(0.6f, 0.4f, 1, 0.5f));
        //}

        LegitProfiler::ProfilerTask depth("Depth prepass", LegitProfiler::Colors::SUN_FLOWER);
        ctx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        DepthPrepass(ctx);
        LegitProfiler::AddCpuTask(std::move(depth));

        auto cmdList = ctx.GetCommandList();
        Graphics::Lights.Dispatch(ctx);

        {
            PIXScopedEvent(cmdList, PIX_COLOR_INDEX(5), "Level");
            LegitProfiler::ProfilerTask queue("Execute queues", LegitProfiler::Colors::AMETHYST);

            auto& depthBuffer = Adapter->GetDepthBuffer();

            auto& target = Adapter->GetRenderTarget();
            target.Transition(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
            ctx.SetRenderTarget(target.GetRTV(), depthBuffer.GetDSV());
            ctx.SetViewportAndScissor(UINT(target.GetWidth() * Settings::Graphics.RenderScale), UINT(target.GetHeight() * Settings::Graphics.RenderScale));
            LightGrid->SetLightConstants(UINT(target.GetWidth() * Settings::Graphics.RenderScale), UINT(target.GetHeight() * Settings::Graphics.RenderScale));


            // todo: OR game show terrain
            if (Settings::Editor.ShowTerrain) {
                //cmdList->OMSetStencilRef(0);
                DrawStars(ctx);
                DrawTerrain(ctx);
                //cmdList->OMSetStencilRef(1);
            }

            ScopedTimer execTimer(&Metrics::ExecuteRenderCommands);

            depthBuffer.Transition(cmdList, D3D12_RESOURCE_STATE_DEPTH_READ);

            // Faking the exit portal using scissors
            //if (Game::Level.SegmentExists(Game::Terrain.ExitTag)) {
            //    auto face = ConstFace::FromSide(level, Game::Terrain.ExitTag);

            //    if (auto ndc = GetNdc(face, ctx.Camera.ViewProjection)) {
            //        auto bounds = Bounds2D::FromPoints(*ndc);

            //        D3D12_RECT scissor{
            //            .left = LONG((bounds.Min.x + 1) * target.GetWidth() * 0.5f),
            //            .top = LONG((1 - bounds.Max.y) * target.GetHeight() * 0.5f),
            //            .right = LONG((bounds.Max.x + 1) * target.GetWidth() * 0.5f),
            //            .bottom = LONG((1 - bounds.Min.y) * target.GetHeight() * 0.5f)
            //        };

            //        cmdList->RSSetScissorRects(1, &scissor);
            //    }
            //}

            {
                PIXScopedEvent(cmdList, PIX_COLOR_INDEX(1), "Opaque queue");
                for (auto& cmd : _renderQueue.Opaque())
                    ExecuteRenderCommand(ctx, cmd, RenderPass::Opaque);
            }

            {
                PIXScopedEvent(cmdList, PIX_COLOR_INDEX(1), "Decal queue");
                for (auto& cmd : _renderQueue.Opaque())
                    ExecuteRenderCommand(ctx, cmd, RenderPass::Decals, true);
            }

            {
                PIXScopedEvent(cmdList, PIX_COLOR_INDEX(2), "Wall queue");
                for (auto& cmd : _renderQueue.Transparent())
                    ExecuteRenderCommand(ctx, cmd, RenderPass::Walls);
            }

            {
                PIXScopedEvent(cmdList, PIX_COLOR_INDEX(2), "Wall decal queue");
                for (auto& cmd : _renderQueue.Transparent())
                    ExecuteRenderCommand(ctx, cmd, RenderPass::Walls, true);
            }

            DrawDecals(ctx, Game::FrameTime);

            {
                PIXScopedEvent(cmdList, PIX_COLOR_INDEX(2), "Transparent queue");
                for (auto& cmd : _renderQueue.Transparent())
                    ExecuteRenderCommand(ctx, cmd, RenderPass::Transparent);
            }

            //ctx.SetViewportAndScissor(UINT(target.GetWidth() * Settings::Graphics.RenderScale), UINT(target.GetHeight() * Settings::Graphics.RenderScale));

            // Copy the contents of the render target to the distortion buffer
            auto& renderTarget = Adapter->GetRenderTarget();

            if (Settings::Graphics.MsaaSamples > 1)
                Adapter->DistortionBuffer.ResolveFromMultisample(cmdList, renderTarget);
            else
                renderTarget.CopyTo(cmdList, Adapter->DistortionBuffer);

            Adapter->DistortionBuffer.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            renderTarget.Transition(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);

            for (auto& cmd : _renderQueue.Distortion())
                ExecuteRenderCommand(ctx, cmd, RenderPass::Distortion);

            LegitProfiler::AddCpuTask(std::move(queue));

            //for (auto& cmd : _transparentQueue) // draw transparent geometry on models
            //    ExecuteRenderCommand(cmdList, cmd, true);

            // Draw heat volumes
            //    _levelResources->Volumes.Draw(cmdList);
        }

        Canvas->SetSize(Adapter->GetWidth(), Adapter->GetHeight());
        if (!Settings::Inferno.ScreenshotMode && Game::GetState() == GameState::Editor) {
            PIXScopedEvent(cmdList, PIX_COLOR_INDEX(6), "Editor");
            LegitProfiler::ProfilerTask editor("Draw editor", LegitProfiler::Colors::CLOUDS);
            DrawEditor(ctx, level);
            DrawDebug(level, ctx.Camera);
            LegitProfiler::AddCpuTask(std::move(editor));
        }
        else {
            //Canvas->DrawGameText(level.Name, 0, 20 * Shell::DpiScale, FontSize::Big, { 1, 1, 1 }, 0.5f, AlignH::Center, AlignV::Top);
            Render::DrawTextInfo info;
            info.Position = Vector2(-10 * Shell::DpiScale, -10 * Shell::DpiScale);
            info.HorizontalAlign = AlignH::Right;
            info.VerticalAlign = AlignV::Bottom;
            info.Font = FontSize::MediumGold;
            info.Scale = 0.5f;
            Canvas->DrawGameText("Inferno\nEngine", info);
        }

        EndUpdateEffects();
    }

    int GetTransparentQueueSize() {
        return (int)_renderQueue.Transparent().size();
    }

    span<RoomID> GetVisibleRooms() {
        return _renderQueue.GetVisibleRooms();
    }
}
