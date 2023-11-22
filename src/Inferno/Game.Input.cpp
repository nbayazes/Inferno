#include "pch.h"
#include "Game.Input.h"
#include "Game.h"
#include "Game.Reactor.h"
#include "Resources.h"
#include "Settings.h"
#include "Editor/Events.h"
#include "Graphics/Render.h"

namespace Inferno {
    using Keys = Input::Keys;

    void CheckGlobalHotkeys() {
        if (Input::IsKeyPressed(Keys::F1))
            Game::ShowDebugOverlay = !Game::ShowDebugOverlay;

        if (Input::IsKeyPressed(Keys::F2))
            Game::SetState(Game::GetState() == GameState::Game ? GameState::Editor : GameState::Game);

        if (Input::IsKeyPressed(Keys::F3))
            Settings::Inferno.ScreenshotMode = !Settings::Inferno.ScreenshotMode;

        if (Input::IsKeyPressed(Keys::F5)) {
            Resources::LoadDataTables(Game::Level);
            Render::Adapter->ReloadResources();
            Editor::Events::LevelChanged();
        }

        if (Input::IsKeyPressed(Keys::F6))
            Render::ReloadTextures();

        if (Input::IsKeyPressed(Keys::F7)) {
            Settings::Graphics.HighRes = !Settings::Graphics.HighRes;
            Render::ReloadTextures();
        }

        if (Input::IsKeyPressed(Keys::F9)) {
            Settings::Graphics.NewLightMode = !Settings::Graphics.NewLightMode;
        }

        if (Input::IsKeyPressed(Keys::F10)) {
            Settings::Graphics.ToneMapper++;
            if (Settings::Graphics.ToneMapper > 2) Settings::Graphics.ToneMapper = 0;
        }
    }

    void HandleEditorDebugInput(float /*dt*/) {
        auto& obj = Game::Level.Objects[0];
        auto& physics = obj.Physics;

        physics.Thrust = Vector3::Zero;
        physics.AngularThrust = Vector3::Zero;
        auto maxAngularThrust = Resources::GameData.PlayerShip.MaxRotationalThrust;
        auto maxThrust = Resources::GameData.PlayerShip.MaxThrust;

        if (Input::IsKeyDown(Keys::Add))
            physics.Thrust += obj.Rotation.Forward() * maxThrust;

        if (Input::IsKeyDown(Keys::Subtract))
            physics.Thrust += obj.Rotation.Backward() * maxThrust;

        if (Input::IsKeyDown(Keys::NumPad1))
            physics.Thrust += obj.Rotation.Left() * maxThrust;

        if (Input::IsKeyDown(Keys::NumPad3))
            physics.Thrust += obj.Rotation.Right() * maxThrust;

        // pitch
        if (Input::IsKeyDown(Keys::NumPad5))
            physics.AngularThrust.x = -maxAngularThrust;
        if (Input::IsKeyDown(Keys::NumPad8))
            physics.AngularThrust.x = maxAngularThrust;

        // yaw
        if (Input::IsKeyDown(Keys::NumPad4))
            physics.AngularThrust.y = -maxAngularThrust;
        if (Input::IsKeyDown(Keys::NumPad6))
            physics.AngularThrust.y = maxAngularThrust;

        // roll
        if (Input::IsKeyDown(Keys::NumPad7))
            physics.AngularThrust.z = -maxAngularThrust;
        if (Input::IsKeyDown(Keys::NumPad9))
            physics.AngularThrust.z = maxAngularThrust;
    }

    void HandleWeaponKeys() {
        if (Input::IsKeyPressed(Keys::D1)) {
            Game::Player.SelectPrimary(PrimaryWeaponIndex::Laser);
        }

        if (Input::IsKeyPressed(Keys::D2)) {
            Game::Player.SelectPrimary(PrimaryWeaponIndex::Vulcan);
        }

        if (Input::IsKeyPressed(Keys::D3)) {
            Game::Player.SelectPrimary(PrimaryWeaponIndex::Spreadfire);
        }

        if (Input::IsKeyPressed(Keys::D4)) {
            Game::Player.SelectPrimary(PrimaryWeaponIndex::Plasma);
        }

        if (Input::IsKeyPressed(Keys::D5)) {
            Game::Player.SelectPrimary(PrimaryWeaponIndex::Fusion);
        }

        if (Input::IsKeyPressed(Keys::D6)) {
            Game::Player.SelectSecondary(SecondaryWeaponIndex::Concussion);
        }

        if (Input::IsKeyPressed(Keys::D7)) {
            Game::Player.SelectSecondary(SecondaryWeaponIndex::Homing);
        }

        if (Input::IsKeyPressed(Keys::D8)) {
            Game::Player.SelectSecondary(SecondaryWeaponIndex::ProximityMine);
        }

        if (Input::IsKeyPressed(Keys::D9)) {
            Game::Player.SelectSecondary(SecondaryWeaponIndex::Smart);
        }

        if (Input::IsKeyPressed(Keys::D0)) {
            Game::Player.SelectSecondary(SecondaryWeaponIndex::Mega);
        }

        if (Input::IsKeyPressed(Keys::F))
            Game::Player.FireFlare();

        if (Input::IsKeyPressed(Keys::X))
            Game::Player.CycleBombs();

        if (Input::IsKeyPressed(Keys::Z))
            Game::Player.DropBomb();
    }

    void HandleInput(float dt) {
        if (dt <= 0) return;

        if (Input::IsKeyPressed(Keys::Back) && Input::IsKeyDown(Keys::LeftAlt))
            Game::SelfDestructMine();

        if (Input::IsKeyPressed(Keys::OemTilde) && Input::IsKeyDown(Keys::LeftAlt))
            Game::SetState(Game::GetState() == GameState::Paused ? GameState::Game : GameState::Paused);

        auto& player = Game::Level.Objects[0];
        auto& physics = player.Physics;
        // Reset previous inputs
        physics.Thrust = Vector3::Zero;
        physics.AngularThrust = Vector3::Zero;

        if (!Input::HasFocus || Game::Player.IsDead) 
            return; // No player input without focus or while dead

        HandleWeaponKeys();

        //auto ht0 = GetHoldTime(true, 0, frameTime);
        //auto ht1 = GetHoldTime(true, 1, frameTime);

        const auto maxAngularThrust = Resources::GameData.PlayerShip.MaxRotationalThrust;
        const auto maxThrust = Resources::GameData.PlayerShip.MaxThrust;

        float forwardThrust = 0;
        float lateralThrust = 0;
        float verticalThrust = 0;

        if (Input::IsKeyDown(Keys::W))
            forwardThrust += maxThrust;

        if (Input::IsKeyDown(Keys::S))
            forwardThrust -= maxThrust;

        if (Input::IsKeyDown(Keys::A))
            lateralThrust -= maxThrust;

        if (Input::IsKeyDown(Keys::D))
            lateralThrust += maxThrust;

        if (Input::IsKeyDown(Keys::LeftShift))
            verticalThrust -= maxThrust;

        if (Input::IsKeyDown(Keys::Space))
            verticalThrust += maxThrust;

        float afterburnerThrust = Game::Player.UpdateAfterburner(dt, Input::IsKeyDown(Keys::LeftControl));
        if (afterburnerThrust > 1)
            forwardThrust = maxThrust * afterburnerThrust;

        forwardThrust = std::clamp(forwardThrust, -maxThrust, afterburnerThrust > 1 ? maxThrust * 2 : maxThrust);
        lateralThrust = std::clamp(lateralThrust, -maxThrust, maxThrust);
        verticalThrust = std::clamp(verticalThrust, -maxThrust, maxThrust);

        // Clamp linear speeds
        physics.Thrust += player.Rotation.Forward() * forwardThrust;
        physics.Thrust += player.Rotation.Right() * lateralThrust;
        physics.Thrust += player.Rotation.Up() * verticalThrust;

        float invertMult = -1;
        float sensitivity = 1 / 64.0f;
        float scale = sensitivity * Game::TICK_RATE / dt;

        physics.AngularThrust.x += Input::MouseDelta.y * scale * invertMult; // pitch
        physics.AngularThrust.y += Input::MouseDelta.x * scale; // yaw

        // roll
        if (Input::IsKeyDown(Keys::Q))
            physics.AngularThrust.z = -maxAngularThrust;
        if (Input::IsKeyDown(Keys::E))
            physics.AngularThrust.z = maxAngularThrust;

        // Clamp angular speeds
        Vector3 maxAngVec(Settings::Inferno.LimitPitchSpeed ? maxAngularThrust / 2 : maxAngularThrust, maxAngularThrust, maxAngularThrust);
        physics.AngularThrust.Clamp(-maxAngVec, maxAngVec);
    }
}
