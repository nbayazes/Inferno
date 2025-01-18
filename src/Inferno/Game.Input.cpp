#include "pch.h"
#include "Game.Input.h"
#include "Editor/Editor.h"
#include "Game.Bindings.h"
#include "Game.h"
#include "Game.Reactor.h"
#include "Resources.h"
#include "Settings.h"
#include "Editor/Events.h"
#include "Game.Automap.h"
#include "Graphics.h"
#include "HUD.h"
#include "SoundSystem.h"

namespace Inferno {
    using Keys = Input::Keys;

    void CheckGlobalHotkeys() {
        if (Input::IsKeyPressed(Keys::F1))
            Game::ShowDebugOverlay = !Game::ShowDebugOverlay;

        if (Input::IsKeyPressed(Keys::F2)) {
            if (Game::GetState() == GameState::MainMenu) {
                Game::SetState(GameState::Editor);
            }
            else {
                Game::SetState(Game::GetState() != GameState::Editor ? GameState::Editor : GameState::LoadLevel);
            }
        }

        if (Input::IsKeyPressed(Keys::F3))
            Settings::Inferno.ScreenshotMode = !Settings::Inferno.ScreenshotMode;

        if (Input::IsKeyPressed(Keys::F5)) {
            Resources::LoadDataTables(Game::Level);
            Graphics::ReloadResources();
            Editor::Events::LevelChanged();
        }

        if (Input::IsKeyPressed(Keys::F6))
            Graphics::ReloadTextures();

        if (Input::IsKeyPressed(Keys::F7)) {
            Settings::Graphics.HighRes = !Settings::Graphics.HighRes;
            Graphics::ReloadTextures();
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

        // Weapons
        if (Input::IsKeyDown(Keys::Enter))
            Game::Player.FirePrimary();
        if (Input::IsKeyDown(Keys::Decimal))
            Game::Player.FireSecondary();
    }

    void GenericCameraController(Camera& camera, float speed, bool orbit) {
        auto dt = Clock.GetFrameTimeSeconds();

        if (Game::Bindings.Pressed(GameAction::FirePrimary))
            camera.Zoom(dt * speed);

        if (Game::Bindings.Pressed(GameAction::FireSecondary))
            camera.Zoom(dt * -speed);

        if (Input::IsMouseButtonPressed(Input::MouseButtons::WheelUp))
            camera.ZoomIn();

        if (Input::IsMouseButtonPressed(Input::MouseButtons::WheelDown))
            camera.ZoomOut();

        if (Game::Bindings.Pressed(GameAction::Forward))
            camera.MoveForward(dt * speed);

        if (Game::Bindings.Pressed(GameAction::Reverse))
            camera.MoveBack(dt * speed);

        if (Game::Bindings.Pressed(GameAction::SlideLeft))
            camera.MoveLeft(dt * speed);

        if (Game::Bindings.Pressed(GameAction::SlideRight))
            camera.MoveRight(dt * speed);

        if (Game::Bindings.Pressed(GameAction::SlideDown))
            camera.MoveDown(dt * speed);

        if (Game::Bindings.Pressed(GameAction::SlideUp))
            camera.MoveUp(dt * speed);

        if (Game::Bindings.Pressed(GameAction::RollLeft))
            camera.Roll(dt * 2);

        if (Game::Bindings.Pressed(GameAction::RollRight))
            camera.Roll(dt * -2);


        auto& delta = Input::MouseDelta;
        if (orbit) {
            int inv = Settings::Editor.InvertOrbitY ? -1 : 1;
            camera.Orbit(-delta.x * Settings::Editor.MouselookSensitivity, -delta.y * inv * Settings::Editor.MouselookSensitivity);
        }
        else {
            float yInvert = Settings::Inferno.InvertY ? 1.0f : -1.0f;
            camera.Rotate(delta.x * Settings::Editor.MouselookSensitivity, delta.y * yInvert * Settings::Editor.MouselookSensitivity);
        }
    }

    void HandleWeaponKeys() {
        if (Game::GetState() != GameState::Game)
            return; // Not in game

        if (!Input::HasFocus || Game::Player.IsDead || Game::Level.Objects.empty())
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

    // Keys that should only be enabled in debug builds
    void HandleDebugKeys() {
        auto state = Game::GetState();

        // Photo mode
        if (Input::IsKeyPressed(Keys::OemTilde) && Input::AltDown && (state == GameState::Game || state == GameState::PhotoMode))
            Game::SetState(state == GameState::PhotoMode ? GameState::Game : GameState::PhotoMode);

        if (state == GameState::Game) {
            if (Input::IsKeyPressed(Keys::Back) && Input::AltDown)
                Game::BeginSelfDestruct();

            if (Input::IsKeyPressed(Keys::M) && Input::AltDown) {
                Game::Automap.RevealFullMap();
                PrintHudMessage("full map!");
                Inferno::Sound::Play2D({ SoundID::Cheater });
            }

            if (Input::IsKeyPressed(Keys::R)) {
                static bool toggle = false;

                if (toggle)
                    Game::SetTimeScale(1, 1.0f);
                else
                    Game::SetTimeScale(0.5f, 0.75f);

                toggle = !toggle;
            }
        }
    }

    void HandleFixedUpdateInput(float /*dt*/) {
        bool firePrimary = false;
        bool fireSecondary = false;

        if (Game::GetState() == GameState::Game) {
            firePrimary = Game::Bindings.Pressed(GameAction::FirePrimary);
            fireSecondary = Game::Bindings.Pressed(GameAction::FireSecondary);
            // todo: replace with bindings
            firePrimary |= Input::IsControllerButtonDown(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
            fireSecondary |= Input::IsControllerButtonDown(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        }

        Game::Player.UpdateFireState(firePrimary, fireSecondary);
    }

    void HandleShipInput(float dt) {
        if (dt <= 0 || Game::Level.Objects.empty())
            return;

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

        thrust += Input::Thrust * maxThrust;
        //thrust.x += Input::GamepadMovementStick.x * maxThrust;
        //physics.Thrust.y += 
        //thrust.z += Input::GamepadMovementStick.y * maxThrust;

        bool abActive = Game::Bindings.Pressed(GameAction::Afterburner);
        float afterburnerThrust = Game::Player.UpdateAfterburner(dt, abActive);
        if (afterburnerThrust > 1)
            thrust.z = maxThrust * afterburnerThrust;

        // Clamp linear thrust
        Vector3 minLinear = { -maxThrust, -maxThrust, -maxThrust };
        Vector3 maxLinear = { maxThrust, maxThrust, afterburnerThrust > 1 ? maxThrust * 2 : maxThrust };
        thrust.Clamp(minLinear, maxLinear);

        physics.Thrust += player.Rotation.Right() * thrust.x;
        physics.Thrust += player.Rotation.Up() * thrust.y;
        physics.Thrust += player.Rotation.Forward() * thrust.z;

        if (Settings::Inferno.EnableMouse) {
            // todo: separate axis sensitivity
            float invertMult = Settings::Inferno.InvertY ? -1.0f : 1.0f;
            float sensitivity = Settings::Inferno.MouseSensitivity * Game::TICK_RATE / dt;
            float yawSensitivity = Settings::Inferno.MouseSensitivityX* Game::TICK_RATE / dt;
            physics.AngularThrust.x += Input::MouseDelta.y * sensitivity * invertMult; // pitch
            physics.AngularThrust.y += Input::MouseDelta.x * yawSensitivity; // yaw
        }

        if (Settings::Inferno.EnableGamepads) {
            // todo: check vertical halving, sensitivity?
            physics.AngularThrust.x += Input::Pitch * maxAngularThrust * -1 * .5f;
            physics.AngularThrust.y += Input::Yaw * maxAngularThrust;
            physics.AngularThrust.z += Input::Roll * maxAngularThrust * .5f;
        }


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

    void HandleInput(float dt) {
        if (Game::GetState() == GameState::Automap) {
            HandleAutomapInput();
            return;
        }

        if (Game::GetState() == GameState::Briefing) {
            HandleBriefingInput();
            return;
        }

        if (Game::GetState() == GameState::PhotoMode) {
            GenericCameraController(Game::MainCamera, 90);
            return;
        }

        if (Game::GetState() == GameState::Game) {
            HandleShipInput(dt);
            HandleWeaponKeys();
        }

        HandleDebugKeys();
    }
}
