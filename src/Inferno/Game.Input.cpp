#include "pch.h"
#include "Game.Input.h"
#include "Game.Bindings.h"
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
        if (Game::Level.Objects.empty()) return;

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
        if (!Input::HasFocus || Game::Player.IsDead)
            return; // No player input without focus or while dead

        if (Input::IsKeyPressed(Keys::D1))
            Game::Player.SelectPrimary(PrimaryWeaponIndex::Laser);

        if (Input::IsKeyPressed(Keys::D2))
            Game::Player.SelectPrimary(PrimaryWeaponIndex::Vulcan);

        if (Input::IsKeyPressed(Keys::D3))
            Game::Player.SelectPrimary(PrimaryWeaponIndex::Spreadfire);

        if (Input::IsKeyPressed(Keys::D4))
            Game::Player.SelectPrimary(PrimaryWeaponIndex::Plasma);

        if (Input::IsKeyPressed(Keys::D5))
            Game::Player.SelectPrimary(PrimaryWeaponIndex::Fusion);

        if (Input::IsKeyPressed(Keys::D6))
            Game::Player.SelectSecondary(SecondaryWeaponIndex::Concussion);

        if (Input::IsKeyPressed(Keys::D7))
            Game::Player.SelectSecondary(SecondaryWeaponIndex::Homing);

        if (Input::IsKeyPressed(Keys::D8))
            Game::Player.SelectSecondary(SecondaryWeaponIndex::ProximityMine);

        if (Input::IsKeyPressed(Keys::D9))
            Game::Player.SelectSecondary(SecondaryWeaponIndex::Smart);

        if (Input::IsKeyPressed(Keys::D0))
            Game::Player.SelectSecondary(SecondaryWeaponIndex::Mega);

        if (Game::Bindings.Pressed(GameAction::FireFlare))
            Game::Player.FireFlare();

        if (Game::Bindings.Pressed(GameAction::CycleBomb))
            Game::Player.CycleBombs();

        if (Game::Bindings.Pressed(GameAction::CyclePrimary))
            Game::Player.CyclePrimary();

        if (Game::Bindings.Pressed(GameAction::CycleSecondary))
            Game::Player.CycleSecondary();

        if (Game::Bindings.Pressed(GameAction::DropBomb))
            Game::Player.DropBomb();
    }

    bool ConfirmedInput() {
        return Input::IsKeyPressed(Input::Keys::Space) || Game::Bindings.Pressed(GameAction::FirePrimary) || Game::Bindings.Pressed(GameAction::FireSecondary);
    }

    void HandleInput() {
        if (Input::IsKeyPressed(Keys::Back) && Input::IsKeyDown(Keys::LeftAlt))
            Game::SelfDestructMine();

        if (Input::IsKeyPressed(Keys::OemTilde) && Input::IsKeyDown(Keys::LeftAlt))
            Game::SetState(Game::GetState() == GameState::Paused ? GameState::Game : GameState::Paused);

        HandleWeaponKeys();
    }

    void HandleShipInput(float dt) {
        if (dt <= 0) return;

        auto& player = Game::Level.Objects[0];
        auto& physics = player.Physics;
        // Reset previous inputs
        physics.Thrust = Vector3::Zero;
        physics.AngularThrust = Vector3::Zero;

        if (!Input::HasFocus || Game::Player.IsDead)
            return; // No player input without focus or while dead


        //auto ht0 = GetHoldTime(true, 0, frameTime);
        //auto ht1 = GetHoldTime(true, 1, frameTime);

        const auto maxAngularThrust = Resources::GameData.PlayerShip.MaxRotationalThrust;
        const auto maxThrust = Resources::GameData.PlayerShip.MaxThrust;

        Vector3 thrust;

        if (Game::Bindings.Pressed(GameAction::Forward))
            thrust.z += maxThrust;

        if (Game::Bindings.Pressed(GameAction::Reverse))
            thrust.z -= maxThrust;

        if (Game::Bindings.Pressed(GameAction::SlideLeft))
            thrust.x -= maxThrust;

        if (Game::Bindings.Pressed(GameAction::SlideRight))
            thrust.x += maxThrust;

        if (Game::Bindings.Pressed(GameAction::SlideDown))
            thrust.y -= maxThrust;

        if (Game::Bindings.Pressed(GameAction::SlideUp))
            thrust.y += maxThrust;

        bool abActive = Game::Bindings.Pressed(GameAction::Afterburner);
        float afterburnerThrust = Game::Player.UpdateAfterburner(dt, abActive);
        if (afterburnerThrust > 1)
            thrust.z = maxThrust * afterburnerThrust;

        Vector3 min = { -maxThrust, -maxThrust, -maxThrust };
        Vector3 max = { maxThrust, maxThrust, afterburnerThrust > 1 ? maxThrust * 2 : maxThrust };
        thrust.Clamp(min, max);

        // Clamp linear speeds
        physics.Thrust += player.Rotation.Right() * thrust.x;
        physics.Thrust += player.Rotation.Up() * thrust.y;
        physics.Thrust += player.Rotation.Forward() * thrust.z;

        float invertMult = Settings::Inferno.InvertY ? 1.0f : -1.0f;
        float scale = Settings::Inferno.MouseSensitivity * Game::TICK_RATE / dt;

        physics.AngularThrust.x += Input::MouseDelta.y * scale * invertMult; // pitch
        physics.AngularThrust.y += Input::MouseDelta.x * scale; // yaw

        if (Game::Bindings.Pressed(GameAction::PitchUp))
            physics.AngularThrust.x -= 1; // pitch

        if (Game::Bindings.Pressed(GameAction::PitchDown))
            physics.AngularThrust.x += 1; // pitch

        if (Game::Bindings.Pressed(GameAction::YawLeft))
            physics.AngularThrust.y -= 1;

        if (Game::Bindings.Pressed(GameAction::YawRight))
            physics.AngularThrust.y += 1;

        // roll
        if (Game::Bindings.Pressed(GameAction::RollLeft))
            physics.AngularThrust.z -= 1;

        if (Game::Bindings.Pressed(GameAction::RollRight))
            physics.AngularThrust.z += 1;

        // Clamp angular speeds
        const auto maxPitch = Settings::Inferno.HalvePitchSpeed ? maxAngularThrust / 2 : maxAngularThrust;
        Vector3 maxAngVec(maxPitch, maxAngularThrust, maxAngularThrust);
        physics.AngularThrust.Clamp(-maxAngVec, maxAngVec);
    }
}
