#pragma once

#include "Graphics/Render.h"
#include "Graphics/Render.Canvas.h"

namespace Inferno {
    //constexpr int BASE_RESOLUTION_Y = 480; // 'high-res' assets base resolution

    // Draws text with a dark background, easier to read
    void DrawMonitorBitmap(Render::CanvasBitmapInfo& info, float shadow = 0.6f) {
        Render::HudGlowCanvas->DrawBitmap(info);

        info.Scanline = 0.0f;
        info.Color = { 0, 0, 0, shadow };
        Render::HudCanvas->DrawBitmap(info);
    }

    void DrawMonitorText(string_view text, Render::DrawTextInfo& info, float shadow = 0) {
        Render::HudGlowCanvas->DrawGameText(text, info);
        info.Color = { 0, 0, 0, shadow };
        info.Scanline = 0.0f;
        Render::HudCanvas->DrawGameText(text, info);
    }


    void DrawReticleBitmap(const Vector2& offset, int gauge) {
        TexID id = Resources::GameData.HiResGauges[gauge];
        auto& ti = Resources::GetTextureInfo(id);
        auto scale = Render::HudCanvas->GetScale();

        Inferno::Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = Vector2((float)ti.Width, (float)ti.Height) * scale;
        info.Texture = Render::Materials->Get(id).Handles[Material2D::Diffuse];
        info.HorizontalAlign = AlignH::Center;
        info.VerticalAlign = AlignV::CenterTop;
        info.Scanline = 0.4f;
        Render::HudCanvas->DrawBitmap(info);
    }

    void DrawShipBitmap(const Vector2& offset, int gauge) {
        TexID id = Resources::GameData.HiResGauges[gauge];
        auto& ti = Resources::GetTextureInfo(id);
        auto scale = Render::HudCanvas->GetScale();

        Inferno::Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = Vector2((float)ti.Width, (float)ti.Height) * scale;
        info.Texture = Render::Materials->Get(id).Handles[Material2D::Diffuse];
        info.HorizontalAlign = AlignH::Center;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = 0.5f;
        DrawMonitorBitmap(info);
    }

    void DrawOpaqueBitmap(const Vector2& offset, AlignH align, string bitmapName) {
        auto& material = Render::Materials->GetOutrageMaterial(bitmapName);

        auto scale = Render::HudCanvas->GetScale();
        Inferno::Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = { (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        info.Size *= scale;
        info.Texture = material.Handles[0];
        info.HorizontalAlign = align;
        info.VerticalAlign = AlignV::Bottom;
        Render::HudCanvas->DrawBitmap(info);
    }

    void DrawWeaponBitmap(const Vector2& offset, AlignH align, TexID id) {
        Render::LoadTextureDynamic(id);
        auto& ti = Resources::GetTextureInfo(id);
        auto scale = Render::HudCanvas->GetScale();

        Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = Vector2((float)ti.Width, (float)ti.Height) * scale;
        info.Texture = Render::Materials->Get(id).Handles[Material2D::Diffuse];
        info.HorizontalAlign = align;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = 0.4f;
        DrawMonitorBitmap(info);
    }

    void DrawReticle() {
        constexpr auto RETICLE_CROSS = 46;
        constexpr auto RETICLE_PRIMARY = 48; // not ready, +1 center ready, +2 quad ready
        constexpr auto RETICLE_SECONDARY = 51; // not ready, left ready, right ready, center not ready (wrong alignment), center ready
        //constexpr auto RETICLE_LAST = 55;

        const Vector2 crossOffset(0/*-8*/, -5);
        const Vector2 primaryOffset(0/*-30*/, 14);
        const Vector2 secondaryOffset(0/*-24*/, 2);

        DrawReticleBitmap(crossOffset, RETICLE_CROSS + 1); // gauss, vulkan
        DrawReticleBitmap(primaryOffset, RETICLE_PRIMARY + 2);
        DrawReticleBitmap(secondaryOffset, RETICLE_SECONDARY + 4);

        //TexID id = Resources::GameData.HiResGauges[RETICLE_PRIMARY];
        //auto& ti = Resources::GetTextureInfo(id);
        //auto pos = size / 2;
        //auto ratio = size.y / BASE_RESOLUTION_Y;

        //Vector2 scaledSize = { ti.Width * ratio, ti.Height * ratio };
        //pos -= scaledSize / 2;
        //Render::Canvas->DrawBitmap(id, pos, scaledSize);
    }

    void DrawAfterburner() {

    }

    constexpr float WEAPON_TEXT_Y_OFFSET = -75;
    constexpr float WEAPON_BMP_Y_OFFSET = -20;
    constexpr Color MonitorTextColor = { 0, 0.8f, 0 };

    void DrawLeftMonitor(float x) {
        DrawOpaqueBitmap({ x, 0 }, AlignH::CenterLeft, "cockpit-left");

        auto scale = Render::HudCanvas->GetScale();

        {
            Render::DrawTextInfo info;
            info.Font = FontSize::Small;
            info.Color = MonitorTextColor;
            info.Position = Vector2(x - 90, WEAPON_TEXT_Y_OFFSET) * scale;
            info.HorizontalAlign = AlignH::CenterRight; // Justify the left edge of the text to the center
            info.VerticalAlign = AlignV::Bottom;
            info.Scanline = 0.5f;
            DrawMonitorText("S.LASER\nLVL: 5", info);
        }

        {
            auto texId = Resources::GameData.Weapons[30].HiresIcon;
            DrawWeaponBitmap({ x - 100, WEAPON_BMP_Y_OFFSET }, AlignH::CenterLeft, texId);
        }

        DrawAfterburner();
    }

    void DrawRightMonitor(float x) {
        DrawOpaqueBitmap({ x, 0 }, AlignH::CenterRight, "cockpit-right");

        auto scale = Render::HudCanvas->GetScale();
        Render::DrawTextInfo info;
        info.Font = FontSize::Small;
        info.Color = MonitorTextColor;
        info.Position = Vector2(x + 25, WEAPON_TEXT_Y_OFFSET) * scale;
        info.HorizontalAlign = AlignH::CenterRight; // Justify the left edge of the text to the center
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = 0.5f;
        DrawMonitorText("CONCSN\nMISSILE", info);

        // Ammo counter
        info.Color = { 0.8f, 0, 0 };
        info.Position = Vector2(x + 50, WEAPON_TEXT_Y_OFFSET + 15) * scale;
        info.HorizontalAlign = AlignH::CenterRight;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = 0.5f;
        DrawMonitorText("004", info);

        {
            // concussion
            auto texId = Resources::GameData.Weapons[8].HiresIcon;
            DrawWeaponBitmap({ x + 75, WEAPON_BMP_Y_OFFSET }, AlignH::CenterRight, texId);
        }

        // Draw Keys
        // Draw bomb count
    }

    void DrawCenterMonitor() {
        constexpr auto SHIELD = 0; // 0 to 9 in decreasing strength
        constexpr auto SHIPS = 38; // 8 Colors

        DrawOpaqueBitmap({ 0, 0 }, AlignH::Center, "cockpit-ctr");
        // Draw shields, invuln state, shield / energy count

        {
            auto scale = Render::HudCanvas->GetScale();
            Render::DrawTextInfo info;
            info.Font = FontSize::Small;
            info.Color = { 0.54f, 0.54f, 0.71f };
            info.Position = Vector2(2, -120) * scale;
            info.HorizontalAlign = AlignH::Center;
            info.VerticalAlign = AlignV::Bottom;
            info.Scanline = 0.5f;
            DrawMonitorText("100", info, 0.5f);
            //info.Scanline = 0.0f;
            //info.Color *= 0.1;
            //info.Color.z = 0.8f;

            info.Color = { 0.78f, 0.56f, 0.18f };
            info.Position = Vector2(2, -150) * scale;
            info.Scanline = 0.5f;
            DrawMonitorText("100", info, 0.5f);
        }

        DrawShipBitmap({ 0, -29 }, SHIELD);
        DrawShipBitmap({ 0, -40 }, SHIPS);
    }

    void DrawHUD() {
        float spacing = 100;
        DrawLeftMonitor(-spacing);
        DrawRightMonitor(spacing);
        DrawCenterMonitor();

        DrawReticle();
    }

}