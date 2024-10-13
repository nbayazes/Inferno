#include "pch.h"
#include "Render.Automap.h"
#include "CameraContext.h"
#include "Colors.h"
#include "Game.Automap.h"
#include "Render.h"
#include "Render.Level.h"
#include "SystemClock.h"
#include "Render.Debug.h"
#include "Resources.h"

namespace Inferno::Render {
    namespace {
        constexpr Color TEXT_COLOR(0.2f, 1.25f, 0.2f);
        //constexpr Color DISABLED_TEXT(0.4f, 0.4f, 0.4f);
        constexpr Color DISABLED_TEXT(0.15f, 0.30f, 0.15f);
    }

    void DrawAutomapModel(GraphicsContext& ctx,
                          const Object& object,
                          ModelID modelId,
                          const Color& color,
                          const UploadBuffer<FrameConstants>& frameConstants) {
        auto& effect = Effects->AutomapObject;
        auto cmdList = ctx.GetCommandList();

        auto& model = Resources::GetModel(modelId);
        if (model.DataSize == 0) return;

        // Most of these do nothing with the automap shader, but it's simpler to match the existing object shader input
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
        constants.Ambient = color; // Ambient is reused as the object color
        constants.EmissiveLight = Color(0, 0, 0);
        constants.TimeOffset = 0;

        Matrix transform = Matrix::CreateScale(object.Scale) * object.GetTransform(Game::LerpAmount);
        constants.TexIdOverride = (int)TexID::None;

        auto& meshHandle = GetMeshHandle(modelId);

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

    float GetAutomapAnimation() {
        return float((sin(Clock.GetTotalTimeSeconds() * 4) + 1) * 0.5f + 0.65f);
    }

    float GetFixedScale(const Vector3& position, const Camera& camera, float scale = 40.0f) {
        auto target = position - camera.Position;
        auto right = camera.GetRight();
        // project the target onto the camera plane so panning does not cause scaling.
        auto projection = target.Dot(right) * right;
        auto distance = (target - projection).Length();
        return distance / scale;
    }

    void DrawAutomap(GraphicsContext& ctx) {
        if (!LevelResources.AutomapMeshes) return;

        auto cmdList = ctx.GetCommandList();
        auto& target = Adapter->GetRenderTarget();
        auto& depthBuffer = Adapter->GetDepthBuffer();

        // Clear depth and color buffers
        ctx.SetViewportAndScissor(UINT(target.GetWidth() * Settings::Graphics.RenderScale), UINT(target.GetHeight() * Settings::Graphics.RenderScale));
        target.Transition(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);

        ctx.ClearDepth(depthBuffer);
        ctx.ClearStencil(Adapter->GetDepthBuffer(), 0);

        ctx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Depth prepass

        {
            BeginDepthPrepass(ctx);

            auto& effect = Effects->DepthCutout;
            ctx.ApplyEffect(effect);
            ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
            auto& shader = effect.Shader;

            shader->SetSampler(cmdList, GetWrappedTextureSampler());
            effect.Shader->SetTextureTable(cmdList, Render::Heaps->Materials.GetGpuHandle(0));

            for (auto& wall : LevelResources.AutomapMeshes->Walls) {
                if (!wall.Mesh.IsValid()) continue;

                auto& texture = Materials->Get(wall.Texture);
                auto& decal = Materials->Get(wall.Decal);

                DepthCutoutShader::Constants constants{};
                constants.Threshold = 0.01f;
                constants.HasOverlay = wall.Decal > TexID::None;

                effect.Shader->SetConstants(cmdList, constants);

                shader->SetDiffuse1(cmdList, texture.Handle());
                shader->SetDiffuse2(cmdList, decal.Handle());
                shader->SetSuperTransparent(cmdList, decal);

                cmdList->IASetVertexBuffers(0, 1, &wall.Mesh.VertexBuffer);
                cmdList->IASetIndexBuffer(&wall.Mesh.IndexBuffer);
                cmdList->DrawIndexedInstanced(wall.Mesh.IndexCount, 1, 0, 0, 0);
            }

            if (Settings::Graphics.MsaaSamples > 1) {
                // must resolve MS target to allow shader sampling
                Adapter->LinearizedDepthBuffer.ResolveFromMultisample(cmdList, Adapter->MsaaLinearizedDepthBuffer);
                Adapter->MsaaLinearizedDepthBuffer.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }

            Adapter->LinearizedDepthBuffer.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }

        // Draw geometry
        ctx.SetRenderTarget(target.GetRTV(), depthBuffer.GetDSV());
        ctx.ClearColor(target, nullptr, &Colors::AutomapBackground);

        depthBuffer.Transition(cmdList, D3D12_RESOURCE_STATE_DEPTH_WRITE);

        auto animation = GetAutomapAnimation();
        auto animate = [&](Color color) {
            color *= animation;
            color.w = Saturate(color.w);
            //color.x *= animation;
            //color.y *= animation;
            //color.z *= animation;
            return color;
        };

        auto drawMesh = [&](AutomapMeshInstance& wall) {
            if (!wall.Mesh.IsValid()) return;

            auto& texture = Materials->Get(wall.Texture);
            auto& decal = Materials->Get(wall.Decal);
            AutomapShader::Constants constants;

            constants.Color = [&wall, &animate] {
                switch (wall.Type) {
                    default:
                    case AutomapType::Normal: return Color(0.1f, 0.6f, 0.1f);
                    case AutomapType::Door: return animate(Colors::Door);
                    case AutomapType::LockedDoor: return animate(Colors::LockedDoor);
                    case AutomapType::GoldDoor: return animate(Colors::DoorGold);
                    case AutomapType::RedDoor: return animate(Colors::DoorRed);
                    case AutomapType::BlueDoor: return animate(Colors::DoorBlue);
                    case AutomapType::FullMap: return Colors::Revealed;
                    case AutomapType::Fuelcen: return animate(Colors::Fuelcen * 1.25f);
                    case AutomapType::Reactor: return animate(Colors::Reactor);
                    case AutomapType::Unrevealed: return animate(Colors::Unexplored);
                    case AutomapType::Matcen: return animate(Colors::Matcen);
                }
            }();

            constants.Flat = [&wall] {
                switch (wall.Type) {
                    case AutomapType::Unrevealed:
                    case AutomapType::GoldDoor:
                    case AutomapType::RedDoor:
                    case AutomapType::BlueDoor:
                    case AutomapType::Door:
                    case AutomapType::LockedDoor:
                    case AutomapType::Fuelcen:
                    case AutomapType::Reactor:
                    case AutomapType::Matcen:
                        return true;
                    default:
                        return false;
                }
            }();

            constants.HasOverlay = wall.Decal > TexID::None;

            auto& shader = Shaders->Automap;
            shader.SetDepth(cmdList, Adapter->LinearizedDepthBuffer.GetSRV());
            shader.SetSampler(cmdList, GetWrappedTextureSampler());
            shader.SetConstants(cmdList, constants);

            shader.SetDiffuse1(cmdList, Materials->White().Handle());
            shader.SetDiffuse2(cmdList, Materials->White().Handle());
            shader.SetMask(cmdList, Materials->White().Handle());

            if (wall.Type == AutomapType::Fuelcen ||
                wall.Type == AutomapType::Reactor ||
                wall.Type == AutomapType::Matcen ||
                wall.Type == AutomapType::Unrevealed) {
                // Don't use textures for special rooms or unrevealed
                shader.SetDiffuse1(cmdList, Materials->White().Handle());
                shader.SetDiffuse2(cmdList, Materials->White().Handle());
                shader.SetMask(cmdList, Materials->White().Handle());
            }
            else {
                shader.SetDiffuse1(cmdList, texture.Handle());
                shader.SetDiffuse2(cmdList, decal.Handle());
                shader.SetMask(cmdList, decal.Handles[Material2D::SuperTransparency]);
            }

            cmdList->IASetVertexBuffers(0, 1, &wall.Mesh.VertexBuffer);
            cmdList->IASetIndexBuffer(&wall.Mesh.IndexBuffer);
            cmdList->DrawIndexedInstanced(wall.Mesh.IndexCount, 1, 0, 0, 0);
        };

        ctx.ApplyEffect(Effects->Automap);
        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());

        for (auto& wall : LevelResources.AutomapMeshes->Walls) {
            drawMesh(wall);
        }

        for (auto& wall : LevelResources.AutomapMeshes->FullmapWalls) {
            drawMesh(wall);
        }

        auto drawCircle = [&animation](const Object& obj, float size, Color color, bool constSize = false) {
            auto& camera = Game::GetActiveCamera();
            if (constSize) size *= GetFixedScale(obj.Position, camera, 100);
            color *= animation;
            color.w = 1;
            Debug::DrawSolidCircle(obj.Position, size, color, camera, 32);
        };

        for (auto& obj : Game::Level.Objects) {
            auto segInfo = Seq::tryItem(Game::Automap.Segments, (int)obj.Segment);

            if (obj.Type == ObjectType::Hostage) {
                drawCircle(obj, 6, Colors::Hostage);
            }

            if (!segInfo || *segInfo == AutomapVisibility::Hidden)
                continue; // Only hostages are drawn if unrevealed

            if (obj.Type == ObjectType::Powerup) {
                if (obj.IsPowerup(PowerupID::KeyBlue)) {
                    drawCircle(obj, 10, Colors::DoorBlue);
                }
                else if (obj.IsPowerup(PowerupID::KeyGold)) {
                    drawCircle(obj, 10, Colors::DoorGold);
                }
                else if (obj.IsPowerup(PowerupID::KeyRed)) {
                    drawCircle(obj, 10, Colors::DoorRed);
                }
            }
            else if (obj.Type == ObjectType::Reactor && !Game::Level.HasBoss) {
                auto color = Colors::Reactor * animation;
                color.w = 1;
                DrawAutomapModel(ctx, obj, obj.Render.Model.ID, color, Adapter->GetFrameConstants());
            }
            else if (obj.Type == ObjectType::Player && obj.ID == 0) {
                auto color = Colors::Player * animation;
                color.w = 1;
                DrawAutomapModel(ctx, obj, obj.Render.Model.ID, color, Adapter->GetFrameConstants());
            }
        }

        ctx.ApplyEffect(Effects->AutomapTransparent);
        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());

        for (auto& wall : LevelResources.AutomapMeshes->TransparentWalls) {
            drawMesh(wall);
        }

        depthBuffer.Transition(cmdList, D3D12_RESOURCE_STATE_DEPTH_READ);

        // outline pass
        ctx.ApplyEffect(Effects->AutomapOutline);
        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
        Effects->AutomapOutline.Shader->SetDepth(cmdList, Adapter->LinearizedDepthBuffer.GetSRV());
        cmdList->DrawInstanced(3, 1, 0, 0);
    }


    void DrawAutomapText(GraphicsContext& ctx) {
        // Reuse the HUD canvas for the automap
        auto canvas = HudCanvas.get();
        auto width = Adapter->GetWidth();
        auto height = Adapter->GetHeight();
        HudCanvas->SetSize(width, height);

        //const float scale = Render::Canvas->GetScale();
        constexpr float margin = 20;
        constexpr float lineHeight = 15;


        //{
        //    // Draw backgrounds
        //    auto black = Materials->Black().Handle();
        //    Color background(1, 1, 1, 0.6f);
        //    CanvasBitmapInfo rect({ 0, 0 }, { 100, 50 }, black, background);

        //    rect.HorizontalAlign = AlignH::Left;
        //    rect.VerticalAlign = AlignV::Top;
        //    rect.Size = Vector2(200, margin * 2 + lineHeight * 4);
        //    canvas->DrawBitmapScaled(rect);

        //    rect.HorizontalAlign = AlignH::Right;
        //    rect.VerticalAlign = AlignV::Top;
        //    rect.Size.x = MeasureString(Game::Level.Name, FontSize::Small).x + margin * 2;
        //    rect.Size.y = margin * 2 + lineHeight * 2;
        //    canvas->DrawBitmapScaled(rect);

        //    rect.HorizontalAlign = AlignH::Left;
        //    rect.VerticalAlign = AlignV::Bottom;
        //    rect.Size = Vector2(300, margin * 2 + lineHeight * 4);
        //    canvas->DrawBitmapScaled(rect);

        //    rect.HorizontalAlign = AlignH::Right;
        //    rect.VerticalAlign = AlignV::Bottom;
        //    rect.Size = Vector2(180, margin * 2 + lineHeight * 6);
        //    canvas->DrawBitmapScaled(rect);
        //}

        {
            Render::DrawTextInfo title;
            title.Position = Vector2(-margin, margin);
            title.HorizontalAlign = AlignH::Right;
            title.VerticalAlign = AlignV::Top;
            title.Font = FontSize::Small;
            //title.Scale = 0.75;
            title.Color = TEXT_COLOR;
            canvas->DrawGameText(Game::Level.Name, title);

            title.Position.y += lineHeight;
            canvas->DrawGameText(Game::Automap.LevelNumber, title);

            title.Position.y += lineHeight;
            canvas->DrawGameText(Game::Automap.Threat, title);

            if (!Game::Automap.HostageText.empty()) {
                title.Position.y += lineHeight;
                canvas->DrawGameText(Game::Automap.HostageText, title);
            }
        }

        auto animation = GetAutomapAnimation();
        constexpr float scanline = 0.2f;

        {
            Render::DrawTextInfo info;
            info.Position = Vector2(margin, margin);
            info.HorizontalAlign = AlignH::Left;
            info.VerticalAlign = AlignV::Top;
            info.Font = FontSize::Small;
            //info.Scale = 1 / scale * 2;
            info.Scale = 1;
            info.Color = TEXT_COLOR;
            info.TabStop = 20;
            info.Scanline = scanline;
            canvas->DrawGameText("Navigation:", info);
            info.Position.y += lineHeight;
            info.Color = Game::Automap.FoundEnergy ? TEXT_COLOR : DISABLED_TEXT;
            canvas->DrawGameText("1.\tEnergy center", info);
            info.Position.y += lineHeight;
            info.Color = Game::Automap.FoundReactor ? TEXT_COLOR : DISABLED_TEXT;
            canvas->DrawGameText("2.\tReactor", info);
            info.Position.y += lineHeight;
            info.Color = Game::Automap.FoundExit ? TEXT_COLOR : DISABLED_TEXT;
            canvas->DrawGameText("3.\tEmergency Exit", info);

            //auto drawItem = [&info, &cursor, lineHeight](string_view label, string_view text) {
            //    cursor.y += lineHeight;
            //    info.Position = cursor;
            //    canvas->DrawGameText(label, info);

            //    info.Position.x += 40;
            //    canvas->DrawGameText(text, info);
            //};

            //canvas->DrawGameText("Navigation", info);
            //drawItem("1.", "Energy center");
            //drawItem("2.", "Reactor");
            //drawItem("3.", "Exit");
        }

        {
            Render::DrawTextInfo info;
            info.HorizontalAlign = AlignH::Left;
            info.VerticalAlign = AlignV::Bottom;
            info.Font = FontSize::Small;
            info.Color = TEXT_COLOR;
            info.Position = Vector2(margin, -margin - lineHeight * 3);
            info.TabStop = 150;
            info.Scanline = scanline;

            // todo: change help text based on automap control mode (orbit or flight)
            canvas->DrawGameText("flight:\tMove view", info);
            info.Position.y += lineHeight;
            canvas->DrawGameText("afterburner:\tcenter on ship", info);
            info.Position.y += lineHeight;
            canvas->DrawGameText("primary fire:\tzoom in", info);
            info.Position.y += lineHeight;
            canvas->DrawGameText("secondary fire:\tzoom out", info);
        }

        {
            Vector2 rectSz{ 10, 10 };

            Render::DrawTextInfo info;
            info.HorizontalAlign = AlignH::Right;
            info.VerticalAlign = AlignV::Bottom;
            info.Font = FontSize::Small;
            info.Color = TEXT_COLOR;
            info.Scanline = scanline;
            info.Position = Vector2(-margin - rectSz.x - 2, -margin - lineHeight * 5);

            auto addHelp = [&](string_view str, const Color& color) {
                canvas->DrawGameText(str, info);
                auto white = Materials->White().Handles[Material2D::Diffuse];
                CanvasBitmapInfo rect({ -margin, info.Position.y }, rectSz, white, color * animation, AlignH::Right, AlignV::Bottom);
                rect.Scanline = 0.15f;
                canvas->DrawBitmapScaled(rect);
                info.Position.y += lineHeight;
            };

            addHelp("Unexplored", Colors::Unexplored);
            addHelp("Door", Colors::Door);
            addHelp("Locked door", Colors::LockedDoor);
            addHelp("Energy center", Colors::Fuelcen);
            addHelp("Matcen", Colors::Matcen);
            addHelp("Reactor", Colors::Reactor);
        }

        HudCanvas->Render(ctx);
        HudGlowCanvas->Render(ctx);
    }
}
