#include "pch.h"
#include "Render.Briefing.h"
#include "Buffers.h"
#include "CameraContext.h"
#include "Object.h"
#include "Render.h"
#include "Resources.h"
#include "ShaderLibrary.h"

namespace Inferno::Render {
    Inferno::Camera BriefingCamera;

    void DrawBriefingModel(GraphicsContext& ctx,
                           const Object& object,
                           const UploadBuffer<FrameConstants>& frameConstants) {
        auto& effect = Effects->BriefingObject;
        auto cmdList = ctx.GetCommandList();

        auto& model = Resources::GetModel(object.Render.Model.ID);

        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, frameConstants.GetGPUVirtualAddress());
        effect.Shader->SetSampler(cmdList, GetWrappedTextureSampler());
        effect.Shader->SetNormalSampler(cmdList, GetNormalSampler());
        effect.Shader->SetTextureTable(cmdList, Render::Heaps->Materials.GetGpuHandle(0));
        effect.Shader->SetVClipTable(cmdList, Render::VClipBuffer->GetSRV());
        effect.Shader->SetMaterialInfoBuffer(cmdList, Render::MaterialInfoBuffer->GetSRV());
        effect.Shader->SetLightGrid(cmdList, Render::Adapter->LightGrid);
        auto cubeSrv = Render::Materials->EnvironmentCube.GetCubeSRV().GetGpuHandle();
        if (!cubeSrv.ptr)cubeSrv = Render::Adapter->NullCube.GetGpuHandle();
        effect.Shader->SetEnvironmentCube(cmdList, cubeSrv);
        effect.Shader->SetDissolveTexture(cmdList, Render::Materials->White().Handle());
        effect.Shader->SetMatcap(cmdList, Render::Materials->Matcap.GetSRV());

        ObjectShader::Constants constants = {};

        if (object.Render.Emissive != Color(0, 0, 0)) {
            // Ignore ambient if object is emissive
            constants.Ambient = Color(0, 0, 0);
            constants.EmissiveLight = object.Render.Emissive;
        }
        else {
            constants.Ambient = object.Ambient.GetValue().ToVector4();
            constants.EmissiveLight = Color(0, 0, 0);
        }

        constants.TimeOffset = 0;

        Matrix transform = object.GetTransform();
        constants.TexIdOverride = -1;

        auto& meshHandle = GetMeshHandle(object.Render.Model.ID);

        for (int submodel = 0; submodel < model.Submodels.size(); submodel++) {
            auto world = GetSubmodelTransform(object, model, submodel) * transform;
            constants.World = world;

            // get the mesh associated with the submodel
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

    void DrawBriefingObject(GraphicsContext& ctx, const Object& object) {
        auto& target = Adapter->GetBriefingRobotBuffer();
        target.Transition(ctx.GetCommandList(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto& depthTarget = Adapter->GetBriefingRobotDepthBuffer();
        ctx.ClearColor(target);
        ctx.ClearDepth(depthTarget);
        ctx.SetRenderTarget(target.GetRTV(), depthTarget.GetDSV());

        ctx.SetViewportAndScissor(target.GetSize());

        auto& model = Resources::GetModel(object.Render.Model.ID);
        if (model.DataSize != 0) {
            // Update barriers and light grid state, can't rely on the level to do it
            MaterialInfoBuffer->Transition(ctx.GetCommandList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            VClipBuffer->Transition(ctx.GetCommandList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            Graphics::Lights.Dispatch(ctx);

            // spin and animate
            auto& frameConstants = Adapter->GetBriefingFrameConstants();
            BriefingCamera.SetPosition(Vector3(0, model.Radius * .5f, -model.Radius * 3.0f));
            BriefingCamera.SetFov(45);
            BriefingCamera.SetViewport(target.GetSize());
            BriefingCamera.UpdatePerspectiveMatrices();
            UpdateFrameConstants(BriefingCamera, frameConstants);

            ctx.GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            Render::DrawBriefingModel(ctx, object, frameConstants);

            if (Settings::Graphics.MsaaSamples > 1) {
                Adapter->BriefingRobot.ResolveFromMultisample(ctx.GetCommandList(), Adapter->BriefingRobotMsaa);
            }
        }

        Adapter->BriefingRobot.Transition(ctx.GetCommandList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void DrawBriefing(GraphicsContext& ctx, RenderTarget& target, const BriefingState& briefing) {
        PIXScopedEvent(ctx.GetCommandList(), PIX_COLOR_INDEX(10), "Briefing");
        ctx.ClearColor(target);

        // Update the light grid in briefing mode, as the level won't do it for us
        if (Game::GetState() == GameState::Briefing)
            Render::Adapter->LightGrid.SetLightConstants(Adapter->BriefingRobot.GetSize());

        if (auto screen = briefing.GetScreen()) {
            if (auto page = briefing.GetPage()) {
                Vector2 scale(1, 1);
                if (briefing.IsDescent1) {
                    scale.x = 640.0f / 320;
                    scale.y = 480.0f / 200;
                }

                if (auto object = briefing.GetObject())
                    DrawBriefingObject(ctx, *object);

                ctx.SetRenderTarget(target.GetRTV());
                ctx.SetViewportAndScissor(target.GetSize());
                BriefingCanvas->SetSize(640, 480); // Always use 640x480 regardless of actual resolution

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
                //info.Scale = 2;
                info.Color = Color(0, 1, 0);
                info.TabStop = screen->TabStop * scale.x;
                BriefingCanvas->DrawFadingText(page->Text, info,
                                               Game::Briefing.GetElapsed(),
                                               BRIEFING_TEXT_SPEED, screen->Cursor);

                // interpolate when downsampling
                auto sampler = Render::Adapter->GetHeight() < target.GetHeight() ? Heaps->States.LinearClamp() : Heaps->States.PointClamp();
                BriefingCanvas->Render(ctx, sampler);
            }
        }

        target.Transition(ctx.GetCommandList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
}
