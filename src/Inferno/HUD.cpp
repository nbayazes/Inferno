#include "pch.h"
#include "HUD.h"
#include "Game.h"
#include "SoundSystem.h"
#include "Graphics/MaterialLibrary.h"
#include "Graphics/Render.h"

namespace Inferno {
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

    class MonitorState {
        FadeState _state{};
        int _requested = -1; // The requested weapon
        int _requestedLaserLevel = -1;
        bool _primary;

    public:
        MonitorState(bool primary) : _primary(primary) {}

        int WeaponIndex = -1; // The visible weapon
        int LaserLevel = -1; // The visible laser level
        float Opacity{}; // Fade out/in based on rearm time / 2

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

    constexpr float WEAPON_TEXT_X_OFFSET = -90;
    constexpr float WEAPON_TEXT_Y_OFFSET = 140;
    constexpr float WEAPON_TEXT_AMMO_Y_OFFSET = WEAPON_TEXT_Y_OFFSET + 25;
    constexpr float WEAPON_BMP_Y_OFFSET = -20;
    constexpr float WEAPON_BMP_X_OFFSET = -135;
    constexpr Color GREEN_TEXT = { 0, 0.7f, 0 };
    constexpr Color RED_TEXT = { 0.7f, 0, 0 };
    constexpr Color GOLD_TEXT = { 0.78f, 0.56f, 0.18f };
    constexpr float MONITOR_BRIGHTNESS = 2;

    void DrawMonitorBitmap(Render::CanvasBitmapInfo& info, float shadow = 0.6f) {
        Render::HudGlowCanvas->DrawBitmap(info);

        info.Scanline = 0.0f;
        info.Color = Color{ 0, 0, 0, shadow };
        Render::HudCanvas->DrawBitmap(info);
    }

    // Draws text with a dark background, easier to read
    void DrawMonitorText(string_view text, Render::DrawTextInfo& info, float shadow = 0.6f) {
        Render::HudGlowCanvas->DrawGameText(text, info);
        info.Color = Color{ 0, 0, 0, shadow };
        info.Scanline = 0.0f;
        Render::HudCanvas->DrawGameText(text, info);
    }

    void DrawReticleBitmap(const Vector2& offset, Gauges gauge, int frame, float scale) {
        TexID id = GetGaugeTexID(Gauges((int)gauge + frame));
        scale *= Render::HudCanvas->GetScale();
        auto& material = Render::Materials->Get(id);

        Inferno::Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = Vector2{ (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        info.Size *= scale;
        info.Texture = material.Handles[Material2D::Diffuse];
        info.HorizontalAlign = AlignH::Center;
        info.VerticalAlign = AlignV::CenterTop;
        info.Scanline = 0.0f;
        Render::HudCanvas->DrawBitmap(info);

        info.Scanline = 0.4f;
        Render::HudCanvas->DrawBitmap(info);
    }

    void DrawOpaqueBitmap(const Vector2& offset, AlignH align, const Material2D& material, const Color& color) {
        auto scale = Render::HudCanvas->GetScale();
        Inferno::Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = Vector2{ (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        info.Size *= scale;
        info.Texture = material.Handle();
        info.HorizontalAlign = align;
        info.VerticalAlign = AlignV::Bottom;
        info.Color = color;
        Render::HudCanvas->DrawBitmap(info);
    }

    void DrawOpaqueBitmap(const Vector2& offset, AlignH align, const string& bitmapName, const Color& color) {
        auto& material = Render::Materials->Get(bitmapName);
        DrawOpaqueBitmap(offset, align, material, color);
    }

    void DrawAdditiveBitmap(const Vector2& offset, AlignH align, const Material2D& material, float sizeScale, float scanline, bool mirrorX = false) {
        float scale = Render::HudCanvas->GetScale();
        Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = Vector2{ (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        info.Size *= scale * sizeScale;
        info.Texture = material.Handles[Material2D::Diffuse];
        info.HorizontalAlign = align;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = scanline;
        info.MirrorX = mirrorX;
        Render::HudGlowCanvas->DrawBitmap(info);
    }

    void DrawAdditiveBitmap(const Vector2& offset, AlignH align, Gauges gauge, float sizeScale, float scanline = 0.4f, bool mirrorX = false) {
        TexID id = GetGaugeTexID(gauge);
        auto& material = Render::Materials->Get(id);
        DrawAdditiveBitmap(offset, align, material, sizeScale, scanline, mirrorX);
    }

    //void DrawAdditiveBitmap(const Vector2& offset, AlignH align, const string& bitmapName, float sizeScale, float scanline = 0.4f) {
    //    auto& material = Render::Materials->Get(bitmapName);
    //    DrawAdditiveBitmap(offset, align, material, sizeScale, scanline);
    //}

    void DrawWeaponBitmap(const Vector2& offset, AlignH align, TexID id, float sizeScale, float alpha) {
        Render::LoadTextureDynamic(id);
        auto& ti = Resources::GetTextureInfo(id);
        float scale = Render::HudCanvas->GetScale();

        Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = Vector2(ti.Width, ti.Height) * scale * sizeScale;
        info.Texture = Render::Materials->Get(id).Handles[Material2D::Diffuse];
        info.HorizontalAlign = align;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = 0.4f;
        info.Color *= MONITOR_BRIGHTNESS;
        info.Color.w = alpha;
        DrawMonitorBitmap(info, 0.6f * alpha);
    }

    void DrawReticle() {
        auto isD1 = Game::Level.IsDescent1();
        const Vector2 crossOffset(0, isD1 ? -2.0f : -5.0f);
        const Vector2 primaryOffset(0, isD1 ? 6.0f : 14.0f);
        const Vector2 secondaryOffset(0, isD1 ? 1.0f : 2.0f);

        bool primaryReady = Game::Player.CanFirePrimary(Game::Player.Primary) && Game::Player.PrimaryDelay <= 0;
        bool secondaryReady = Game::Player.CanFireSecondary(Game::Player.Secondary) && Game::Player.SecondaryDelay <= 0;
        float scale = isD1 ? 2.0f : 1.0f;
        // cross deactivates when no primary or secondary weapons are available
        int crossFrame = primaryReady || secondaryReady ? 1 : 0;

        bool quadLasers = Game::Player.HasPowerup(PowerupFlag::QuadLasers) && Game::Player.Primary == PrimaryWeaponIndex::Laser;
        int primaryFrame = primaryReady ? (quadLasers ? 2 : 1) : 0;
        DrawReticleBitmap(crossOffset, Gauges::ReticleCross, crossFrame, scale); // gauss, vulkan
        DrawReticleBitmap(primaryOffset, Gauges::ReticlePrimary, primaryFrame, scale);

        int secondaryFrame = secondaryReady;
        static constexpr uint8_t Secondary_weapon_to_gun_num[10] = { 4, 4, 7, 7, 7, 4, 4, 7, 4, 7 };

        if (Secondary_weapon_to_gun_num[(int)Game::Player.Secondary] == 7)
            secondaryFrame += 3; //now value is 0,1 or 3,4
        else if (secondaryFrame && !(Game::Player.MissileFiringIndex & 1))
            secondaryFrame++;

        DrawReticleBitmap(secondaryOffset, Gauges::ReticleSecondary, secondaryFrame, scale);
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
        auto hex = Color(1 * MONITOR_BRIGHTNESS, 1 * MONITOR_BRIGHTNESS, 1 * MONITOR_BRIGHTNESS).RGBA().v;

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

        Render::CanvasPayload payload{};
        payload.V0 = { v0, uv0, hex }; // bottom left
        payload.V1 = { v1, uv1, hex }; // bottom right
        payload.V2 = { v2, uv2, hex }; // top right
        payload.V3 = { v3, uv3, hex }; // top left
        payload.Texture = material.Handle();
        payload.Scanline = 1.0f;

        Render::HudGlowCanvas->Draw(payload);
        Render::HudGlowCanvas->Draw(payload);
    }

    constexpr float DEFAULT_SCANLINE = 0.75f;

    void DrawAfterburnerBar(float x, const Player& player) {
        if (!player.HasPowerup(PowerupFlag::Afterburner))
            return;

        float percent = player.AfterburnerCharge;
        auto scale = Render::HudCanvas->GetScale();
        auto hex = Color(1, 1, 1).RGBA().v;
        auto pos = Vector2{ x - 151, -37 } * scale;
        auto& material = Render::Materials->Get("gauge02b");
        Vector2 size = {
            (float)material.Textures[0].GetWidth() * scale,
            (float)material.Textures[0].GetHeight() * percent * scale
        };

        auto alignment = Render::GetAlignment(size, AlignH::CenterLeft, AlignV::Bottom, Render::HudGlowCanvas->GetSize());
        float uvTop = 1 - percent;

        Render::CanvasPayload info{};
        info.V0 = { Vector2{ pos.x, pos.y + size.y } + alignment, { 0, 1 }, hex }; // bottom left
        info.V1 = { Vector2{ pos.x + size.x, pos.y + size.y } + alignment, { 1, 1 }, hex }; // bottom right
        info.V2 = { Vector2{ pos.x + size.x, pos.y } + alignment, { 1, uvTop }, hex }; // top right
        info.V3 = { Vector2{ pos.x, pos.y } + alignment, { 0, uvTop }, hex }; // top left
        info.Texture = material.Handle();
        info.Scanline = DEFAULT_SCANLINE;
        Render::HudGlowCanvas->Draw(info);
    }

    // convert '1' characters to a special fixed width version. Only works with small font.
    void UseWide1Char(string& s) {
        for (auto& c : s)
            if (c == '1') c = static_cast<char>(132);
    }

    void DrawLeftMonitor(float x, const MonitorState& state, const Player& player, const Color& ambient) {
        DrawOpaqueBitmap({ x, 0 }, AlignH::CenterLeft, "cockpit-left", ambient);

        auto scale = Render::HudCanvas->GetScale();
        auto weaponIndex = (PrimaryWeaponIndex)state.WeaponIndex;

        Render::DrawTextInfo info;
        info.Font = FontSize::Small;
        info.Color = GREEN_TEXT;
        info.Color.w = state.Opacity;
        info.Position = Vector2(x + WEAPON_TEXT_X_OFFSET, WEAPON_TEXT_Y_OFFSET) * scale;
        info.HorizontalAlign = AlignH::CenterRight; // Justify the left edge of the text to the center
        info.VerticalAlign = AlignV::CenterTop;
        info.Scanline = 0.5f;
        auto weaponName = Resources::GetPrimaryNameShort(weaponIndex);
        string label = string(weaponName), ammo;

        switch (weaponIndex) {
            case PrimaryWeaponIndex::Laser:
            {
                if (player.HasPowerup(PowerupFlag::QuadLasers))
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
                ammo = fmt::format("{:05}", player.PrimaryAmmo[1]);
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
            info.Position = Vector2(x + WEAPON_TEXT_X_OFFSET + 5, WEAPON_TEXT_AMMO_Y_OFFSET) * scale;
            info.HorizontalAlign = AlignH::CenterRight;
            info.VerticalAlign = AlignV::CenterTop;
            info.Scanline = 0.5f;
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

    void DrawRightMonitor(float x, const MonitorState& state, const Player& player, const Color& ambient) {
        DrawOpaqueBitmap({ x, 0 }, AlignH::CenterRight, "cockpit-right", ambient);

        auto scale = Render::HudCanvas->GetScale();
        Render::DrawTextInfo info;
        info.Font = FontSize::Small;
        info.Color = GREEN_TEXT;
        info.Color.w = state.Opacity;
        info.Position = Vector2(x + 25, WEAPON_TEXT_Y_OFFSET) * scale;
        info.HorizontalAlign = AlignH::CenterRight; // Justify the left edge of the text to the center
        info.VerticalAlign = AlignV::CenterTop;
        info.Scanline = 0.5f;
        DrawMonitorText(Resources::GetSecondaryNameShort((SecondaryWeaponIndex)state.WeaponIndex), info, 0.6f * state.Opacity);

        // Ammo counter
        info.Color = RED_TEXT;
        info.Color.w = state.Opacity;
        info.Position = Vector2(x + 35, WEAPON_TEXT_AMMO_Y_OFFSET) * scale;
        info.HorizontalAlign = AlignH::CenterRight;
        info.VerticalAlign = AlignV::CenterTop;
        info.Scanline = 0.5f;
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
            info.Color = bomb == SecondaryWeaponIndex::Proximity ? RED_TEXT : GOLD_TEXT;
            info.Position = Vector2(x + 157, -26) * scale;
            info.HorizontalAlign = AlignH::CenterRight;
            info.VerticalAlign = AlignV::Bottom;
            info.Scanline = 0.5f;

            auto bombs = player.SecondaryAmmo[(int)bomb];
            if (bombs > 0)
                DrawMonitorText(fmt::format("B:{:02}", bombs), info);
        }

        // Draw Keys
        bool mirrorX = Game::Level.IsDescent1();
        float keyScanline = 0.0f;
        if (player.HasPowerup(PowerupFlag::BlueKey))
            DrawAdditiveBitmap({ x + 147, -90 }, AlignH::CenterRight, Gauges::BlueKey, resScale, keyScanline, mirrorX);

        if (player.HasPowerup(PowerupFlag::GoldKey))
            DrawAdditiveBitmap({ x + 147 + 2, -90 + 21 }, AlignH::CenterRight, Gauges::GoldKey, resScale, keyScanline, mirrorX);

        if (player.HasPowerup(PowerupFlag::RedKey))
            DrawAdditiveBitmap({ x + 147 + 4, -90 + 42 }, AlignH::CenterRight, Gauges::RedKey, resScale, keyScanline, mirrorX);
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
            Render::CanvasPayload payload;
            payload.Texture = material.Handle();

            float x0 = -cos((steps - i) * 3.14f / steps / 2 + 0.2f) * width * scale * 0.7f + offset;
            float x1 = -cos((steps - i - 1) * 3.14f / steps / 2 + 0.2f) * width * scale * 0.7f + offset;
            float y0 = yOffset + yStep * float(i);
            float y1 = yOffset + yStep * float(i + 1);

            Vector2 v0 = { x0, y0 };
            Vector2 v1 = { x0 + width * 2, y0 };
            Vector2 v2 = { x1 + width * 2, y1 };
            Vector2 v3 = { x1, y1 };

            payload.V0 = CanvasVertex{ v0, { 1 - vStep * float(i), 0 }, color.RGBA().v }; // bottom left
            payload.V1 = CanvasVertex{ v1, { 1 - vStep * float(i), 1 }, color.RGBA().v }; // bottom right
            payload.V2 = CanvasVertex{ v2, { 1 - vStep * float(i + 1), 1 }, color.RGBA().v }; // top right
            payload.V3 = CanvasVertex{ v3, { 1 - vStep * float(i + 1), 0 }, color.RGBA().v }; // top left
            Render::HudGlowCanvas->Draw(payload);
        }
    }

    string HudMessages[4]{};
    int HudMessageCount = 0;
    float HudTimer = 0;

    // Shifts all messages down by one
    void ShiftHudMessages() {
        if (HudMessageCount <= 0) return;

        for (int i = 0; i < (int)std::size(HudMessages) - 1; i++) {
            HudMessages[i] = std::move(HudMessages[i + 1]);
            //HudMessages[i] = HudMessages[i + 1];
        }

        HudMessages[std::size(HudMessages) - 1] = "";
        HudMessageCount--;
    }

    void PrintHudMessage(string_view msg) {
        if (HudMessageCount > 0 && msg == HudMessages[HudMessageCount - 1])
            return; // duplicated

        if (HudMessageCount >= std::size(HudMessages)) {
            ShiftHudMessages();
        }

        HudMessages[HudMessageCount] = msg.data();
        HudMessageCount++;
        HudTimer = 3;
    }

    void DrawHudMessages(float dt) {
        auto scale = Render::HudCanvas->GetScale();
        float offset = 5;

        Render::DrawTextInfo info;
        info.Font = FontSize::Small;
        info.Color = GREEN_TEXT;
        info.HorizontalAlign = AlignH::Center;
        info.VerticalAlign = AlignV::Top;
        info.Scanline = 0.5f;

        for (auto& msg : HudMessages) {
            info.Position = Vector2(0, offset) * scale;
            Render::HudCanvas->DrawGameText(msg, info);
            offset += 16;
        }

        HudTimer -= dt;
        if (HudTimer <= 0) {
            ShiftHudMessages();
            HudTimer = 3;
        }
    }

    float GetCloakAlpha(const Player& player) {
        if (!player.HasPowerup(PowerupFlag::Cloak)) return 1;

        auto remaining = player.CloakTime + CLOAK_TIME - (float)Game::Time;

        if (remaining > CLOAK_TIME - 0.5f) {
            // Cloak just picked up, fade out
            return 1 - ((float)Game::Time - player.CloakTime) / 0.5f;
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
        auto scale = Render::HudCanvas->GetScale();

        Inferno::Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = Vector2{ (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        info.Size *= scale * sizeScale;
        info.Texture = material.Handles[Material2D::Diffuse];
        info.HorizontalAlign = AlignH::Center;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = 0.75f;
        info.Color = Color(1, 1, 1, alpha);
        Render::HudGlowCanvas->DrawBitmap(info);

        //DrawMonitorBitmap(info, 0.90f);
    }

    void DrawCenterMonitor(const Player& player, const Color& ambient) {
        DrawOpaqueBitmap({ 0, 0 }, AlignH::Center, "cockpit-ctr", ambient);
        // Draw shields, invuln state, shield / energy count

        {
            auto scale = Render::HudCanvas->GetScale();
            Render::DrawTextInfo info;
            info.Font = FontSize::Small;
            info.Color = Color{ 0.54f, 0.54f, 0.71f };

            info.Position = Vector2(2, -120) * scale;
            info.HorizontalAlign = AlignH::Center;
            info.VerticalAlign = AlignV::Bottom;
            info.Scanline = 0.5f;
            auto shields = fmt::format("{:.0f}", player.Shields < 0 ? 0 : std::floor(player.Shields));
            DrawMonitorText(shields, info, 0.5f);
            //info.Scanline = 0.0f;
            //info.Color *= 0.1;
            //info.Color.z = 0.8f;

            info.Color = GOLD_TEXT;
            info.Position = Vector2(2, -150) * scale;
            info.Scanline = 0.5f;
            auto energy = fmt::format("{:.0f}", player.Energy < 0 ? 0 : std::floor(player.Energy));
            DrawMonitorText(energy, info, 0.5f);
        }

        {
            auto alpha = GetCloakAlpha(player);
            TexID ship = GetGaugeTexID(Gauges::Ship);
            if (Game::Level.IsDescent1())
                DrawShipBitmap({ 0, -46 }, Render::Materials->Get(ship), 2, alpha);
            else
                DrawShipBitmap({ 0, -40 }, Render::Materials->Get(ship), 1, alpha);

            int frame = std::clamp((int)((100 - player.Shields) / 10), 0, 9);

            if (player.HasPowerup(PowerupFlag::Invulnerable)) {
                auto remaining = player.InvulnerableTime + INVULN_TIME - (float)Game::Time;
                int invFrame = 10 + (int)(remaining * 5) % 10; // frames 10 to 19, 5 fps

                if (remaining > 4.0f || std::fmod(remaining, 1.0f) < 0.5f) {
                    // check if near expiring, flicker off/on every 3.5 seconds
                    frame = invFrame;
                }
            }

            if (frame != 9) {
                // Frame 9 is 'missing' - no shields
                auto shieldGfx = fmt::format("gauge01b#{}", frame);
                DrawShipBitmap({ 0, -29 }, Render::Materials->Get(shieldGfx), 1, 1);
            }
        }
    }

    constexpr float BASE_SCORE_WINDOW = 3.0f;

    class Hud {
        MonitorState _leftMonitor = { true }, _rightMonitor = { false };
        float _scoreTime = 0;
        int _scoreAdded = 0;

    public:
        void Draw(float dt, Player& player, const Color& ambient) {
            CheckLockWarning();

            float spacing = 100;
            _leftMonitor.Update(dt, player, (int)player.Primary);
            _rightMonitor.Update(dt, player, (int)player.Secondary);

            DrawLeftMonitor(-spacing, _leftMonitor, player, ambient);
            DrawRightMonitor(spacing, _rightMonitor, player, ambient);
            DrawCenterMonitor(player, ambient);

            DrawReticle();

            auto scale = Render::HudCanvas->GetScale();

            if (player.Lives > 0) {
                // Life text
                Render::DrawTextInfo info;
                info.Font = FontSize::Small;
                info.Color = GREEN_TEXT;
                info.Position = Vector2(30, 5) * scale;
                info.HorizontalAlign = AlignH::Left;
                info.VerticalAlign = AlignV::Top;
                info.Scanline = 0.5f;
                auto lives = fmt::format("X {}", player.Lives);
                Render::HudCanvas->DrawGameText(lives, info);
            }

            {
                // Life marker
                Inferno::Render::CanvasBitmapInfo info;
                info.Position = Vector2(5, 5) * scale;
                auto& material = Render::Materials->Get(GetGaugeTexID(Gauges::Lives));
                info.Size = Vector2{ (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
                info.Size *= info.Size.x <= 8 ? scale * 2 : scale; // Fix for low-res graphics
                info.Texture = material.Handle();
                info.HorizontalAlign = AlignH::Left;
                info.VerticalAlign = AlignV::Top;
                info.Scanline = 0.5f;
                Render::HudCanvas->DrawBitmap(info);
            }

            {
                // Score
                Render::DrawTextInfo info;
                info.Font = FontSize::Small;
                info.Color = GREEN_TEXT;
                info.HorizontalAlign = AlignH::Right;
                info.VerticalAlign = AlignV::Top;
                info.Scanline = 0.5f;
                info.Position = Vector2(-5, 5) * scale;
                auto score = fmt::format("score: {:5}", player.Score);
                UseWide1Char(score);
                Render::HudCanvas->DrawGameText(score, info);

                _scoreTime -= dt;
                if (_scoreTime > 0) {
                    // fade score out
                    info.Position = Vector2(-5, 20) * scale;
                    auto t = std::clamp((2 - _scoreTime) / 2, 0.0f, 1.0f); // fade the last 2 seconds
                    t = int(t * 10) / 10.0f; // steps of 10 to simulate a limited palette
                    info.Color.w = std::lerp(1.0f, 0.0f, t);
                    score = fmt::format("{:5}", _scoreAdded);
                    UseWide1Char(score);
                    Render::HudCanvas->DrawGameText(score, info);
                }
                else {
                    _scoreTime = 0;
                    _scoreAdded = 0;
                }
            }

            if (Game::Time - _lastLockWarningTime < _lockTextTime) {
                // Lock text
                Render::DrawTextInfo info;
                info.Font = FontSize::Small;
                info.Color = RED_TEXT;
                info.Position = Vector2(0, 30) * scale;
                info.HorizontalAlign = AlignH::Center;
                info.VerticalAlign = AlignV::CenterTop;
                info.Scanline = 0.8f;
                DrawMonitorText("!LOCK!", info);
            }

            if (Game::ControlCenterDestroyed && Game::CountdownSeconds >= 0) {
                Render::DrawTextInfo info;
                info.Font = FontSize::Small;
                info.Color = GREEN_TEXT;
                info.Position = Vector2(0, 80) * scale;
                info.HorizontalAlign = AlignH::Center;
                info.VerticalAlign = AlignV::Top;
                info.Scanline = 0.8f;
                auto timer = fmt::format("T-{} s", Game::CountdownSeconds);
                Render::HudCanvas->DrawGameText(timer, info);
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

    private:
        double _lastLockWarningTime = -1;
        float _lockTextTime = 0;

        void CheckLockWarning() {
            if (Game::Player.HomingObjectDist >= 0) {
                auto delay = Game::Player.HomingObjectDist / 128.0f;
                delay = std::clamp(delay, 1 / 8.0f, 1.0f);
                if (Game::Time - _lastLockWarningTime > delay / 2) {
                    auto resource = Resources::GetSoundResource(SoundID::HomingWarning);
                    Sound::Play(resource);
                    _lastLockWarningTime = Game::Time;
                    _lockTextTime = delay / 4;
                }
            }
        }
    } Hud;

    void DrawHUD(float dt, Color ambient) {
        constexpr Color minLight(0.5f, 0.5f, 0.5f, 1);
        ambient = Vector4::Max(minLight, ambient);
        Hud.Draw(dt, Game::Player, ambient);
    }

    void AddPointsToHUD(int points) {
        Hud.AddPoints(points);
    }
}
