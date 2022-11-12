#include "pch.h"

#include "CommandContext.h"
#include "Game.h"
#include "Render.Editor.h"
#include "Render.h"

namespace Inferno::Render {
    using Graphics::GraphicsContext;

    // When up is provided, it constrains the sprite to that axis
    void DrawSprite(GraphicsContext& ctx,
                    const Object& object,
                    bool additive,
                    const Vector3* up = nullptr,
                    bool lit = false) {
        Color color = lit ? Game::Level.GetSegment(object.Segment).VolumeLight : Color(1, 1, 1);
        color += object.Render.Emissive;

        auto pos = object.GetPosition(Game::LerpAmount);

        if (object.Render.Type == RenderType::WeaponVClip ||
            object.Render.Type == RenderType::Powerup ||
            object.Render.Type == RenderType::Hostage) {
            auto& vclip = Resources::GetVideoClip(object.Render.VClip.ID);
            if (vclip.NumFrames == 0) {
                DrawObjectOutline(object);
                return;
            }

            auto tid = vclip.GetFrame((float)ElapsedTime);
            DrawBillboard(ctx, tid, pos, object.Radius, color, additive, object.Render.Rotation, up);
        }
        else if (object.Render.Type == RenderType::Laser) {
            // "laser" is used for still-image "blobs" like spreadfire
            auto& weapon = Resources::GetWeapon((WeaponID)object.ID);
            DrawBillboard(ctx, weapon.BlobBitmap, pos, object.Radius, color, additive, object.Render.Rotation, up);
        }
        else {
            DrawObjectOutline(object);
        }
    }

    // Draws a square glow that always faces the camera (Descent 3 submodels);
    void DrawObjectGlow(ID3D12GraphicsCommandList* cmd, float radius, const Color& color) {
        if (radius <= 0) return;
        const auto r = radius;
        ObjectVertex v0({ -r, r, 0 }, { 0, 0 }, color);
        ObjectVertex v1({ r, r, 0 }, { 1, 0 }, color);
        ObjectVertex v2({ r, -r, 0 }, { 1, 1 }, color);
        ObjectVertex v3({ -r, -r, 0 }, { 0, 1 }, color);

        // Horrible immediate mode nonsense
        Stats::DrawCalls++;
        g_SpriteBatch->Begin(cmd);
        g_SpriteBatch->DrawQuad(v0, v1, v2, v3);
        g_SpriteBatch->End();
    }


    void DrawOutrageModel(const Object& object,
                          ID3D12GraphicsCommandList* cmd,
                          int index,
                          bool transparentPass) {
        auto& meshHandle = GetOutrageMeshHandle(index);

        ObjectShader::Constants constants = {};
        auto& seg = Game::Level.GetSegment(object.Segment);
        constants.EmissiveLight = object.Render.Emissive;
        constants.Ambient = Settings::Editor.RenderMode == RenderMode::Shaded ? seg.VolumeLight : Color(1, 1, 1);

        Matrix transform = object.GetTransform();
        transform.Forward(-transform.Forward()); // flip z axis to correct for LH models

        auto model = Resources::GetOutrageModel(TEST_MODEL);
        if (model == nullptr) return;

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
                //auto smPos = Vector3::Transform(Vector3::Zero, world);
                //auto billboard = Matrix::CreateBillboard(smPos, Camera.Position, Camera.Up);
                constants.World = world;
                //constants.Projection = billboard * ViewProjection;
            }
            else {
                if (submodel.HasFlag(SubmodelFlag::Rotate))
                    world = Matrix::CreateFromAxisAngle(submodel.Keyframes[1].Axis, DirectX::XM_2PI * submodel.Rotation * (float)Render::ElapsedTime) * world;

                constants.World = world;
                //constants.Projection = world * ViewProjection;
            }

            //constants.Time = (float)ElapsedTime;

            // get the mesh associated with the submodel
            for (auto& [i, mesh] : submesh) {

                auto& material = Render::NewTextureCache->GetTextureInfo(model->TextureHandles[i]);
                bool transparent = material.Saturate() || material.Alpha();

                if ((transparentPass && !transparent) || (!transparentPass && transparent))
                    continue; // skip saturate textures unless on glow pass

                auto handle = i >= 0 ?
                    Render::NewTextureCache->GetResource(model->TextureHandles[i], (float)ElapsedTime) :
                    Materials->White.Handles[0];

                bool additive = material.Saturate() || submodel.HasFlag(SubmodelFlag::Facing);

                auto& effect = additive ? Effects->ObjectGlow : Effects->Object;
                effect.Apply(cmd);
                effect.Shader->SetSampler(cmd, GetTextureSampler());
                effect.Shader->SetMaterial(cmd, handle);

                if (transparentPass && submodel.HasFlag(SubmodelFlag::Facing)) {
                    if (material.Saturate())
                        constants.Ambient = Color(1, 1, 1);
                    //constants.Colors[1] = Color(1, 1, 1, 1);
                    effect.Shader->SetConstants(cmd, constants);
                    DrawObjectGlow(cmd, submodel.Radius, Color(1, 1, 1, 1));
                }
                else {
                    //constants.Colors[1] = material.Color; // color 1 is used for texture alpha
                    effect.Shader->SetConstants(cmd, constants);
                    cmd->IASetVertexBuffers(0, 1, &mesh->VertexBuffer);
                    cmd->IASetIndexBuffer(&mesh->IndexBuffer);
                    cmd->DrawIndexedInstanced(mesh->IndexCount, 1, 0, 0, 0);
                    Stats::DrawCalls++;
                }
            }
        }
    }

    void DrawModel(GraphicsContext& ctx, 
                   const Object& object, 
                   ModelID modelId, 
                   RenderPass pass, 
                   TexID texOverride = TexID::None) {
        auto& effect = Effects->Object;
        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, Adapter->FrameConstantsBuffer.GetGPUVirtualAddress());
        auto cmdList = ctx.CommandList();

        auto& model = Resources::GetModel(modelId);
        if (model.DataSize == 0) {
            DrawObjectOutline(object);
            return;
        }

        auto& meshHandle = GetMeshHandle(modelId);

        effect.Shader->SetSampler(cmdList, GetTextureSampler());
        auto& seg = Game::Level.GetSegment(object.Segment);
        ObjectShader::Constants constants = {};

        if (object.Render.Emissive != Color(0, 0, 0)) {
            // Change the ambient color to white if object has any emissivity
            constants.Ambient = Color(1, 1, 1);
            constants.EmissiveLight = object.Render.Emissive;
        }
        else {
            constants.Ambient = Settings::Editor.RenderMode == RenderMode::Shaded ? seg.VolumeLight : Color(1, 1, 1);
            constants.EmissiveLight = Color(0, 0, 0);
        }

        Matrix transform = Matrix::Lerp(object.GetLastTransform(), object.GetTransform(), Game::LerpAmount);
        transform.Forward(-transform.Forward()); // flip z axis to correct for LH models
        const float vclipOffset = (float)object.Signature * 0.762f; // randomize vclips across objects

        for (int submodel = 0; submodel < model.Submodels.size(); submodel++) {
            // accumulate the offsets for each submodel
            auto submodelOffset = model.GetSubmodelOffset(submodel);
            auto world = Matrix::CreateTranslation(submodelOffset) * transform;
            constants.World = world;
            effect.Shader->SetConstants(cmdList, constants);

            // get the mesh associated with the submodel
            auto& subMesh = meshHandle.Meshes[submodel];

            for (int i = 0; i < subMesh.size(); i++) {
                auto mesh = subMesh[i];
                if (!mesh) continue;

                TexID tid = texOverride;
                if (texOverride == TexID::None)
                    tid = mesh->EffectClip == EClipID::None ? mesh->Texture : Resources::GetEffectClip(mesh->EffectClip).VClip.GetFrame(ElapsedTime + vclipOffset);

                auto& ti = Resources::GetTextureInfo(tid);
                if (ti.Transparent && pass != RenderPass::Transparent) continue;
                if (!ti.Transparent && pass != RenderPass::Opaque) continue;

                const Material2D& material = tid == TexID::None ? Materials->White : Materials->Get(tid);
                effect.Shader->SetMaterial(cmdList, material);

                cmdList->IASetVertexBuffers(0, 1, &mesh->VertexBuffer);
                cmdList->IASetIndexBuffer(&mesh->IndexBuffer);
                cmdList->DrawIndexedInstanced(mesh->IndexCount, 1, 0, 0, 0);
                Stats::DrawCalls++;
            }
        }
    }

    void DrawObject(GraphicsContext& ctx, const Object& object, RenderPass pass) {
        switch (object.Type) {
            case ObjectType::Robot:
            {
                // could be transparent or opaque pass
                auto& info = Resources::GetRobotInfo(object.ID);
                auto texOverride = Resources::LookupLevelTexID(object.Render.Model.TextureOverride);
                DrawModel(ctx, object, info.Model, pass, texOverride);
                break;
            }

            case ObjectType::Hostage:
            {
                if (pass != RenderPass::Transparent) return;
                auto up = object.Rotation.Up();
                DrawSprite(ctx, object, false, &up, Settings::Editor.RenderMode == RenderMode::Shaded);
                break;
            }

            case ObjectType::Coop:
            case ObjectType::Player:
            case ObjectType::Reactor:
            case ObjectType::SecretExitReturn:
            case ObjectType::Marker:
            {
                auto texOverride = Resources::LookupLevelTexID(object.Render.Model.TextureOverride);
                DrawModel(ctx, object, object.Render.Model.ID, pass, texOverride);
                break;
            }

            case ObjectType::Weapon:
                if (object.Render.Type == RenderType::None) {
                    // Do nothing, what did you expect?
                }
                else if (object.Render.Type == RenderType::Model) {
                    auto texOverride = Resources::LookupLevelTexID(object.Render.Model.TextureOverride);
                    DrawModel(ctx, object, object.Render.Model.ID, pass, texOverride);
                    if (object.Type == ObjectType::Weapon && Resources::GameData.Weapons[object.ID].ModelInner > ModelID::None) {
                        DrawModel(ctx, object, Resources::GameData.Weapons[object.ID].ModelInner, pass, texOverride);
                    }
                }
                else {
                    if (pass != RenderPass::Transparent) return;
                    bool additive = object.ID != (int8)WeaponID::ProxMine && object.ID != (int8)WeaponID::SmartMine;
                    DrawSprite(ctx, object, additive);
                }
                break;

            case ObjectType::Fireball:
            {
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

            case ObjectType::Powerup:
            {
                if (pass != RenderPass::Transparent) return;
                DrawSprite(ctx, object, false);
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

}
