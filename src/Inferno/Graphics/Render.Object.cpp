#include "pch.h"

#include "Game.h"
#include "Game.Object.h"
#include "MaterialLibrary.h"
#include "Render.Editor.h"
#include "Render.h"
#include "Resources.h"
#include "Render.Object.h"
#include "OpenSimplex2.h"
#include "Render.Debug.h"

//#define DEBUG_DISSOLVE

namespace Inferno::Render {
    constexpr Color MIN_POWERUP_AMBIENT = { 0.1f, 0.1f, 0.1f };

    constexpr float GetTimeOffset(const Object& obj) {
        return (float)obj.Signature * 0.762f; // randomize time across objects
    }

    // When up is provided, it constrains the sprite to that axis
    void DrawSprite(GraphicsContext& ctx,
                    const Object& object,
                    bool additive,
                    const Vector3* up = nullptr,
                    bool lit = false) {
        Color color = lit ? object.Ambient.GetValue() * Game::GlobalDimming : Color(1, 1, 1);
        if (object.IsPowerup()) color += MIN_POWERUP_AMBIENT;

        if (object.Render.Emissive != Color())
            color = object.Render.Emissive;

        color += object.Render.VClip.DirectLight;
        auto pos = object.GetPosition(Game::LerpAmount);

        if (object.Render.Type == RenderType::WeaponVClip ||
            object.Render.Type == RenderType::Powerup ||
            object.Render.Type == RenderType::Hostage) {
            auto& vclip = Resources::GetVideoClip(object.Render.VClip.ID);
            if (vclip.NumFrames == 0) {
                if (Game::GetState() == GameState::Editor && !Settings::Editor.HideUI)
                    DrawObjectOutline(object, ctx.Camera);

                return;
            }

            // Randomize sprite animation
            auto tid = vclip.GetFrame(Game::Time + GetTimeOffset(object));
            BillboardInfo info = {
                .Radius = object.Radius,
                .Color = color,
                .Additive = additive,
                .Rotation = object.Render.Rotation,
                .Up = up,
                .Terrain = object.Segment == SegID::Terrain
            };
            DrawBillboard(ctx, tid, pos, info);
        }
        else if (object.Render.Type == RenderType::Laser) {
            // "laser" is used for still-image "blobs" like spreadfire
            auto& weapon = Resources::GetWeapon((WeaponID)object.ID);
            BillboardInfo info = {
                .Radius = object.Radius,
                .Color = color,
                .Additive = additive,
                .Rotation = object.Render.Rotation,
                .Up = up,
                .Terrain = object.Segment == SegID::Terrain
            };
            DrawBillboard(ctx, weapon.BlobBitmap, pos, info);
        }
        else if (Game::GetState() == GameState::Editor && !Settings::Editor.HideUI) {
            DrawObjectOutline(object, ctx.Camera);
        }
    }

    void SpriteDepthPrepass(GraphicsContext& ctx, const Object& object, const Vector3* up = nullptr) {
        auto pos = object.GetPosition(Game::LerpAmount);

        if (object.Render.Type == RenderType::WeaponVClip ||
            object.Render.Type == RenderType::Powerup ||
            object.Render.Type == RenderType::Hostage) {
            auto& vclip = Resources::GetVideoClip(object.Render.VClip.ID);
            if (vclip.NumFrames == 0) {
                return;
            }

            auto tid = vclip.GetFrame(Game::Time);
            DrawDepthBillboard(ctx, tid, pos, object.Radius, object.Render.Rotation, up);
        }
        else if (object.Render.Type == RenderType::Laser) {
            // "laser" is used for still-image "blobs" like spreadfire
            auto& weapon = Resources::GetWeapon((WeaponID)object.ID);
            DrawDepthBillboard(ctx, weapon.BlobBitmap, pos, object.Radius, object.Render.Rotation, up);
        }
    }


    // Draws a square glow that always faces the camera (Descent 3 submodels);
    void DrawObjectGlow(ID3D12GraphicsCommandList* cmd, float radius, const Color& color, TexID tex, float rotation = 0) {
        if (radius <= 0) return;
        const auto r = radius;
        auto xform = Matrix::CreateRotationZ(rotation);
        Vector3 v0{ -r, r, 0 };
        Vector3 v1{ r, r, 0 };
        Vector3 v2{ r, -r, 0 };
        Vector3 v3{ -r, -r, 0 };
        Vector3::Transform(v0, xform);

        ObjectVertex ov0(Vector3::Transform(v0, xform), { 0, 0 }, color, {}, {}, {}, (int)tex);
        ObjectVertex ov1(Vector3::Transform(v1, xform), { 1, 0 }, color, {}, {}, {}, (int)tex);
        ObjectVertex ov2(Vector3::Transform(v2, xform), { 1, 1 }, color, {}, {}, {}, (int)tex);
        ObjectVertex ov3(Vector3::Transform(v3, xform), { 0, 1 }, color, {}, {}, {}, (int)tex);

        // todo: batch somehow?
        // Horrible immediate mode nonsense
        Stats::DrawCalls++;
        g_SpriteBatch->Begin(cmd);
        g_SpriteBatch->DrawQuad(ov0, ov1, ov2, ov3);
        g_SpriteBatch->End();
    }

    void ModelDepthPrepass(GraphicsContext& ctx, const Object& object, ModelID modelId) {
        auto cmdList = ctx.GetCommandList();
        auto& effect = Game::OnTerrain && object.IsPlayer() ? Effects->TerrainDepthObject : Effects->DepthObject;

        if (ctx.ApplyEffect(effect)) {
            ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
            effect.Shader->SetSampler(cmdList, GetWrappedTextureSampler());
            effect.Shader->SetTextureTable(cmdList, Render::Heaps->Materials.GetGpuHandle(0));
            effect.Shader->SetVClipTable(cmdList, Render::VClipBuffer->GetSRV());
        }

        auto& model = Resources::GetModel(modelId);
        auto& meshHandle = GetMeshHandle(modelId);

        bool transparentOverride = false;
        if (auto texOverride = Resources::LookupTexID(object.Render.Model.TextureOverride); texOverride != TexID::None)
            transparentOverride = Resources::GetTextureInfo(texOverride).Transparent;

        ObjectDepthShader::Constants constants = {};
        constants.TimeOffset = GetTimeOffset(object);
        auto transform = Matrix::CreateScale(object.Scale) * Matrix::Lerp(object.GetPrevTransform(), object.GetTransform(), Game::LerpAmount);

        auto& shader = Shaders->DepthObject;
        shader.SetDissolveTexture(cmdList, Render::Materials->Black().Handle());

#ifdef DEBUG_DISSOLVE
        shader.SetDissolveTexture(cmdList, Render::Materials->Get("noise").Handle());
        shader.SetSampler(cmdList, GetWrappedTextureSampler());
        double x;
        constants.PhaseAmount = (float)std::modf(Clock.GetTotalTimeSeconds() * 0.5, &x);
#else
        if (object.IsPhasing()) {
            shader.SetDissolveTexture(cmdList, Render::Materials->Get("noise").Handle());
            constants.PhaseAmount = std::max(1 - object.Effects.GetPhasePercent(), 0.01f); // Shader checks for 0 to skip effect
        }
#endif

        for (int submodel = 0; submodel < model.Submodels.size(); submodel++) {
            constants.World = GetSubmodelTransform(object, model, submodel) * transform;
            shader.SetConstants(cmdList, constants);

            // get the mesh associated with the submodel
            auto& subMesh = meshHandle.Meshes[submodel];

            for (int i = 0; i < subMesh.size(); i++) {
                auto mesh = subMesh[i];
                if (!mesh) continue;
                if (transparentOverride || mesh->IsTransparent) continue;

                cmdList->IASetVertexBuffers(0, 1, &mesh->VertexBuffer);
                cmdList->IASetIndexBuffer(&mesh->IndexBuffer);
                cmdList->DrawIndexedInstanced(mesh->IndexCount, 1, 0, 0, 0);
                Stats::DrawCalls++;
            }
        }
    }

    void OutrageModelDepthPrepass(GraphicsContext& ctx, const Object& object) {
        assert(object.Render.Type == RenderType::Model);
        auto& meshHandle = GetOutrageMeshHandle(object.Render.Model.ID);

        auto model = Resources::GetOutrageModel(object.Render.Model.ID);
        if (model == nullptr) return;

        ObjectDepthShader::Constants constants = {};
        Matrix transform = Matrix::CreateScale(object.Scale) * Matrix::CreateScale(object.Scale) * Matrix::Lerp(object.GetPrevTransform(), object.GetTransform(), Game::LerpAmount);

        auto cmd = ctx.GetCommandList();
        auto& shader = Shaders->DepthObject;
        shader.SetTextureTable(ctx.GetCommandList(), Render::Heaps->Materials.GetGpuHandle(0));

        for (int submodelIndex = 0; submodelIndex < model->Submodels.size(); submodelIndex++) {
            auto& submodel = model->Submodels[submodelIndex];
            auto& submesh = meshHandle.Meshes[submodelIndex];

            // accumulate the offsets for each submodel
            auto submodelOffset = Vector3::Zero;
            auto* smc = &submodel;
            while (smc->Parent != -1) {
                submodelOffset += smc->Offset;
                smc = &model->Submodels[smc->Parent];
            }

            auto world = Matrix::CreateTranslation(submodelOffset) * transform;

            using namespace Outrage;

            if (submodel.HasFlag(SubmodelFlag::Facing)) {
                continue;
            }
            else {
                if (submodel.HasFlag(SubmodelFlag::Rotate))
                    world = Matrix::CreateFromAxisAngle(submodel.Keyframes[1].Axis, DirectX::XM_2PI * submodel.Rotation * (float)Game::Time) * world;

                constants.World = world;
            }

            // get the mesh associated with the submodel
            for (auto& [i, mesh] : submesh) {
                if (i == -1) continue; // flat rendering? invisible mesh?
                auto& material = Render::NewTextureCache->GetTextureInfo(model->TextureHandles[i]);
                bool transparent = material.Saturate() || material.Alpha();
                if (transparent) continue; // skip transparent textures in depth prepass

                //constants.Colors[1] = material.Color; // color 1 is used for texture alpha
                shader.SetConstants(cmd, constants);
                cmd->IASetVertexBuffers(0, 1, &mesh->VertexBuffer);
                cmd->IASetIndexBuffer(&mesh->IndexBuffer);
                cmd->DrawIndexedInstanced(mesh->IndexCount, 1, 0, 0, 0);
                Stats::DrawCalls++;
            }
        }
    }

    void DrawOutrageModel(GraphicsContext& ctx,
                          const Object& object,
                          RenderPass pass) {
        assert(object.Render.Type == RenderType::Model);
        auto& meshHandle = GetOutrageMeshHandle(object.Render.Model.ID);

        auto model = Resources::GetOutrageModel(object.Render.Model.ID);
        if (model == nullptr) return;

        ObjectShader::Constants constants = {};
        auto& seg = Game::Level.GetSegment(object.Segment);
        if (object.Render.Emissive != Color(0, 0, 0)) {
            // Ignore ambient if object is emissive
            constants.Ambient = Color(0, 0, 0);
            constants.EmissiveLight = object.Render.Emissive;
        }
        else {
            constants.Ambient = Settings::Editor.RenderMode == RenderMode::Shaded ? seg.VolumeLight : Color(1, 1, 1);
            constants.EmissiveLight = Color(0, 0, 0);
        }

        Matrix transform = Matrix::CreateScale(object.Scale) * Matrix::Lerp(object.GetPrevTransform(), object.GetTransform(), Game::LerpAmount);

        auto cmdList = ctx.GetCommandList();

        for (int submodelIndex = 0; submodelIndex < model->Submodels.size(); submodelIndex++) {
            auto& submodel = model->Submodels[submodelIndex];
            auto& submesh = meshHandle.Meshes[submodelIndex];

            // accumulate the offsets for each submodel
            auto submodelOffset = Vector3::Zero;
            auto* smc = &submodel;
            while (smc->Parent != -1) {
                submodelOffset += smc->Offset;
                smc = &model->Submodels[smc->Parent];
            }

            auto world = Matrix::CreateTranslation(submodelOffset) * transform;

            using namespace Outrage;

            if (submodel.HasFlag(SubmodelFlag::Facing)) {
                auto smPos = Vector3::Transform(Vector3::Zero, world);
                auto billboard = Matrix::CreateBillboard(smPos, ctx.Camera.Position, ctx.Camera.Up);
                constants.World = billboard;
                //constants.Projection = billboard * ViewProjection;
            }
            else {
                if (submodel.HasFlag(SubmodelFlag::Rotate))
                    world = Matrix::CreateFromAxisAngle(submodel.Keyframes[1].Axis, DirectX::XM_2PI * submodel.Rotation * (float)Game::Time) * world;

                constants.World = world;
                //constants.Projection = world * ViewProjection;
            }

            //constants.Time = (float)ElapsedTime;

            // get the mesh associated with the submodel
            for (auto& [texId, mesh] : submesh) {
                if (texId == -1) continue; // flat rendering? invisible mesh?
                auto& material = Render::NewTextureCache->GetTextureInfo(model->TextureHandles[texId]);

                bool transparent = material.Saturate() || material.Alpha();
                bool transparentPass = pass == RenderPass::Transparent;
                if ((transparentPass && !transparent) || (!transparentPass && transparent))
                    continue; // skip saturate textures unless on glow pass

                if (submodel.HasFlag(SubmodelFlag::Glow)) continue; // We have proper bloom

                //auto handle = texId >= 0 
                //    ? Render::NewTextureCache->GetResource(model->TextureHandles[texId], (float)ElapsedTime) 
                //    : Materials->White().Handle();
                bool additive = material.Saturate() || submodel.HasFlag(SubmodelFlag::Facing);

                auto& effect = additive ? Effects->ObjectGlow : Effects->Object;
                if (ctx.ApplyEffect(effect)) {
                    effect.Shader->SetSampler(cmdList, GetWrappedTextureSampler());
                    effect.Shader->SetNormalSampler(cmdList, GetNormalSampler());
                    effect.Shader->SetTextureTable(cmdList, Render::Heaps->Materials.GetGpuHandle(0));
                    effect.Shader->SetVClipTable(cmdList, Render::VClipBuffer->GetSRV());
                    effect.Shader->SetMaterialInfoBuffer(cmdList, Render::MaterialInfoBuffer->GetSRV());
                    effect.Shader->SetLightGrid(cmdList, Render::Adapter->LightGrid);
                }

                if (transparentPass && submodel.HasFlag(SubmodelFlag::Facing)) {
                    //if (object.Type == ObjectType::Weapon) continue; // Facing on weapons is usually glows

                    if (material.Saturate())
                        constants.Ambient = Color(1, 1, 1);
                    //constants.Colors[1] = Color(1, 1, 1, 1);
                    effect.Shader->SetConstants(cmdList, constants);
                    DrawObjectGlow(cmdList, submodel.Radius, Color(1, 1, 1, 1), mesh->Texture);
                }
                else {
                    //constants.Colors[1] = material.Color; // color 1 is used for texture alpha
                    effect.Shader->SetConstants(cmdList, constants);
                    cmdList->IASetVertexBuffers(0, 1, &mesh->VertexBuffer);
                    cmdList->IASetIndexBuffer(&mesh->IndexBuffer);
                    cmdList->DrawIndexedInstanced(mesh->IndexCount, 1, 0, 0, 0);
                    Stats::DrawCalls++;
                }
            }
        }
    }

    void DrawCloakedModel(GraphicsContext& ctx,
                          const Object& object,
                          ModelID modelId,
                          RenderPass pass) {
        if (pass != RenderPass::Transparent) return;

        auto cmdList = ctx.GetCommandList();
        auto& effect = Effects->ObjectDistortion;
        if (ctx.ApplyEffect(effect)) {
            ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
            effect.Shader->SetFrameTexture(cmdList, Adapter->DistortionBuffer.GetSRV());
        }

        auto& model = Resources::GetModel(modelId);
        if (model.DataSize == 0) {
            if (Game::GetState() == GameState::Editor && !Settings::Editor.HideUI)
                DrawObjectOutline(object, ctx.Camera);

            return;
        }

        auto& meshHandle = GetMeshHandle(modelId);
        Matrix transform = Matrix::CreateScale(object.Scale) * object.GetTransform(Game::LerpAmount);
        ObjectDistortionShader::Constants constants{};
        constants.TimeOffset = GetTimeOffset(object);
        constexpr float flickerSpeed = 3.75f;
        auto noise = OpenSimplex2::Noise2((int)object.Signature, Game::Time * flickerSpeed, 0);
        constants.Noise = (1 + noise) * 0.5f; // Map to 0-1
        auto noise2 = OpenSimplex2::Noise2((int)object.Signature, constants.TimeOffset + Game::Time * flickerSpeed * 0.5f, 0);
        constants.Noise2 = (1 + noise2) * 0.5f; // Map to 0-1

        for (int submodel = 0; submodel < model.Submodels.size(); submodel++) {
            auto world = GetSubmodelTransform(object, model, submodel) * transform;
            constants.World = world;

            // get the mesh for the submodel
            auto& subMesh = meshHandle.Meshes[submodel];

            for (int i = 0; i < subMesh.size(); i++) {
                auto mesh = subMesh[i];
                if (!mesh) continue;

                effect.Shader->SetConstants(cmdList, constants);

                cmdList->IASetVertexBuffers(0, 1, &mesh->VertexBuffer);
                cmdList->IASetIndexBuffer(&mesh->IndexBuffer);
                cmdList->DrawIndexedInstanced(mesh->IndexCount, 1, 0, 0, 0);
                Stats::DrawCalls++;
            }
        }
    }


    void StaticModelDepthPrepass(GraphicsContext& ctx, ModelID modelId, const Matrix& transform) {
        if (modelId == ModelID::None) return;

        auto cmdList = ctx.GetCommandList();
        auto& effect = Effects->DepthObject;

        if (ctx.ApplyEffect(effect)) {
            ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
            effect.Shader->SetSampler(cmdList, GetWrappedTextureSampler());
            effect.Shader->SetTextureTable(cmdList, Render::Heaps->Materials.GetGpuHandle(0));
            effect.Shader->SetVClipTable(cmdList, Render::VClipBuffer->GetSRV());
        }

        auto& model = Resources::GetModel(modelId);
        auto& meshHandle = GetMeshHandle(modelId);

        ObjectDepthShader::Constants constants = {};
        constants.World = transform;

        auto& shader = Shaders->DepthObject;
        shader.SetDissolveTexture(cmdList, Render::Materials->Black().Handle());

        for (int submodel = 0; submodel < model.Submodels.size(); submodel++) {
            shader.SetConstants(cmdList, constants);

            // get the mesh associated with the submodel
            auto& subMesh = meshHandle.Meshes[submodel];

            for (int i = 0; i < subMesh.size(); i++) {
                auto mesh = subMesh[i];
                if (!mesh) continue;

                cmdList->IASetVertexBuffers(0, 1, &mesh->VertexBuffer);
                cmdList->IASetIndexBuffer(&mesh->IndexBuffer);
                cmdList->DrawIndexedInstanced(mesh->IndexCount, 1, 0, 0, 0);
                Stats::DrawCalls++;
            }
        }
    }

    void DrawStaticModel(GraphicsContext& ctx,
                         ModelID modelId,
                         RenderPass /*pass*/,
                         const Color& ambient,
                         const UploadBuffer<FrameConstants>& frameConstants,
                         const Matrix& transform) {
        auto& effect = Effects->TerrainObject;
        auto cmdList = ctx.GetCommandList();

        auto& model = Resources::GetModel(modelId);
        if (model.DataSize == 0)
            return;

        if (ctx.ApplyEffect(effect)) {
            ctx.SetConstantBuffer(0, frameConstants.GetGPUVirtualAddress());
            effect.Shader->SetSampler(cmdList, GetWrappedTextureSampler());
            effect.Shader->SetNormalSampler(cmdList, GetNormalSampler());
            effect.Shader->SetTextureTable(cmdList, Render::Heaps->Materials.GetGpuHandle(0));
            effect.Shader->SetVClipTable(cmdList, Render::VClipBuffer->GetSRV());
            effect.Shader->SetMaterialInfoBuffer(cmdList, Render::MaterialInfoBuffer->GetSRV());
            effect.Shader->SetLightGrid(cmdList, Render::Adapter->LightGrid);
            auto cubeSrv = Render::Materials->EnvironmentCube.GetCubeSRV().GetGpuHandle();
            if (!cubeSrv.ptr) cubeSrv = Render::Adapter->NullCube.GetGpuHandle();
            effect.Shader->SetEnvironmentCube(cmdList, cubeSrv);
            effect.Shader->SetMatcap(cmdList, Render::Materials->Matcap.GetSRV());
            effect.Shader->SetDissolveTexture(cmdList, Render::Materials->White().Handle());
        }

        ObjectShader::Constants constants = {};
        constants.Ambient = ambient;
        constants.EmissiveLight = Color(0, 0, 0);
        constants.TimeOffset = 0;
        constants.World = transform;

        //Matrix transform = Matrix::CreateScale(object.Scale) * object.GetTransform(Game::LerpAmount);
        auto texOverride = TexID::None;

        constants.TexIdOverride = -1;

        if (texOverride != TexID::None) {
            if (auto effectId = Resources::GetEffectClipID(texOverride); effectId > EClipID::None)
                constants.TexIdOverride = (int)effectId + VCLIP_RANGE;
            else
                constants.TexIdOverride = (int)texOverride;
        }

        auto& meshHandle = GetMeshHandle(modelId);

        for (int submodel = 0; submodel < model.Submodels.size(); submodel++) {
            // get the mesh associated with the submodel
            auto& subMesh = meshHandle.Meshes[submodel];

            for (int i = 0; i < subMesh.size(); i++) {
                auto mesh = subMesh[i];
                if (!mesh) continue;

                //bool isTransparent = mesh->IsTransparent || transparentOverride;
                //if (isTransparent && pass != RenderPass::Transparent) continue;
                //if (!isTransparent && pass != RenderPass::Opaque) continue;

                //if (isTransparent) {
                //    auto& material = Resources::GetMaterial(mesh->Texture);
                //    if (material.Additive)
                //        ctx.ApplyEffect(Effects->ObjectGlow); // Additive blend
                //    else
                //        ctx.ApplyEffect(Effects->Object); // Alpha blend
                //}
                //else {
                //    ctx.ApplyEffect(effect);
                //}

                effect.Shader->SetConstants(cmdList, constants);

                cmdList->IASetVertexBuffers(0, 1, &mesh->VertexBuffer);
                cmdList->IASetIndexBuffer(&mesh->IndexBuffer);
                cmdList->DrawIndexedInstanced(mesh->IndexCount, 1, 0, 0, 0);
                Stats::DrawCalls++;
            }
        }
    }

    void DrawModel(GraphicsContext& ctx,
                   const Object& object,
                   ModelID modelId,
                   RenderPass pass,
                   const UploadBuffer<FrameConstants>& frameConstants) {
        if (Settings::Graphics.DrawGunpoints) {
            if (object.IsRobot()) {
                auto& robot = Resources::GetRobotInfo(object);
                auto forward = object.GetRotation(Game::LerpAmount).Forward();

                for (uint8 i = 0; i < robot.Guns; i++) {
                    auto pos = GetGunpointWorldPosition(object, i);
                    Debug::DrawLine(pos, pos + forward * 2, Color(0, 1, 0), Color(0, 1, 0, 0));
                }
            }
            else if (object.IsReactor()) {
                if (auto info = Seq::tryItem(Resources::GameData.Reactors, object.ID)) {
                    for (uint8 gun = 0; gun < info->Guns; gun++) {
                        auto gunSubmodel = GetGunpointSubmodelOffset(object, gun);
                        auto objOffset = GetSubmodelOffset(object, gunSubmodel);
                        auto gunPoint = Vector3::Transform(objOffset, object.GetTransform());
                        auto gunDir = Vector3::Transform(info->GunDirs[gun], object.Rotation);
                        Debug::DrawLine(gunPoint, gunPoint + gunDir * 2, Color(0, 1, 0), Color(0, 1, 0, 0));
                    }
                }
            }
            else if (object.IsPlayer()) {
                for (uint8 gun = 0; gun < Resources::GameData.PlayerShip.Gunpoints.size(); gun++) {
                    auto gunSubmodel = GetGunpointSubmodelOffset(object, gun);
                    auto objOffset = GetSubmodelOffset(object, gunSubmodel);
                    auto gunPoint = Vector3::Transform(objOffset, object.GetTransform());
                    auto forward = object.GetRotation(Game::LerpAmount).Forward();
                    if (gun == 7) forward *= -1; // Flip bomb gunpoint
                    Debug::DrawLine(gunPoint, gunPoint + forward * 2, Color(0, 1, 0), Color(0, 1, 0, 0));
                }
            }
        }

        if (object.IsCloaked() && Game::GetState() != GameState::Editor) {
            DrawCloakedModel(ctx, object, modelId, pass);
            return;
        }

        auto& effect = Game::OnTerrain && object.IsPlayer() ? Effects->TerrainObject : Effects->Object;
        auto cmdList = ctx.GetCommandList();

        auto& model = Resources::GetModel(modelId);
        if (model.DataSize == 0) {
            if (Game::GetState() == GameState::Editor && !Settings::Editor.HideUI)
                DrawObjectOutline(object, ctx.Camera);

            return;
        }

        if (ctx.ApplyEffect(effect)) {
            ctx.SetConstantBuffer(0, frameConstants.GetGPUVirtualAddress());
            effect.Shader->SetSampler(cmdList, GetWrappedTextureSampler());
            effect.Shader->SetNormalSampler(cmdList, GetNormalSampler());
            effect.Shader->SetTextureTable(cmdList, Render::Heaps->Materials.GetGpuHandle(0));
            effect.Shader->SetVClipTable(cmdList, Render::VClipBuffer->GetSRV());
            effect.Shader->SetMaterialInfoBuffer(cmdList, Render::MaterialInfoBuffer->GetSRV());
            effect.Shader->SetLightGrid(cmdList, Render::Adapter->LightGrid);
            auto cubeSrv = Render::Materials->EnvironmentCube.GetCubeSRV().GetGpuHandle();
            if (!cubeSrv.ptr) cubeSrv = Render::Adapter->NullCube.GetGpuHandle();
            effect.Shader->SetEnvironmentCube(cmdList, cubeSrv);
            effect.Shader->SetDissolveTexture(cmdList, Render::Materials->White().Handle());

            auto matcap = Render::Materials->Matcap.GetSRV();
            if (!matcap.ptr) matcap = Render::Materials->Black().Handle();
            effect.Shader->SetMatcap(cmdList, matcap);
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
            constants.EmissiveLight = Color(0, 0, 0);

            if (Game::GetState() == GameState::Editor) {
                if (Settings::Editor.RenderMode == RenderMode::Flat || Settings::Editor.RenderMode == RenderMode::Textured)
                    constants.Ambient = Color(1, 1, 1); // fullbright ambient in flat modes
                else if (auto seg = Game::Level.TryGetSegment(object.Segment))
                    constants.Ambient = seg->VolumeLight;
            }
            else {
                constants.Ambient = object.Ambient.GetValue().ToVector4();
            }
        }

        constants.TimeOffset = GetTimeOffset(object);

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

        auto& meshHandle = GetMeshHandle(modelId);

        for (int submodel = 0; submodel < model.Submodels.size(); submodel++) {
            auto world = GetSubmodelTransform(object, model, submodel) * transform;
            constants.World = world;

            // get the mesh associated with the submodel
            auto& subMesh = meshHandle.Meshes[submodel];

            for (int i = 0; i < subMesh.size(); i++) {
                auto mesh = subMesh[i];
                if (!mesh) continue;

                bool isTransparent = mesh->IsTransparent || transparentOverride;
                if (isTransparent && pass != RenderPass::Transparent) continue;
                if (!isTransparent && pass != RenderPass::Opaque) continue;

                if (isTransparent) {
                    auto& material = Resources::GetMaterial(mesh->Texture);
                    if (HasFlag(material.Flags, MaterialFlags::Additive))
                        ctx.ApplyEffect(Effects->ObjectGlow); // Additive blend
                    else
                        ctx.ApplyEffect(Game::OnTerrain && object.IsPlayer() ? Effects->TerrainObject : Effects->Object); // Alpha blend
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

    void DrawFoggedModel(GraphicsContext& ctx,
                         const Object& object,
                         ModelID modelId,
                         RenderPass pass,
                         const UploadBuffer<FrameConstants>& frameConstants) {
        if (object.IsCloaked() && Game::GetState() != GameState::Editor) {
            //DrawCloakedModel(ctx, object, modelId, pass);
            return;
        }

        auto env = Game::GetEnvironment(object);
        if (!env || !env->useFog) return;

        // todo: handle fog on terrain
        //auto& effect = Game::OnTerrain && object.IsPlayer() ? Effects->TerrainObject : Effects->Object;
        auto& effect = env->additiveFog ? Effects->AdditiveFogObject : Effects->FogObject;
        auto cmdList = ctx.GetCommandList();

        auto& model = Resources::GetModel(modelId);
        if (model.DataSize == 0) {
            if (Game::GetState() == GameState::Editor && !Settings::Editor.HideUI)
                DrawObjectOutline(object, ctx.Camera);

            return;
        }

        auto& depthTexture = Adapter->LinearizedDepthBuffer;
        depthTexture.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        if (ctx.ApplyEffect(effect)) {
            ctx.SetConstantBuffer(0, frameConstants.GetGPUVirtualAddress());
            effect.Shader->SetDepthTexture(cmdList, depthTexture.GetSRV());
        }

        FogObjectShader::Constants constants = {};

        constants.color = env->fog;

        Matrix transform = Matrix::CreateScale(object.Scale) * object.GetTransform(Game::LerpAmount);

        auto& meshHandle = GetMeshHandle(modelId);
        constants.ambient = object.Ambient.GetValue();

        for (int submodel = 0; submodel < model.Submodels.size(); submodel++) {
            auto world = GetSubmodelTransform(object, model, submodel) * transform;
            constants.World = world;

            // get the mesh associated with the submodel
            auto& subMesh = meshHandle.Meshes[submodel];

            for (int i = 0; i < subMesh.size(); i++) {
                auto mesh = subMesh[i];
                if (!mesh) continue;

                bool isTransparent = mesh->IsTransparent;
                if (isTransparent && pass != RenderPass::Transparent) continue;
                if (!isTransparent && pass != RenderPass::Opaque) continue;

                if (isTransparent) {
                    continue;
                }

                effect.Shader->SetConstants(cmdList, constants);

                cmdList->IASetVertexBuffers(0, 1, &mesh->VertexBuffer);
                cmdList->IASetIndexBuffer(&mesh->IndexBuffer);
                cmdList->DrawIndexedInstanced(mesh->IndexCount, 1, 0, 0, 0);
                Stats::DrawCalls++;
            }
        }
    }

    void DrawFoggedObject(GraphicsContext& ctx, const Object& object, RenderPass pass) {
        auto& frameConstants = Adapter->GetFrameConstants();

        if (object.Render.Type == RenderType::Model) {
            DrawFoggedModel(ctx, object, object.Render.Model.ID, pass, frameConstants);
        }
    }

    void DrawObject(GraphicsContext& ctx, const Object& object, RenderPass pass) {
        auto& frameConstants = Adapter->GetFrameConstants();

        switch (object.Type) {
            case ObjectType::Robot: {
                // could be transparent or opaque pass
                auto& info = Resources::GetRobotInfo(object.ID);
                DrawModel(ctx, object, info.Model, pass, frameConstants);
                break;
            }

            case ObjectType::Hostage: {
                if (pass != RenderPass::Transparent) return;
                auto up = object.Rotation.Up();
                DrawSprite(ctx, object, false, &up, Settings::Editor.RenderMode == RenderMode::Shaded);
                break;
            }

            case ObjectType::Powerup: {
                if (pass != RenderPass::Transparent) return;
                DrawSprite(ctx, object, false, nullptr, Settings::Editor.RenderMode == RenderMode::Shaded);
                break;
            }

            case ObjectType::Coop:
            case ObjectType::Player:
            case ObjectType::Reactor:
            case ObjectType::SecretExitReturn:
            case ObjectType::Marker: {
                DrawModel(ctx, object, object.Render.Model.ID, pass, frameConstants);
                break;
            }

            case ObjectType::Weapon:
                if (object.Render.Type == RenderType::None) {
                    // Do nothing, what did you expect?
                }
                else if (object.Render.Type == RenderType::Model) {
                    if (object.Render.Model.Outrage) {
                        DrawOutrageModel(ctx, object, pass);
                    }
                    else {
                        DrawModel(ctx, object, object.Render.Model.ID, pass, frameConstants);
                        auto inner = Resources::GameData.Weapons[object.ID].ModelInner;
                        if (object.Type == ObjectType::Weapon && inner > ModelID::None && inner != ModelID(255)) {
                            DrawModel(ctx, object, Resources::GameData.Weapons[object.ID].ModelInner, pass, frameConstants);
                        }
                    }
                }
                else {
                    if (pass != RenderPass::Transparent) return;
                    bool additive = object.ID != (int8)WeaponID::ProxMine && object.ID != (int8)WeaponID::SmartMine;
                    DrawSprite(ctx, object, additive, nullptr, !additive);
                }
                break;

            case ObjectType::Fireball: {
                if (pass != RenderPass::Transparent) return;
                if (object.Render.VClip.ID == VClipID::Matcen) {
                    auto up = object.Rotation.Up();
                    DrawSprite(ctx, object, true, &up);
                }
                else {
                    DrawSprite(ctx, object, true);
                }
                break;
            }

            case ObjectType::Debris:
                break;
            case ObjectType::Clutter:
                break;
        }
    }
}
