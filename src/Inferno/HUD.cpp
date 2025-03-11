#include "pch.h"
#include "HUD.h"
#include "Game.h"
#include "GameTimer.h"
#include "Graphics.h"
#include "Graphics/Render.h"
#include "Resources.h"
#include "SoundSystem.h"

namespace Inferno {
    namespace {
        constexpr float WEAPON_TEXT_X_OFFSET = -90;
        constexpr float WEAPON_TEXT_Y_OFFSET = 140;
        constexpr float WEAPON_TEXT_AMMO_Y_OFFSET = WEAPON_TEXT_Y_OFFSET + +FONT_LINE_SPACING * 2 + 20;
        constexpr float WEAPON_BMP_Y_OFFSET = -20;
        constexpr float WEAPON_BMP_X_OFFSET = -135;
        constexpr Color GREEN_TEXT = { 0, 1.0f, 0 };
        constexpr Color RED_TEXT = { 0.80f, 0, 0 };
        constexpr Color GOLD_TEXT = { 0.78f, 0.56f, 0.18f };
        constexpr float MONITOR_BRIGHTNESS = 1.20f;

        constexpr float IMAGE_SCANLINE = 0.2f;
        constexpr float TEXT_SCANLINE = 0.5f;
        constexpr float MONITOR_AMBIENT_SCALE = 0.25f;
        constexpr float BASE_SCORE_WINDOW = 3.0f;
        constexpr float GLARE = 0.125f;

        Color Ambient, Direct;
    }

    enum class Gauges {
        Shield = 0, // 0 to 9 in decreasing strength
        Invincible = 10, // 10 to 19
        Afterburner = 20,
        BlueKey = 24,
        GoldKey = 25,
        RedKey = 26,
        Lives = 37,
        Ship = 38, // 8 Colors
        ReticleCross = 46, // 2 frames: not ready, ready
        ReticlePrimary = 48, // 3 frames: not ready, center ready, quad ready
        ReticleSecondary = 51, // 5 frames: not ready, left ready, right ready, center not ready, center ready
        HomingWarningOn = 56,
        HomingWarningOff = 57,
    };

    enum FadeState { FadeNone, FadeIn, FadeOut };

    void ApplyAmbient(Color& color, const Color& ambient) {
        color.x += ambient.x * MONITOR_AMBIENT_SCALE;
        color.y += ambient.y * MONITOR_AMBIENT_SCALE;
        color.z += ambient.z * MONITOR_AMBIENT_SCALE;
    }

    class MonitorState {
        FadeState _state{};
        int _requested = -1; // The requested weapon
        int _requestedLaserLevel = -1;
        bool _primary;

    public:
        MonitorState(bool primary) : _primary(primary) {}

        int WeaponIndex = -1; // The visible weapon
        int LaserLevel = -1; // The visible laser level
        float Opacity = 0; // Fade out/in based on rearm time / 2

        // Instantly reset transitions
        void Reset() {
            WeaponIndex = -1;
        }

        void Update(float dt, const Player& player, int weapon) {
            // Laser tier can be downgraded if thief steals the super laser

            bool laserTierChanged = _primary && (
                                        (player.LaserLevel > MAX_LASER_LEVEL && LaserLevel <= MAX_LASER_LEVEL) ||
                                        (player.LaserLevel <= MAX_LASER_LEVEL && LaserLevel > MAX_LASER_LEVEL));

            if (_requested != weapon || (laserTierChanged && weapon == (int)PrimaryWeaponIndex::Laser)) {
                _state = FadeOut;
                //Opacity = player.RearmTime;
                _requested = weapon;
                _requestedLaserLevel = player.LaserLevel;
            }

            if (!laserTierChanged)
                LaserLevel = _requestedLaserLevel = player.LaserLevel; // keep laser immediately in sync unless tier changes

            if (WeaponIndex == -1) {
                // initial load, draw current weapon
                WeaponIndex = _requested = weapon;
                LaserLevel = _requestedLaserLevel = player.LaserLevel;
                Opacity = 1;
                _state = FadeNone;
            }

            if (_state == FadeOut) {
                Opacity -= dt * player.RearmTime * 2;
                if (Opacity <= 0) {
                    Opacity = 0;
                    _state = FadeIn;
                    WeaponIndex = _requested; // start showing the requested weapon
                    LaserLevel = _requestedLaserLevel; // show the requested laser
                }
            }
            else if (_state == FadeIn) {
                if (_requested != weapon) {
                    _state = FadeOut; // weapon was changed while swapping
                }
                else {
                    Opacity += dt * player.RearmTime * 2;
                    if (Opacity >= 1) {
                        _state = FadeNone;
                        Opacity = 1;
                    }
                }
            }
        }
    };

    TexID GetGaugeTexID(Gauges gauge) {
        return Game::Level.IsDescent1() ? Resources::GameData.Gauges[(int)gauge] : Resources::GameData.HiResGauges[(int)gauge];
    }

    TexID GetWeaponTexID(const Weapon& weapon) {
        return Game::Level.IsDescent1() ? weapon.Icon : weapon.HiresIcon;
    }

    void DrawMonitorBitmap(Render::CanvasBitmapInfo& info, float shadow = 0.6f) {
        Render::HudGlowCanvas->DrawBitmapScaled(info);

        info.Scanline = 0.0f;
        info.Color = Color{ 0, 0, 0, shadow };
        Render::HudCanvas->DrawBitmapScaled(info);
    }

    // Draws text with a dark background, easier to read
    void DrawMonitorText(string_view text, Render::DrawTextInfo info, float shadow = 0.6f) {
        info.Color.x *= MONITOR_BRIGHTNESS;
        info.Color.y *= MONITOR_BRIGHTNESS;
        info.Color.z *= MONITOR_BRIGHTNESS;

        Render::HudGlowCanvas->DrawText(text, info);
        info.Color = Color{ 0, 0, 0, shadow };
        info.Scanline = 0.0f;
        Render::HudCanvas->DrawText(text, info);
    }

    void DrawReticleBitmap(const Vector2& offset, Gauges gauge, int frame, float scale) {
        TexID id = GetGaugeTexID(Gauges((int)gauge + frame));
        auto& material = Render::Materials->Get(id);

        Inferno::Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = Vector2{ (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        info.Size *= scale;
        info.Texture = material.Handles[Material2D::Diffuse];
        info.HorizontalAlign = AlignH::Center;
        info.VerticalAlign = AlignV::CenterTop;
        info.Scanline = 0.0f;
        Render::HudCanvas->DrawBitmapScaled(info);

        info.Scanline = 0.4f;
        Render::HudCanvas->DrawBitmapScaled(info);
    }

    void DrawReticleBitmap(const Vector2& offset, const string& name, float scale) {
        auto& material = Render::Materials->Get(name);

        Inferno::Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = Vector2{ (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        info.Size *= scale;
        info.Texture = material.Handles[Material2D::Diffuse];
        info.HorizontalAlign = AlignH::Center;
        info.VerticalAlign = AlignV::CenterTop;
        info.Scanline = 0.0f;
        Render::HudCanvas->DrawBitmapScaled(info);

        info.Scanline = 0.4f;
        Render::HudCanvas->DrawBitmapScaled(info);
    }

    void DrawOpaqueBitmap(const Vector2& offset, AlignH align, const Material2D& material, const Color& color) {
        Inferno::Render::CanvasBitmapInfo info;
        info.Position = offset;
        info.Size = Vector2{ (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        info.Texture = material.Handle();
        info.HorizontalAlign = align;
        info.VerticalAlign = AlignV::Bottom;
        info.Color = color;
        info.Color.w = Saturate(info.Color.w);
        Render::HudCanvas->DrawBitmapScaled(info);
    }

    void DrawOpaqueBitmap(const Vector2& offset, AlignH align, const string& bitmapName, const Color& color) {
        auto& material = Render::Materials->Get(bitmapName);
        DrawOpaqueBitmap(offset, align, material, color);
    }

    void DrawAdditiveBitmap(const Vector2& offset, AlignH align, const Material2D& material, float sizeScale, float scanline, const Color& color, bool mirrorX = false) {
        Render::CanvasBitmapInfo info;
        info.Position = offset;
        info.Size = Vector2{ (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        info.Size *= sizeScale;
        info.Texture = material.Handles[Material2D::Diffuse];
        info.HorizontalAlign = align;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = scanline;
        info.MirrorX = mirrorX;
        info.Color = color;
        Render::HudGlowCanvas->DrawBitmapScaled(info);
    }

    void DrawAdditiveBitmap(const Vector2& offset, AlignH align, Gauges gauge, float sizeScale, float scanline = 0.4f, bool mirrorX = false) {
        TexID id = GetGaugeTexID(gauge);
        auto& material = Render::Materials->Get(id);
        DrawAdditiveBitmap(offset, align, material, sizeScale, scanline, Color(2, 2, 2), mirrorX);
    }

    //void DrawAdditiveBitmap(const Vector2& offset, AlignH align, const string& bitmapName, float sizeScale, float scanline = 0.4f) {
    //    auto& material = Render::Materials->Get(bitmapName);
    //    DrawAdditiveBitmap(offset, align, material, sizeScale, scanline);
    //}

    void DrawWeaponBitmap(const Vector2& offset, AlignH align, TexID id, float sizeScale, float alpha) {
        Graphics::LoadTexture(id);
        auto& ti = Resources::GetTextureInfo(id);

        Render::CanvasBitmapInfo info;
        info.Position = offset;
        info.Size = Vector2(ti.Width, ti.Height) * sizeScale;
        info.Texture = Render::Materials->Get(id).Handles[Material2D::Diffuse];
        info.HorizontalAlign = align;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = IMAGE_SCANLINE;
        info.Color = Color{ MONITOR_BRIGHTNESS, MONITOR_BRIGHTNESS, MONITOR_BRIGHTNESS, alpha };
        ApplyAmbient(info.Color, Ambient + Direct * GLARE);
        DrawMonitorBitmap(info, 0.6f * alpha);
    }

    void DrawReticle() {
        //auto isD1 = Game::Level.IsDescent1();
        //const Vector2 crossOffset(0, isD1 ? -2.0f : -5.0f);
        //const Vector2 primaryOffset(0, isD1 ? 6.0f : 14.0f);
        //const Vector2 secondaryOffset(0, isD1 ? 1.0f : 2.0f);

        constexpr Vector2 crossOffset(0, -5.0f);
        constexpr Vector2 primaryOffset(0, 14.0f);
        constexpr Vector2 secondaryOffset(0, 2.0f);

        bool primaryReady = Game::Player.CanFirePrimary(Game::Player.Primary) && Game::Player.PrimaryDelay <= 0;
        bool secondaryReady = Game::Player.CanFireSecondary(Game::Player.Secondary) && Game::Player.SecondaryDelay <= 0;
        //float scale = isD1 ? 2.0f : 1.0f;
        float scale = 1;
        // cross deactivates when no primary or secondary weapons are available
        int crossFrame = primaryReady || secondaryReady ? 1 : 0;

        bool quadLasers = Game::Player.HasPowerup(PowerupFlag::QuadFire) && Game::Player.Primary == PrimaryWeaponIndex::Laser;
        int primaryFrame = primaryReady ? (quadLasers ? 2 : 1) : 0;
        auto cross = fmt::format("targ01#{}", crossFrame);
        DrawReticleBitmap(crossOffset, fmt::format("targ01b#{}", crossFrame), scale); // gauss, vulkan
        DrawReticleBitmap(primaryOffset, fmt::format("targ02b#{}", primaryFrame), scale);

        int secondaryFrame = secondaryReady;
        static constexpr uint8_t Secondary_weapon_to_gun_num[10] = { 4, 4, 7, 7, 7, 4, 4, 7, 4, 7 };

        if (Secondary_weapon_to_gun_num[(int)Game::Player.Secondary] == 7)
            secondaryFrame += 3; //now value is 0,1 or 3,4
        else if (secondaryFrame && !(Game::Player.SecondaryFiringIndex & 1))
            secondaryFrame++;

        DrawReticleBitmap(secondaryOffset, fmt::format("targ03b#{}", secondaryFrame), scale);
    }

    void DrawEnergyBar(float spacing, bool flipX, float energy) {
        constexpr float ENERGY_HEIGHT = -125;
        constexpr float ENERGY_SPACING = -9;
        auto percent = std::lerp(0.115f, 1.0f, energy / 100);

        auto& material = Render::Materials->Get("gauge03b");
        auto scale = Render::HudCanvas->GetScale();
        Vector2 pos = Vector2(spacing + (flipX ? ENERGY_SPACING : -ENERGY_SPACING), ENERGY_HEIGHT) * scale;
        Vector2 size = { (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        size *= scale;

        auto halign = flipX ? AlignH::CenterRight : AlignH::CenterLeft;
        auto alignment = Render::GetAlignment(size, halign, AlignV::Bottom, Render::HudGlowCanvas->GetSize());
        Color color = { MONITOR_BRIGHTNESS, MONITOR_BRIGHTNESS, MONITOR_BRIGHTNESS };
        ApplyAmbient(color, Ambient);

        // Adjust for percentage
        auto offset = size.x * (1 - percent);
        auto v0 = Vector2{ pos.x + offset, pos.y + size.y } + alignment;
        auto v1 = Vector2{ pos.x + size.x, pos.y + size.y } + alignment;
        auto v2 = Vector2{ pos.x + size.x, pos.y } + alignment;
        auto v3 = Vector2{ pos.x + offset + size.y, pos.y } + alignment;

        auto angleRatio = size.y / size.x; // for 45 degree angle
        Vector2 uv0 = { 1.0f - percent, 1 };
        Vector2 uv1 = { 1, 1 };
        Vector2 uv2 = { 1, 0 };
        Vector2 uv3 = { 1.0f - percent + angleRatio, 0 };

        if (flipX) {
            auto off = v2.x;
            auto flip = [&](float& x) { x = -(x - off) + off - size.x; };
            flip(v0.x);
            flip(v1.x);
            flip(v2.x);
            flip(v3.x);
        }

        Render::HudCanvasPayload payload{};
        payload.V0 = { v0, uv0, color }; // bottom left
        payload.V1 = { v1, uv1, color }; // bottom right
        payload.V2 = { v2, uv2, color }; // top right
        payload.V3 = { v3, uv3, color }; // top left
        payload.Texture = material.Handle();
        payload.Scanline = 0.4f;

        Render::HudGlowCanvas->Draw(payload);
        Render::HudGlowCanvas->Draw(payload);
    }

    void DrawAfterburnerBar(float x, const Player& player) {
        if (!player.HasPowerup(PowerupFlag::Afterburner))
            return;

        float percent = player.AfterburnerCharge;
        auto scale = Render::HudCanvas->GetScale();
        auto pos = Vector2{ x - 151, -37 } * scale;
        auto& material = Render::Materials->Get("gauge02b");
        Vector2 size = {
            (float)material.Textures[0].GetWidth() * scale,
            (float)material.Textures[0].GetHeight() * percent * scale
        };

        auto alignment = Render::GetAlignment(size, AlignH::CenterLeft, AlignV::Bottom, Render::HudGlowCanvas->GetSize());
        float uvTop = 1 - percent;

        Color color = { MONITOR_BRIGHTNESS, MONITOR_BRIGHTNESS, MONITOR_BRIGHTNESS, 1 };
        ApplyAmbient(color, Ambient + Direct * GLARE);

        Render::HudCanvasPayload info{};
        info.V0 = { Vector2{ pos.x, pos.y + size.y } + alignment, { 0, 1 }, color }; // bottom left
        info.V1 = { Vector2{ pos.x + size.x, pos.y + size.y } + alignment, { 1, 1 }, color }; // bottom right
        info.V2 = { Vector2{ pos.x + size.x, pos.y } + alignment, { 1, uvTop }, color }; // top right
        info.V3 = { Vector2{ pos.x, pos.y } + alignment, { 0, uvTop }, color }; // top left
        info.Texture = material.Handle();
        info.Scanline = IMAGE_SCANLINE;
        Render::HudGlowCanvas->Draw(info);
    }

    // convert '1' characters to a special fixed width version. Only works with small font.
    void UseWide1Char(string& s) {
        for (auto& c : s)
            if (c == '1') c = static_cast<char>(132);
    }

    void DrawLeftMonitor(float x, const MonitorState& state, const Player& player) {
        DrawOpaqueBitmap({ x, 0 }, AlignH::CenterLeft, "cockpit-left", Ambient + Direct);

        auto weaponIndex = (PrimaryWeaponIndex)state.WeaponIndex;

        Render::DrawTextInfo info;
        info.Font = FontSize::Small;
        info.Color = GREEN_TEXT;
        info.Color.w = state.Opacity;
        ApplyAmbient(info.Color, Ambient + Direct * GLARE);
        info.Position = Vector2(x + WEAPON_TEXT_X_OFFSET, WEAPON_TEXT_Y_OFFSET);
        info.HorizontalAlign = AlignH::CenterRight; // Justify the left edge of the text to the center
        info.VerticalAlign = AlignV::CenterTop;
        info.Scanline = TEXT_SCANLINE;
        auto weaponName = Resources::GetPrimaryNameShort(weaponIndex);
        string label = string(weaponName), ammo;
        auto& weapon = player.Ship.Weapons[(int)weaponIndex];

        switch (weaponIndex) {
            case PrimaryWeaponIndex::Laser:
            {
                if (player.HasPowerup(PowerupFlag::QuadFire))
                    label = fmt::format("laser\nlvl: {}\nquad", state.LaserLevel + 1);
                else
                    label = fmt::format("laser\nlvl: {}", state.LaserLevel + 1);
                break;
            }
            case PrimaryWeaponIndex::SuperLaser:
                label = "napalm";
                break;

            case PrimaryWeaponIndex::Vulcan:
            case PrimaryWeaponIndex::Gauss:
                ammo = fmt::format("{:05}", player.PrimaryAmmo[weapon.AmmoType]);
                break;

            case PrimaryWeaponIndex::Omega:
                ammo = fmt::format("{:.0f}%", player.OmegaCharge * 100);
                break;
        }

        DrawMonitorText(label, info, 0.6f * state.Opacity);

        if (!ammo.empty()) {
            // Ammo counter
            info.Color = RED_TEXT;
            info.Color.w = state.Opacity;
            ApplyAmbient(info.Color, Ambient + Direct * GLARE);
            info.Position = Vector2(x + WEAPON_TEXT_X_OFFSET + 5, WEAPON_TEXT_AMMO_Y_OFFSET);
            info.HorizontalAlign = AlignH::CenterRight;
            info.VerticalAlign = AlignV::CenterTop;
            info.Scanline = TEXT_SCANLINE;
            UseWide1Char(ammo);
            DrawMonitorText(ammo, info, 0.6f * state.Opacity);
        }

        // todo: omega charge

        {
            float resScale = Game::Level.IsDescent1() ? 2.0f : 1.0f; // todo: check resource path instead?
            WeaponID wid =
                weaponIndex == PrimaryWeaponIndex::Laser && state.LaserLevel >= 4 ? WeaponID::Laser5 : PrimaryToWeaponID[(int)weaponIndex];

            auto texId = GetWeaponTexID(Resources::GetWeapon(wid));

            DrawWeaponBitmap({ x + WEAPON_BMP_X_OFFSET, WEAPON_BMP_Y_OFFSET }, AlignH::CenterRight, texId, resScale, state.Opacity);
        }

        DrawEnergyBar(x, false, player.Energy);
        DrawAfterburnerBar(x, player);
    }

    void DrawRightMonitor(float x, const MonitorState& state, const Player& player) {
        DrawOpaqueBitmap({ x, 0 }, AlignH::CenterRight, "cockpit-right", Ambient + Direct);

        Render::DrawTextInfo info;
        info.Font = FontSize::Small;
        info.Color = GREEN_TEXT;
        info.Color.w = state.Opacity;
        ApplyAmbient(info.Color, Ambient + Direct * GLARE);
        info.Position = Vector2(x + 25, WEAPON_TEXT_Y_OFFSET);
        info.HorizontalAlign = AlignH::CenterRight; // Justify the left edge of the text to the center
        info.VerticalAlign = AlignV::CenterTop;
        info.Scanline = TEXT_SCANLINE;
        DrawMonitorText(Resources::GetSecondaryNameShort((SecondaryWeaponIndex)state.WeaponIndex), info, 0.6f * state.Opacity);

        // Ammo counter
        info.Color = RED_TEXT;
        info.Color.w = state.Opacity;
        ApplyAmbient(info.Color, Ambient + Direct * GLARE);
        info.Position = Vector2(x + 35, WEAPON_TEXT_AMMO_Y_OFFSET);
        info.HorizontalAlign = AlignH::CenterRight;
        info.VerticalAlign = AlignV::CenterTop;
        info.Scanline = TEXT_SCANLINE;
        auto ammo = fmt::format("{:03}", player.SecondaryAmmo[state.WeaponIndex]);
        UseWide1Char(ammo);
        DrawMonitorText(ammo, info, 0.6f * state.Opacity);

        float resScale = Game::Level.IsDescent1() ? 2.0f : 1.0f;
        {
            auto texId = GetWeaponTexID(Resources::GetWeapon(SecondaryToWeaponID[state.WeaponIndex]));
            DrawWeaponBitmap({ x + 75, WEAPON_BMP_Y_OFFSET }, AlignH::CenterRight, texId, resScale, state.Opacity);
        }

        DrawEnergyBar(x, true, player.Energy);

        {
            auto bomb = player.GetActiveBomb();

            // Bomb counter
            info.Color = bomb == SecondaryWeaponIndex::ProximityMine ? RED_TEXT : GOLD_TEXT;
            ApplyAmbient(info.Color, Ambient + Direct * GLARE);
            info.Position = Vector2(x + 157, -26);
            info.HorizontalAlign = AlignH::CenterRight;
            info.VerticalAlign = AlignV::Bottom;
            info.Scanline = TEXT_SCANLINE;

            auto bombs = player.SecondaryAmmo[(int)bomb];
            if (bombs > 0)
                DrawMonitorText(fmt::format("B:{:02}", std::min(bombs, (uint16)99)), info);
        }

        // Draw Keys
        bool mirrorX = Game::Level.IsDescent1();
        float keyScanline = 0.0f;
        if (player.HasPowerup(PowerupFlag::BlueKey)) {
            auto& blue = Render::Materials->Get("gauge02b#0");
            if (blue.Pointer())
                DrawAdditiveBitmap({ x + 147, -90 }, AlignH::CenterRight, blue, 1, keyScanline, Color(0.0f, 0.0f, 2), false);
            else
                DrawAdditiveBitmap({ x + 147, -90 }, AlignH::CenterRight, Gauges::BlueKey, resScale, keyScanline, mirrorX);
        }

        if (player.HasPowerup(PowerupFlag::GoldKey)) {
            auto& yellow = Render::Materials->Get("gauge02b#1");
            if (yellow.Pointer())
                DrawAdditiveBitmap({ x + 147 + 2, -90 + 21 }, AlignH::CenterRight, yellow, 1, keyScanline, Color(1.1f, 1.1f, 1.1f), false);
            else
                DrawAdditiveBitmap({ x + 147 + 2, -90 + 21 }, AlignH::CenterRight, Gauges::GoldKey, resScale, keyScanline, mirrorX);
        }

        if (player.HasPowerup(PowerupFlag::RedKey)) {
            auto& red = Render::Materials->Get("gauge02b#2");
            if (red.Pointer())
                DrawAdditiveBitmap({ x + 147 + 4, -90 + 42 }, AlignH::CenterRight, red, 1, keyScanline, Color(1.75f, 0, 0), false);
            else
                DrawAdditiveBitmap({ x + 147 + 4, -90 + 42 }, AlignH::CenterRight, Gauges::RedKey, resScale, keyScanline, mirrorX);
        }
    }

    void DrawHighlights(bool flip, float opacity = 0.07f) {
        auto& material = Render::Materials->Get("SmHilite");
        auto scale = Render::HudCanvas->GetScale() * 1.5f;
        auto& screen = Render::HudCanvas->GetSize();
        int fl = flip ? 1 : -1;

        auto height = (float)material.Textures[0].GetWidth() * scale;
        auto width = (float)material.Textures[0].GetHeight() * scale * fl;

        Color color(1, 1, 1, opacity);

        constexpr int steps = 16;
        constexpr float vStep = 1.0f / steps;
        const float yStep = height / steps * 0.75f;
        float offset = screen.x / 2 + 150 * scale * fl;
        float yOffset = 10 * scale;

        for (int i = 0; i < steps; i++) {
            Render::HudCanvasPayload payload;
            payload.Texture = material.Handle();

            float x0 = -cos((steps - i) * 3.14f / steps / 2 + 0.2f) * width * scale * 0.7f + offset;
            float x1 = -cos((steps - i - 1) * 3.14f / steps / 2 + 0.2f) * width * scale * 0.7f + offset;
            float y0 = yOffset + yStep * float(i);
            float y1 = yOffset + yStep * float(i + 1);

            Vector2 v0 = { x0, y0 };
            Vector2 v1 = { x0 + width * 2, y0 };
            Vector2 v2 = { x1 + width * 2, y1 };
            Vector2 v3 = { x1, y1 };

            payload.V0 = HudVertex{ v0, { 1 - vStep * float(i), 0 }, color }; // bottom left
            payload.V1 = HudVertex{ v1, { 1 - vStep * float(i), 1 }, color }; // bottom right
            payload.V2 = HudVertex{ v2, { 1 - vStep * float(i + 1), 1 }, color }; // top right
            payload.V3 = HudVertex{ v3, { 1 - vStep * float(i + 1), 0 }, color }; // top left
            Render::HudGlowCanvas->Draw(payload);
        }
    }

    float GetCloakAlpha(const Object& player) {
        if (!player.IsCloaked()) return 1;
        if (player.Effects.CloakDuration < 0) return 0;

        auto timer = player.Effects.CloakTimer;
        auto remaining = player.Effects.CloakDuration - timer;

        if (timer < 0.5f) {
            // Cloak just picked up, fade out
            return 1 - timer / 0.5f;
        }
        else if (remaining < 2.5f) {
            // Cloak close to expiring, fade in/out every 0.5 seconds for 2.5 seconds
            constexpr float p = 1; // period
            return 2 * std::abs((remaining + 0.5f) / p - std::floor((remaining + 0.5f) / p + 0.5f));
        }
        else {
            return 0;
        }
    }

    void DrawShipBitmap(const Vector2& offset, const Material2D& material, float sizeScale, float alpha) {
        Inferno::Render::CanvasBitmapInfo info;
        info.Position = offset;
        info.Size = Vector2{ (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        info.Size *= sizeScale;
        info.Texture = material.Handles[Material2D::Diffuse];
        info.HorizontalAlign = AlignH::Center;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = IMAGE_SCANLINE;
        info.Color = Color{ MONITOR_BRIGHTNESS, MONITOR_BRIGHTNESS, MONITOR_BRIGHTNESS, alpha };
        ApplyAmbient(info.Color, Ambient + Direct * GLARE);
        Render::HudGlowCanvas->DrawBitmapScaled(info);
    }

    void DrawCenterMonitor(const Player& player) {
        DrawOpaqueBitmap({ 0, 0 }, AlignH::Center, "cockpit-ctr", Ambient + Direct);
        // Draw shields, invuln state, shield / energy count

        {
            Render::DrawTextInfo info;
            info.Font = FontSize::Small;
            info.Color = Color{ 0.54f, 0.54f, 0.71f };
            ApplyAmbient(info.Color, Ambient + Direct * GLARE);

            info.Position = Vector2(2, -120);
            info.HorizontalAlign = AlignH::Center;
            info.VerticalAlign = AlignV::Bottom;
            info.Scanline = TEXT_SCANLINE;
            auto shields = fmt::format("{:.0f}", player.Shields < 0 ? 0 : std::floor(player.Shields));
            DrawMonitorText(shields, info, 0.5f);

            info.Color = GOLD_TEXT;
            ApplyAmbient(info.Color, Ambient + Direct);
            info.Position = Vector2(2, -150);
            info.Scanline = TEXT_SCANLINE;
            auto energy = fmt::format("{:.0f}", player.Energy < 0 ? 0 : std::floor(player.Energy));
            DrawMonitorText(energy, info, 0.5f);
        }

        {
            auto& playerObj = Game::GetPlayerObject();

            auto alpha = GetCloakAlpha(playerObj);
            TexID ship = GetGaugeTexID(Gauges::Ship);

            if (Game::Level.IsDescent1())
                DrawShipBitmap({ 0, -46 }, Render::Materials->Get(ship), 2, alpha);
            else
                DrawShipBitmap({ 0, -40 }, Render::Materials->Get(ship), 1, alpha);

            /*{
                Inferno::Render::CanvasBitmapInfo info;
                info.Position = Vector2{ 0, -43 };
                info.Size = Vector2{ 55, 55 };
                info.Texture = Render::Materials->Get(ship).Handles[Material2D::Diffuse];
                info.HorizontalAlign = AlignH::Center;
                info.VerticalAlign = AlignV::Bottom;
                info.Scanline = IMAGE_SCANLINE;
                info.Color = Color{ MONITOR_BRIGHTNESS, MONITOR_BRIGHTNESS, MONITOR_BRIGHTNESS, alpha };
                ApplyAmbient(info.Color, Ambient + Direct * GLARE);
                Render::HudGlowCanvas->DrawBitmapScaled(info);
            }*/

            int frame = std::clamp((int)((100 - player.Shields) / 10), 0, 9);

            if (playerObj.IsInvulnerable()) {
                if (playerObj.Effects.InvulnerableDuration > 0) {
                    auto remaining = playerObj.Effects.InvulnerableDuration - playerObj.Effects.InvulnerableTimer;
                    int invFrame = 10 + (int)(remaining * 5) % 10; // frames 10 to 19, 5 fps

                    // check if near expiring, flicker off/on every 3.5 seconds
                    if (remaining > 4.0f || std::fmod(remaining, 1.0f) < 0.5f)
                        frame = invFrame;
                }
                else {
                    // Infinite invulnerability
                    frame = 10 + (int)(playerObj.Effects.InvulnerableTimer * 5) % 10;
                }
            }

            if (frame != 9) {
                // Frame 9 is 'missing' - no shields
                auto shieldGfx = fmt::format("gauge01b#{}", frame);
                DrawShipBitmap({ 0, -29 }, Render::Materials->Get(shieldGfx), 1, 1);
            }
        }
    }

    struct KillEntry {
        GameTimer Timer;
        string Message;
    };

    template <int TSize>
    class KillTracker {
        std::array<KillEntry, TSize> _messages;

    public:
        void AddKill(string_view message) {
            // Shift all existing entries down by 1
            for (size_t i = _messages.size() - 1; i > 0; i--) {
                _messages[i] = _messages[i - 1];
            }

            _messages[0].Message = message;
            _messages[0].Timer = BASE_SCORE_WINDOW;
        }

        span<KillEntry> GetKills() { return _messages; }
    };

    KillTracker<5> _killTracker;

    class Hud {
        MonitorState _leftMonitor = { true }, _rightMonitor = { false };
        float _scoreTime = 0;
        int _scoreAdded = 0;
        string _messages[4];
        int _messageCount = 0;
        float _messageTimer = 0;
        double _lastLockWarningTime = -1;
        float _lockTextTime = 0;

    public:
        void Reset() {
            _leftMonitor.Reset();
            _rightMonitor.Reset();
            _lastLockWarningTime = -1;
            _lockTextTime = 0;
            _scoreTime = 0;
            ranges::fill(_messages, "");
        }

        void Draw(float dt, Player& player) {
            CheckLockWarning();

            float spacing = 100;
            _leftMonitor.Update(dt, player, (int)player.Primary);
            _rightMonitor.Update(dt, player, (int)player.Secondary);

            DrawLeftMonitor(-spacing, _leftMonitor, player);
            DrawRightMonitor(spacing, _rightMonitor, player);
            DrawCenterMonitor(player);

            DrawReticle();

            if (player.Lives > 1) {
                {
                    // Life text
                    Render::DrawTextInfo info;
                    info.Font = FontSize::Small;
                    info.Color = GREEN_TEXT;
                    info.Position = Vector2(30, 5);
                    info.HorizontalAlign = AlignH::Left;
                    info.VerticalAlign = AlignV::Top;
                    info.Scanline = TEXT_SCANLINE;
                    auto lives = fmt::format("X {}", player.Lives - 1);
                    Render::HudCanvas->DrawText(lives, info);
                }

                {
                    // Life marker
                    Inferno::Render::CanvasBitmapInfo info;
                    info.Position = Vector2(5, 5);
                    auto& material = Render::Materials->Get(GetGaugeTexID(Gauges::Lives));
                    info.Size = Vector2{ (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
                    info.Size *= info.Size.x <= 8.0f ? 2.0f : 1.0f; // Fix for low-res graphics
                    info.Texture = material.Handle();
                    info.HorizontalAlign = AlignH::Left;
                    info.VerticalAlign = AlignV::Top;
                    info.Scanline = TEXT_SCANLINE;
                    info.Color = Color(1.5f, 1.5f, 1.5f);
                    Render::HudCanvas->DrawBitmapScaled(info);
                }
            }

            {
                // Score
                Render::DrawTextInfo info;
                info.Font = FontSize::Small;
                info.Color = GREEN_TEXT;
                info.HorizontalAlign = AlignH::Right;
                info.VerticalAlign = AlignV::Top;
                info.Scanline = TEXT_SCANLINE;
                info.Position = Vector2(-5, 5);
                auto score = fmt::format("score: {:5}", player.Score);
                UseWide1Char(score);
                Render::HudCanvas->DrawText(score, info);

                info.Position.y += 16;

                _scoreTime -= dt;
                if (_scoreTime > 0) {
                    // fade score out
                    auto t = std::clamp((2 - _scoreTime) / 2, 0.0f, 1.0f); // fade the last 2 seconds
                    t = int(t * 10) / 10.0f; // steps of 10 to simulate a limited palette
                    info.Color.w = std::lerp(1.0f, 0.0f, t);
                    score = fmt::format("{:5}", _scoreAdded);
                    UseWide1Char(score);
                    Render::HudCanvas->DrawText(score, info);
                }
                else {
                    _scoreTime = 0;
                    _scoreAdded = 0;
                }

                // Kills
                for (auto& kill : _killTracker.GetKills()) {
                    auto t = Saturate(kill.Timer.Remaining() / 2); // fade the last 2 seconds
                    t = int(t * 10) / 10.0f; // steps of 10 to simulate a limited palette
                    info.Color.w = std::lerp(0.0f, 1.0f, t);
                    //info.Position.y += Atlas.GetFont(FontSize::Small)->Height; // text height
                    info.Position.y += 16; // text height

                    if (info.Color.w > 0)
                        Render::HudCanvas->DrawText(kill.Message, info);
                }
            }

            if (Game::Time - _lastLockWarningTime < _lockTextTime) {
                // Lock text
                Render::DrawTextInfo info;
                info.Font = FontSize::Small;
                info.Color = RED_TEXT;
                ApplyAmbient(info.Color, Ambient);
                info.Position = Vector2(0, 30);
                info.HorizontalAlign = AlignH::Center;
                info.VerticalAlign = AlignV::CenterTop;
                info.Scanline = TEXT_SCANLINE;
                //DrawMonitorText("!LOCK!", info); // Enabling this causes points and lives to flicker for some reason?
                Render::HudCanvas->DrawText("!LOCK!", info);
            }

            if (Game::ControlCenterDestroyed &&
                Game::CountdownSeconds >= 0 &&
                Game::GetState() != GameState::ExitSequence) {
                Render::DrawTextInfo info;
                info.Font = FontSize::Small;
                info.Color = GREEN_TEXT;
                ApplyAmbient(info.Color, Ambient);
                info.Position = Vector2(0, 80);
                info.HorizontalAlign = AlignH::Center;
                info.VerticalAlign = AlignV::Top;
                info.Scanline = TEXT_SCANLINE;
                auto timer = fmt::format("T-{} s", Game::CountdownSeconds);
                Render::HudCanvas->DrawText(timer, info);
            }

            //DrawHighlights(false);
            //DrawHighlights(true);

            // Lock warning
            //DrawAdditiveBitmap({ -220, -230 }, AlignH::CenterRight, "gauge16b", 1);

            //{
            //    auto& material = Render::Materials->Get("gauge16b");

            //    Render::CanvasBitmapInfo info;
            //    info.Position = { -300, 0 } *scale;
            //    info.Size = { (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
            //    info.Size *= scale;
            //    info.Texture = material.Handles[Material2D::Diffuse];
            //    info.HorizontalAlign = AlignH::CenterRight;
            //    info.VerticalAlign = AlignV::CenterTop;
            //    info.Scanline = 1;
            //    Render::HudGlowCanvas->DrawBitmap(info);
            //}

            DrawHudMessages(dt);
        }

        void AddPoints(int points) {
            if (points <= 0) return;
            _scoreAdded += points;
            _scoreTime += BASE_SCORE_WINDOW;
            _scoreTime = std::clamp(_scoreTime, 0.0f, BASE_SCORE_WINDOW * 2);
        }

        void PrintHudMessage(string_view msg) {
            if (_messageCount > 0 && msg == _messages[_messageCount - 1])
                return; // duplicated

            if (_messageCount >= std::size(_messages)) {
                ShiftHudMessages();
            }

            _messages[_messageCount] = msg.data();
            _messageCount++;
            _messageTimer = 3;
        }

    private:
        void CheckLockWarning() {
            if (Game::Player.HomingObjectDist >= 0) {
                auto delay = Game::Player.HomingObjectDist / 128.0f;
                delay = std::clamp(delay, 1 / 8.0f, 1.0f);
                if (Game::Time - _lastLockWarningTime > delay / 2) {
                    Sound::Play2D({ SoundID::HomingWarning }, 0.55f);
                    _lastLockWarningTime = Game::Time;
                    _lockTextTime = delay / 4;
                }
            }
        }

        // Shifts all messages down by one
        void ShiftHudMessages() {
            if (_messageCount <= 0) return;

            for (int i = 0; i < (int)std::size(_messages) - 1; i++) {
                _messages[i] = _messages[i + 1];
                _messages[i + 1] = "";
            }

            _messages[std::size(_messages) - 1] = "";
            _messageCount--;
        }

        void DrawHudMessages(float dt) {
            float offset = 5;

            Render::DrawTextInfo info;
            info.Font = FontSize::Small;
            info.Color = GREEN_TEXT;
            info.HorizontalAlign = AlignH::Center;
            info.VerticalAlign = AlignV::Top;
            info.Scanline = TEXT_SCANLINE;

            for (auto& msg : _messages) {
                info.Position = Vector2(0, offset);
                Render::HudCanvas->DrawText(msg, info);
                offset += 16;
            }

            _messageTimer -= dt;
            if (_messageTimer <= 0) {
                ShiftHudMessages();
                _messageTimer = 3;
            }
        }
    } Hud;

    void PrintHudMessage(string_view msg) {
        Hud.PrintHudMessage(msg);
    }


    void DrawHUD(float dt, const Color& ambient) {
        constexpr Color minLight(0.5f, 0.5f, 0.5f);
        Ambient = ambient;
        Ambient *= Game::GlobalDimming;
        Ambient += minLight;
        Ambient.A(1);
        Direct = Game::Player.DirectLight * 0.5f;
        Direct.A(0);
        Hud.Draw(dt, Game::Player);
    }

    void AddPointsToHUD(int points) {
        Hud.AddPoints(points);
    }

    void AddKillToHUD(string_view name) {
        _killTracker.AddKill(name);
    }

    void ResetHUD() {
        _killTracker = {};
        Hud.Reset();
    }
}
