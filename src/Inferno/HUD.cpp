#include "pch.h"
#include "HUD.h"
#include "Game.h"

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
        Lives = 37,
        Ship = 38, // 8 Colors
        ReticleCross = 46, // 2 frames: not ready, ready
        ReticlePrimary = 48, // 3 frames: not ready, center ready, quad ready
        ReticleSecondary = 51, // 5 frames: not ready, left ready, right ready, center not ready, center ready
        HomingWarningOn = 56,
        HomingWarningOff = 57,
    };

    class MonitorState {
        enum FadeState { FadeNone, FadeIn, FadeOut };
        FadeState State{};
        int Requested = -1; // The requested weapon

    public:
        int WeaponIndex = -1; // The visible weapon
        float Opacity{}; // Fade out/in based on rearm time / 2

        void Update(float dt, Player& player, int weapon) {
            if (Requested != weapon) {
                State = FadeOut;
                //Opacity = player.RearmTime;
                Requested = weapon;
            }

            if (WeaponIndex == -1) {
                // initial load, draw current weapon
                WeaponIndex = Requested = weapon;
                Opacity = 1;
                State = FadeNone;
            }

            if (State == FadeOut) {
                Opacity -= dt * player.RearmTime * 2;
                if (Opacity <= 0) {
                    Opacity = 0;
                    State = FadeIn;
                    WeaponIndex = Requested; // start showing the requested weapon
                }
            }
            else if (State == FadeIn) {
                if (Requested != weapon) {
                    State = FadeOut; // weapon was changed while swapping
                }
                else {
                    Opacity += dt * player.RearmTime * 2;
                    if (Opacity >= 1) {
                        State = FadeNone;
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
    constexpr Color MonitorGreenText = { 0, 0.7f, 0 };
    constexpr Color MonitorRedText = { 0.7f, 0, 0 };

    void DrawMonitorBitmap(Render::CanvasBitmapInfo& info, float shadow = 0.6f) {
        Render::HudGlowCanvas->DrawBitmap(info);

        info.Scanline = 0.0f;
        info.Color = { 0, 0, 0, shadow };
        Render::HudCanvas->DrawBitmap(info);
    }

    // Draws text with a dark background, easier to read
    void DrawMonitorText(string_view text, Render::DrawTextInfo& info, float shadow = 0.6f) {
        Render::HudGlowCanvas->DrawGameText(text, info);
        info.Color = { 0, 0, 0, shadow };
        info.Scanline = 0.0f;
        Render::HudCanvas->DrawGameText(text, info);
    }

    void DrawReticleBitmap(const Vector2& offset, Gauges gauge, int frame, float scale) {
        TexID id = GetGaugeTexID(Gauges((int)gauge + frame));
        scale *= Render::HudCanvas->GetScale();
        auto& material = Render::Materials->Get(id);

        Inferno::Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = { (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        info.Size *= scale;
        info.Texture = material.Handles[Material2D::Diffuse];
        info.HorizontalAlign = AlignH::Center;
        info.VerticalAlign = AlignV::CenterTop;
        info.Scanline = 0.0f;
        Render::HudCanvas->DrawBitmap(info);

        info.Scanline = 0.4f;
        Render::HudCanvas->DrawBitmap(info);
    }

    void DrawShipBitmap(const Vector2& offset, const Material2D& material, float sizeScale) {
        auto scale = Render::HudCanvas->GetScale();

        Inferno::Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = { (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        info.Size *= scale * sizeScale;
        info.Texture = material.Handles[Material2D::Diffuse];
        info.HorizontalAlign = AlignH::Center;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = 0.5f;
        DrawMonitorBitmap(info, 0.90f);
    }

    void DrawOpaqueBitmap(const Vector2& offset, AlignH align, const Material2D& material) {
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

    void DrawOpaqueBitmap(const Vector2& offset, AlignH align, string bitmapName) {
        auto& material = Render::Materials->GetOutrageMaterial(bitmapName);
        DrawOpaqueBitmap(offset, align, material);
    }

    void DrawAdditiveBitmap(const Vector2& offset, AlignH align, const Material2D& material, float sizeScale, float scanline) {
        float scale = Render::HudCanvas->GetScale();
        Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = { (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        info.Size *= scale * sizeScale;
        info.Texture = material.Handles[Material2D::Diffuse];
        info.HorizontalAlign = align;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = scanline;
        Render::HudGlowCanvas->DrawBitmap(info);
    }

    void DrawAdditiveBitmap(const Vector2& offset, AlignH align, Gauges gauge, float sizeScale, float scanline = 0.4f) {
        TexID id = GetGaugeTexID(gauge);
        auto& material = Render::Materials->Get(id);
        DrawAdditiveBitmap(offset, align, material, sizeScale, scanline);
    }

    void DrawAdditiveBitmap(const Vector2& offset, AlignH align, string bitmapName, float sizeScale, float scanline = 0.4f) {
        auto& material = Render::Materials->GetOutrageMaterial(bitmapName);
        DrawAdditiveBitmap(offset, align, material, sizeScale, scanline);
    }

    void DrawWeaponBitmap(const Vector2& offset, AlignH align, TexID id, float sizeScale, float alpha) {
        Render::LoadTextureDynamic(id);
        auto& ti = Resources::GetTextureInfo(id);
        float scale = Render::HudCanvas->GetScale();

        Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = Vector2((float)ti.Width, (float)ti.Height) * scale * sizeScale;
        info.Texture = Render::Materials->Get(id).Handles[Material2D::Diffuse];
        info.HorizontalAlign = align;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = 0.4f;
        info.Color.w = alpha;
        DrawMonitorBitmap(info, 0.6f * alpha);
    }

    void DrawReticle() {
        const Vector2 crossOffset(0/*-8*/, -5);
        const Vector2 primaryOffset(0/*-30*/, 14);
        const Vector2 secondaryOffset(0/*-24*/, 2);

        bool primaryReady = Game::Player.CanFirePrimary();
        bool secondaryReady = Game::Player.CanFireSecondary();
        float scale = Game::Level.IsDescent1() ? 2.0f : 1.0f;
        // cross deactivates when no primary or secondary weapons are available
        int crossFrame = primaryReady || secondaryReady ? 1 : 0;

        bool quadLasers = Game::Player.HasPowerup(PowerupFlag::QuadLasers) && Game::Player.Primary == PrimaryWeaponIndex::Laser;
        int primaryFrame = primaryReady ? (quadLasers ? 2 : 1) : 0;
        DrawReticleBitmap(crossOffset, Gauges::ReticleCross, crossFrame, scale); // gauss, vulkan
        DrawReticleBitmap(primaryOffset, Gauges::ReticlePrimary, primaryFrame, scale);

        int secondaryFrame = secondaryReady;
        static constexpr uint8_t Secondary_weapon_to_gun_num[10] = { 4,4,7,7,7,4,4,7,4,7 };

        if (Secondary_weapon_to_gun_num[(int)Game::Player.Secondary] == 7)
            secondaryFrame += 3;		//now value is 0,1 or 3,4
        else if (secondaryFrame && (Game::Player.MissileGunpoint & 1))
            secondaryFrame++;

        DrawReticleBitmap(secondaryOffset, Gauges::ReticleSecondary, secondaryFrame, scale);

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

    // convert '1' characters to special wide ones (fixed width?)
    void UseWide1Char(string& s) {
        for (auto& c : s)
            if (c == '1') c = 132;
    }

    void DrawLeftMonitor(float x, const MonitorState& state) {
        DrawOpaqueBitmap({ x, 0 }, AlignH::CenterLeft, "cockpit-left");

        auto scale = Render::HudCanvas->GetScale();

        {
            Render::DrawTextInfo info;
            info.Font = FontSize::Small;
            info.Color = MonitorGreenText;
            info.Color.w = state.Opacity;
            info.Position = Vector2(x + WEAPON_TEXT_X_OFFSET, WEAPON_TEXT_Y_OFFSET) * scale;
            info.HorizontalAlign = AlignH::CenterRight; // Justify the left edge of the text to the center
            info.VerticalAlign = AlignV::CenterTop;
            info.Scanline = 0.5f;
            auto weaponName = Resources::GetPrimaryNameShort((PrimaryWeaponIndex)state.WeaponIndex);
            string label = string(weaponName), ammo;

            switch ((PrimaryWeaponIndex)state.WeaponIndex) {
                case PrimaryWeaponIndex::Laser:
                case PrimaryWeaponIndex::SuperLaser:
                {
                    auto lvl = Resources::GetString(StringTableEntry::Lvl);
                    if (Game::Player.HasPowerup(PowerupFlag::QuadLasers))
                        label = fmt::format("{}\n{}: {}\n{}", weaponName, lvl, Game::Player.LaserLevel + 1, Resources::GetString(StringTableEntry::Quad));
                    else
                        label = fmt::format("{}\n{}: {}", weaponName, lvl, Game::Player.LaserLevel + 1);
                    break;
                }

                case PrimaryWeaponIndex::Vulcan:
                case PrimaryWeaponIndex::Gauss:
                    ammo = fmt::format("{:05}", Game::Player.PrimaryAmmo[1]);
                    break;
            }

            DrawMonitorText(label, info, 0.6f * state.Opacity);

            if (!ammo.empty()) {
                // Ammo counter
                info.Color = MonitorRedText;
                info.Color.w = state.Opacity;
                info.Position = Vector2(x + WEAPON_TEXT_X_OFFSET + 5, WEAPON_TEXT_AMMO_Y_OFFSET) * scale;
                info.HorizontalAlign = AlignH::CenterRight;
                info.VerticalAlign = AlignV::CenterTop;
                info.Scanline = 0.5f;
                UseWide1Char(ammo);
                DrawMonitorText(ammo, info, 0.6f * state.Opacity);
            }

            // todo: omega charge
        }

        {
            float resScale = Game::Level.IsDescent1() ? 2.0f : 1.0f; // todo: check resource path instead?
            auto texId = GetWeaponTexID(Resources::GetWeapon(PrimaryToWeaponID[state.WeaponIndex]));
            DrawWeaponBitmap({ x + WEAPON_BMP_X_OFFSET, WEAPON_BMP_Y_OFFSET }, AlignH::CenterRight, texId, resScale, state.Opacity);
        }

        DrawEnergyBar(x, false);

        DrawAdditiveBitmap({ x - 151, -38 }, AlignH::CenterLeft, "gauge02b", 1);
    }

    void DrawRightMonitor(float x, const MonitorState& state) {
        DrawOpaqueBitmap({ x, 0 }, AlignH::CenterRight, "cockpit-right");

        auto scale = Render::HudCanvas->GetScale();
        Render::DrawTextInfo info;
        info.Font = FontSize::Small;
        info.Color = MonitorGreenText;
        info.Color.w = state.Opacity;
        info.Position = Vector2(x + 25, WEAPON_TEXT_Y_OFFSET) * scale;
        info.HorizontalAlign = AlignH::CenterRight; // Justify the left edge of the text to the center
        info.VerticalAlign = AlignV::CenterTop;
        info.Scanline = 0.5f;
        DrawMonitorText(Resources::GetSecondaryNameShort((SecondaryWeaponIndex)state.WeaponIndex), info, 0.6f * state.Opacity);

        // Ammo counter
        info.Color = MonitorRedText;
        info.Color.w = state.Opacity;
        info.Position = Vector2(x + 35, WEAPON_TEXT_AMMO_Y_OFFSET) * scale;
        info.HorizontalAlign = AlignH::CenterRight;
        info.VerticalAlign = AlignV::CenterTop;
        info.Scanline = 0.5f;
        auto ammo = fmt::format("{:03}", Game::Player.SecondaryAmmo[state.WeaponIndex]);
        UseWide1Char(ammo);
        DrawMonitorText(ammo, info, 0.6f * state.Opacity);

        float resScale = Game::Level.IsDescent1() ? 2.0f : 1.0f;
        {
            auto texId = GetWeaponTexID(Resources::GetWeapon(SecondaryToWeaponID[state.WeaponIndex]));
            DrawWeaponBitmap({ x + 75, WEAPON_BMP_Y_OFFSET }, AlignH::CenterRight, texId, resScale, state.Opacity);
        }

        DrawEnergyBar(x, true);


        // Bomb counter
        info.Color = MonitorRedText;
        info.Position = Vector2(x + 157, -26) * scale;
        info.HorizontalAlign = AlignH::CenterRight;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = 0.5f;
        DrawMonitorText("B:04", info);


        // Draw Keys
        float keyScanline = 0.0f;
        DrawAdditiveBitmap({ x + 147, -90 }, AlignH::CenterRight, Gauges::BlueKey, resScale, keyScanline);
        DrawAdditiveBitmap({ x + 147 + 2, -90 + 21 }, AlignH::CenterRight, Gauges::GoldKey, keyScanline);
        DrawAdditiveBitmap({ x + 147 + 4, -90 + 42 }, AlignH::CenterRight, Gauges::RedKey, keyScanline);
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

        {
            TexID ship = GetGaugeTexID(Gauges::Ship);
            if (Game::Level.IsDescent1())
                DrawShipBitmap({ 0, -46 }, Render::Materials->Get(ship), 2);
            else
                DrawShipBitmap({ 0, -40 }, Render::Materials->Get(ship), 1);

            DrawShipBitmap({ 0, -29 }, Render::Materials->GetOutrageMaterial("gauge01b#0"), 1);
        }
    }

    void DrawHighlights(bool flip, float opacity = 0.07f) {
        auto& material = Render::Materials->GetOutrageMaterial("SmHilite");
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
            payload.Texture = material.Handles[0];

            float x0 = -cos((steps - i) * 3.14f / steps / 2 + 0.2f) * width * scale * 0.7f + offset;
            float x1 = -cos((steps - i - 1) * 3.14f / steps / 2 + 0.2f) * width * scale * 0.7f + offset;
            float y0 = yOffset + yStep * i;
            float y1 = yOffset + yStep * (i + 1);

            Vector2 v0 = { x0, y0 };
            Vector2 v1 = { x0 + (width * 2), y0 };
            Vector2 v2 = { x1 + (width * 2), y1 };
            Vector2 v3 = { x1, y1 };

            payload.V0 = CanvasVertex{ v0, { 1 - vStep * i      , 0 }, color.RGBA().v }; // bottom left
            payload.V1 = CanvasVertex{ v1, { 1 - vStep * i      , 1 }, color.RGBA().v }; // bottom right
            payload.V2 = CanvasVertex{ v2, { 1 - vStep * (i + 1), 1 }, color.RGBA().v }; // top right
            payload.V3 = CanvasVertex{ v3, { 1 - vStep * (i + 1), 0 }, color.RGBA().v }; // top left
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
        if (msg == HudMessages[0]) return; // duplicated

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
        info.Color = MonitorGreenText;
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

    class Hud {
        MonitorState LeftMonitor, RightMonitor;

        // cloak fade
    public:
        void Draw(float dt, Player& player) {
            float spacing = 100;
            LeftMonitor.Update(dt, player, (int)player.Primary);
            RightMonitor.Update(dt, player, (int)player.Secondary);

            DrawLeftMonitor(-spacing, LeftMonitor);
            DrawRightMonitor(spacing, RightMonitor);
            DrawCenterMonitor();

            DrawReticle();

            auto scale = Render::HudCanvas->GetScale();

            {
                // Life text
                Render::DrawTextInfo info;
                info.Font = FontSize::Small;
                info.Color = MonitorGreenText;
                info.Position = Vector2(30, 5) * scale;
                info.HorizontalAlign = AlignH::Left;
                info.VerticalAlign = AlignV::Top;
                info.Scanline = 0.5f;
                Render::HudCanvas->DrawGameText("X 2", info);
            }

            {
                // Life marker
                Inferno::Render::CanvasBitmapInfo info;
                info.Position = Vector2(5, 5) * scale;
                auto& material = Render::Materials->Get(GetGaugeTexID(Gauges::Lives));
                info.Size = { (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
                info.Size *= scale;
                info.Texture = material.Handles[0];
                info.HorizontalAlign = AlignH::Left;
                info.VerticalAlign = AlignV::Top;
                info.Scanline = 0.5f;
                Render::HudCanvas->DrawBitmap(info);
            }

            {
                // Score
                Render::DrawTextInfo info;
                info.Font = FontSize::Small;
                info.Color = MonitorGreenText;
                info.Position = Vector2(-5, 5) * scale;
                info.HorizontalAlign = AlignH::Right;
                info.VerticalAlign = AlignV::Top;
                info.Scanline = 0.5f;
                Render::HudCanvas->DrawGameText("SCORE:       0", info);
            }

            {
                // Lock text
                Render::DrawTextInfo info;
                info.Font = FontSize::Small;
                info.Color = MonitorRedText;
                info.Position = Vector2(0, 40) * scale;
                info.HorizontalAlign = AlignH::Center;
                info.VerticalAlign = AlignV::CenterTop;
                info.Scanline = 0.8f;
                //DrawMonitorText("!LOCK!", info);
            }

            DrawHighlights(false);
            DrawHighlights(true);

            // Lock warning
            //DrawAdditiveBitmap({ -220, -230 }, AlignH::CenterRight, "gauge16b", 1);

            //{
            //    auto& material = Render::Materials->GetOutrageMaterial("gauge16b");

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
    } Hud;

    void DrawHUD(float dt) {
        Hud.Draw(dt, Game::Player);
    }
}