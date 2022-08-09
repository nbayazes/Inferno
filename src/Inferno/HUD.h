#pragma once

#include "Graphics/Render.h"
#include "Graphics/Render.Canvas.h"

namespace Inferno {

    enum class Gauges {
        Shield = 0, // 0 to 9 in decreasing strength
        Invincible = 10, // 10 to 19
        Afterburner = 20,
        BlueKey = 24,
        GoldKey = 25,
        RedKey = 26,
        //BlueKeyOff = 27,
        //GoldKeyOff = 28,
        //RedKeyOff = 29,
        //BlueKeySB = 30,
        Lives = 37,
        Ship = 38, // 8 Colors
        ReticleCross = 46, // 2 frames: not ready, ready
        ReticlePrimary = 48, // 3 frames: not ready, center ready, quad ready
        ReticleSecondary = 51, // 5 frames: not ready, left ready, right ready, center not ready, center ready
        HomingWarningOn = 56,
        HomingWarningOff = 57,
    };


    constexpr float WEAPON_TEXT_Y_OFFSET = -75;
    constexpr float WEAPON_BMP_Y_OFFSET = -20;
    constexpr Color MonitorGreenText = { 0, 0.6f, 0 };
    constexpr Color MonitorRedText = { 0.6f, 0, 0 };

    void DrawMonitorBitmap(Render::CanvasBitmapInfo& info, float shadow = 0.6f) {
        Render::HudGlowCanvas->DrawBitmap(info);

        info.Scanline = 0.0f;
        info.Color = { 0, 0, 0, shadow };
        Render::HudCanvas->DrawBitmap(info);
    }

    // Draws text with a dark background, easier to read
    void DrawMonitorText(string_view text, Render::DrawTextInfo& info, float shadow = 0) {
        Render::HudGlowCanvas->DrawGameText(text, info);
        info.Color = { 0, 0, 0, shadow };
        info.Scanline = 0.0f;
        Render::HudCanvas->DrawGameText(text, info);
    }


    void DrawReticleBitmap(const Vector2& offset, Gauges gauge, int frame) {
        TexID id = Resources::GameData.HiResGauges[(int)gauge + frame];
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

    void DrawShipBitmap(const Vector2& offset, Gauges gauge) {
        TexID id = Resources::GameData.HiResGauges[(int)gauge];
        auto& ti = Resources::GetTextureInfo(id);
        auto scale = Render::HudCanvas->GetScale();

        Inferno::Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = Vector2((float)ti.Width, (float)ti.Height) * scale;
        info.Texture = Render::Materials->Get(id).Handles[Material2D::Diffuse];
        info.HorizontalAlign = AlignH::Center;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = 0.5f;
        DrawMonitorBitmap(info, 0.95f);
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

    void DrawAdditiveBitmap(const Vector2& offset, AlignH align, Gauges gauge, float scanline = 0.4f) {
        TexID id = Resources::GameData.HiResGauges[(int)gauge];
        auto& ti = Resources::GetTextureInfo(id);
        auto scale = Render::HudCanvas->GetScale();

        Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = Vector2((float)ti.Width, (float)ti.Height) * scale;
        info.Texture = Render::Materials->Get(id).Handles[Material2D::Diffuse];
        info.HorizontalAlign = align;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = scanline;
        Render::HudGlowCanvas->DrawBitmap(info);
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
        const Vector2 crossOffset(0/*-8*/, -5);
        const Vector2 primaryOffset(0/*-30*/, 14);
        const Vector2 secondaryOffset(0/*-24*/, 2);

        DrawReticleBitmap(crossOffset, Gauges::ReticleCross, 1); // gauss, vulkan
        DrawReticleBitmap(primaryOffset, Gauges::ReticlePrimary, 2);
        DrawReticleBitmap(secondaryOffset, Gauges::ReticleSecondary, 4);

        //TexID id = Resources::GameData.HiResGauges[RETICLE_PRIMARY];
        //auto& ti = Resources::GetTextureInfo(id);
        //auto pos = size / 2;
        //auto ratio = size.y / BASE_RESOLUTION_Y;

        //Vector2 scaledSize = { ti.Width * ratio, ti.Height * ratio };
        //pos -= scaledSize / 2;
        //Render::Canvas->DrawBitmap(id, pos, scaledSize);
    }

    void DrawEnergyBar(float spacing, bool flipX) {
        constexpr float ENERGY_HEIGHT = -125;
        constexpr float ENERGY_SPACING = -9;

        auto& material = Render::Materials->GetOutrageMaterial("gauge03b");
        auto scale = Render::HudCanvas->GetScale();
        Render::CanvasBitmapInfo info;
        info.Position = Vector2(spacing + (flipX ? ENERGY_SPACING : -ENERGY_SPACING), ENERGY_HEIGHT) * scale;
        info.Size = { (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        info.Size *= scale;
        info.Texture = material.Handles[0];
        info.Scanline = 1.0f;
        if (flipX) {
            info.UV1.x = 0;
            info.UV0.x = 1;
        }
        info.HorizontalAlign = flipX ? AlignH::CenterRight : AlignH::CenterLeft;
        info.VerticalAlign = AlignV::Bottom;
        //info.Color.w = 0.5f;

        Render::HudGlowCanvas->DrawBitmap(info);
        //info.Color *= 0.5f;
        //Render::HudCanvas->DrawBitmap(info);
    }

    void DrawLeftMonitor(float x) {
        DrawOpaqueBitmap({ x, 0 }, AlignH::CenterLeft, "cockpit-left");

        auto scale = Render::HudCanvas->GetScale();

        {
            Render::DrawTextInfo info;
            info.Font = FontSize::Small;
            info.Color = MonitorGreenText;
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

        DrawEnergyBar(x, false);

        DrawAdditiveBitmap({ x - 151, -38 }, AlignH::CenterLeft, Gauges::Afterburner);
    }

    void DrawRightMonitor(float x) {
        DrawOpaqueBitmap({ x, 0 }, AlignH::CenterRight, "cockpit-right");

        auto scale = Render::HudCanvas->GetScale();
        Render::DrawTextInfo info;
        info.Font = FontSize::Small;
        info.Color = MonitorGreenText;
        info.Position = Vector2(x + 25, WEAPON_TEXT_Y_OFFSET) * scale;
        info.HorizontalAlign = AlignH::CenterRight; // Justify the left edge of the text to the center
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = 0.5f;
        DrawMonitorText("CONCSN\nMISSILE", info);

        // Ammo counter
        info.Color = MonitorRedText;
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

        DrawEnergyBar(x, true);


        // Bomb counter
        info.Color = MonitorRedText;
        info.Position = Vector2(x + 158, -26) * scale;
        info.HorizontalAlign = AlignH::CenterRight;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = 0.5f;
        DrawMonitorText("B:04", info);

        // Draw Keys
        DrawAdditiveBitmap({ x + 147, -90 }, AlignH::CenterRight, Gauges::BlueKey, 1.25f);
        DrawAdditiveBitmap({ x + 147 + 2, -90 + 21 }, AlignH::CenterRight, Gauges::GoldKey, 1.25f);
        DrawAdditiveBitmap({ x + 147 + 4, -90 + 42 }, AlignH::CenterRight, Gauges::RedKey, 1.25f);
    }

    void DrawCenterMonitor() {
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

        DrawShipBitmap({ 0, -29 }, Gauges::Shield);
        DrawShipBitmap({ 0, -40 }, Gauges::Ship);
    }

    void DrawHUD() {
        float spacing = 100;
        DrawLeftMonitor(-spacing);
        DrawRightMonitor(spacing);
        DrawCenterMonitor();

        DrawReticle();
    }

}