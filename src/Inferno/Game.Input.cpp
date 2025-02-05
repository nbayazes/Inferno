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

        if (Game::Bindings.Held(GameAction::FirePrimary))
            camera.Zoom(dt * speed);

        if (Game::Bindings.Held(GameAction::FireSecondary))
            camera.Zoom(dt * -speed);

        if (Input::IsMouseButtonPressed(Input::MouseButtons::WheelUp))
            camera.ZoomIn();

        if (Input::IsMouseButtonPressed(Input::MouseButtons::WheelDown))
            camera.ZoomOut();

        if (Game::Bindings.Held(GameAction::Forward))
            camera.MoveForward(dt * speed);

        if (Game::Bindings.Held(GameAction::Reverse))
            camera.MoveBack(dt * speed);

        if (Game::Bindings.Held(GameAction::SlideLeft))
            camera.MoveLeft(dt * speed);

        if (Game::Bindings.Held(GameAction::SlideRight))
            camera.MoveRight(dt * speed);

        if (Game::Bindings.Held(GameAction::SlideDown))
            camera.MoveDown(dt * speed);

        if (Game::Bindings.Held(GameAction::SlideUp))
            camera.MoveUp(dt * speed);

        if (Game::Bindings.Held(GameAction::RollLeft))
            camera.Roll(dt * 2);

        if (Game::Bindings.Held(GameAction::RollRight))
            camera.Roll(dt * -2);


        camera.MoveRight(Game::Bindings.LinearAxis(GameAction::LeftRightAxis) * speed * dt);
        camera.MoveForward(Game::Bindings.LinearAxis(GameAction::ForwardReverseAxis) * speed * dt);
        camera.MoveUp(Game::Bindings.LinearAxis(GameAction::UpDownAxis) * speed * dt);

        float invert = Settings::Inferno.MouselookInvert ? 1.0f : -1.0f;
        auto& delta = Input::MouseDelta;
        float linearSensitivity = 3.0f;

        if (orbit) {
            camera.Orbit(-delta.x * Settings::Inferno.MouselookSensitivity, -delta.y * invert * Settings::Inferno.MouselookSensitivity);

            camera.Orbit(Game::Bindings.LinearAxis(GameAction::YawAxis) * dt * linearSensitivity,
                Game::Bindings.LinearAxis(GameAction::PitchAxis) * dt * linearSensitivity);
        }
        else {
            camera.Rotate(delta.x * Settings::Inferno.MouselookSensitivity, delta.y * invert * Settings::Inferno.MouselookSensitivity);

            camera.Rotate(Game::Bindings.LinearAxis(GameAction::YawAxis) * dt * linearSensitivity,
                Game::Bindings.LinearAxis(GameAction::PitchAxis) * dt * linearSensitivity);
        }
    }

    void HandleWeaponKeys() {
        if (Game::GetState() != GameState::Game)
            return; // Not in game

        auto& player = Game::Player;

        if (!Input::HasFocus || player.IsDead || Game::Level.Objects.empty())
            return; // No player input without focus or while dead

        if (Game::Bindings.Pressed(GameAction::Weapon1))
            player.SelectPrimary(PrimaryWeaponIndex::Laser);

        if (Game::Bindings.Pressed(GameAction::Weapon2))
            player.SelectPrimary(PrimaryWeaponIndex::Vulcan);

        if (Game::Bindings.Pressed(GameAction::Weapon3))
            player.SelectPrimary(PrimaryWeaponIndex::Spreadfire);

        if (Game::Bindings.Pressed(GameAction::Weapon4))
            player.SelectPrimary(PrimaryWeaponIndex::Plasma);

        if (Game::Bindings.Pressed(GameAction::Weapon5))
            player.SelectPrimary(PrimaryWeaponIndex::Fusion);

        if (Game::Bindings.Pressed(GameAction::Weapon6))
            player.SelectSecondary(SecondaryWeaponIndex::Concussion);

        if (Game::Bindings.Pressed(GameAction::Weapon7))
            player.SelectSecondary(SecondaryWeaponIndex::Homing);

        if (Game::Bindings.Pressed(GameAction::Weapon8))
            player.SelectSecondary(SecondaryWeaponIndex::ProximityMine);

        if (Game::Bindings.Pressed(GameAction::Weapon9))
            player.SelectSecondary(SecondaryWeaponIndex::Smart);

        if (Game::Bindings.Pressed(GameAction::Weapon10))
            player.SelectSecondary(SecondaryWeaponIndex::Mega);

        if (Game::Bindings.Pressed(GameAction::FireFlare))
            player.FireFlare();

        if (Game::Bindings.Pressed(GameAction::CycleBomb))
            player.CycleBombs();

        if (Game::Bindings.Pressed(GameAction::CyclePrimary))
            player.CyclePrimary();

        if (Game::Bindings.Pressed(GameAction::CycleSecondary))
            player.CycleSecondary();

        if (Game::Bindings.Pressed(GameAction::DropBomb))
            player.DropBomb();

        if (Game::Bindings.Pressed(GameAction::Headlight))
            player.ToggleHeadlight();
    }

    bool ConfirmedInput() {
        return Input::IsKeyPressed(Input::Keys::Space) || Game::Bindings.Pressed(GameAction::FirePrimary) || Game::Bindings.Pressed(GameAction::FireSecondary);
    }

    bool CheckPhotoMode() {
        auto state = Game::GetState();

        // Photo mode
        if (Input::IsKeyPressed(Keys::OemTilde) && Input::AltDown && (state == GameState::Game || state == GameState::PhotoMode)) {
            Game::SetState(state == GameState::PhotoMode ? GameState::Game : GameState::PhotoMode);
            return true;
        }

        return false;
    }

    // Keys that should only be enabled in debug builds
    void HandleDebugKeys() {
        auto state = Game::GetState();

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
            firePrimary = Game::Bindings.Held(GameAction::FirePrimary);
            fireSecondary = Game::Bindings.Held(GameAction::FireSecondary);
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
        const auto maxPitch = Settings::Inferno.HalvePitchSpeed ? maxAngularThrust / 2 : maxAngularThrust;

        Vector3 thrust;

        if (Game::Bindings.Held(GameAction::Forward))
            thrust.z += maxThrust;

        if (Game::Bindings.Held(GameAction::Reverse))
            thrust.z -= maxThrust;

        if (Game::Bindings.Held(GameAction::SlideLeft))
            thrust.x -= maxThrust;

        if (Game::Bindings.Held(GameAction::SlideRight))
            thrust.x += maxThrust;

        if (Game::Bindings.Held(GameAction::SlideDown))
            thrust.y -= maxThrust;

        if (Game::Bindings.Held(GameAction::SlideUp))
            thrust.y += maxThrust;

        //auto pitchAxis = Game::Bindings.LinearAxis(GameAction::PitchAxis);
        thrust.x += Game::Bindings.LinearAxis(GameAction::LeftRightAxis) * maxThrust;
        thrust.y += Game::Bindings.LinearAxis(GameAction::UpDownAxis) * maxThrust;
        thrust.z += Game::Bindings.LinearAxis(GameAction::ForwardReverseAxis) * maxThrust;

        physics.AngularThrust.z += Game::Bindings.LinearAxis(GameAction::RollLeft) * maxAngularThrust;
        physics.AngularThrust.z += Game::Bindings.LinearAxis(GameAction::RollRight) * maxAngularThrust;

        physics.AngularThrust.x += Game::Bindings.LinearAxis(GameAction::PitchAxis) * maxPitch * -1;
        physics.AngularThrust.y += Game::Bindings.LinearAxis(GameAction::YawAxis) * maxAngularThrust;
        physics.AngularThrust.z += Game::Bindings.LinearAxis(GameAction::RollAxis) * maxAngularThrust;

        //thrust.x += Input::GamepadMovementStick.x * maxThrust;
        //physics.Thrust.y += 
        //thrust.z += Input::GamepadMovementStick.y * maxThrust;

        bool abActive = Game::Bindings.Held(GameAction::Afterburner);
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

            auto& mouse = Game::Bindings.GetMouse();

            for (auto& binding : mouse.GetBinding(GameAction::YawAxis)) {
                if (binding.type == BindType::None) continue;

                float value = 0;
                if (binding.id == (int)Input::MouseAxis::MouseX)
                    value = Input::MouseDelta.x;
                if (binding.id == (int)Input::MouseAxis::MouseY)
                    value = Input::MouseDelta.y;

                float sensitivity = Settings::Inferno.MouseSensitivity * Game::TICK_RATE / dt;

                physics.AngularThrust.y += value * binding.GetInvertSign() * sensitivity; // yaw
            }


            for (auto& binding : mouse.GetBinding(GameAction::PitchAxis)) {
                if (binding.type == BindType::None) continue;

                float value = 0;
                if (binding.id == (int)Input::MouseAxis::MouseX)
                    value = Input::MouseDelta.x;
                if (binding.id == (int)Input::MouseAxis::MouseY)
                    value = Input::MouseDelta.y;

                float sensitivity = Settings::Inferno.MouseSensitivity * Game::TICK_RATE / dt;
                physics.AngularThrust.x += value * binding.GetInvertSign() * sensitivity; // pitch
            }
        }

        if (Game::Bindings.Held(GameAction::PitchUp))
            physics.AngularThrust.x -= 1; // pitch

        if (Game::Bindings.Held(GameAction::PitchDown))
            physics.AngularThrust.x += 1; // pitch

        if (Game::Bindings.Held(GameAction::YawLeft))
            physics.AngularThrust.y -= 1;

        if (Game::Bindings.Held(GameAction::YawRight))
            physics.AngularThrust.y += 1;

        // roll
        if (Game::Bindings.Held(GameAction::RollLeft))
            physics.AngularThrust.z -= 1;

        if (Game::Bindings.Held(GameAction::RollRight))
            physics.AngularThrust.z += 1;

        // Clamp angular speeds
        Vector3 maxAngVec(maxPitch, maxAngularThrust, maxAngularThrust);
        physics.AngularThrust.Clamp(-maxAngVec, maxAngVec);
    }

    void HandleInput(float dt) {
        switch (Game::GetState()) {
            case GameState::Automap:
                if (Game::Bindings.Pressed(GameAction::Automap) || Game::Bindings.Pressed(GameAction::Pause)) {
                    Game::SetState(GameState::Game);
                }

                HandleAutomapInput();
                return;

            case GameState::Briefing:
                HandleBriefingInput();
                return;

            case GameState::PhotoMode:
                CheckPhotoMode();
                GenericCameraController(Game::MainCamera, 90);
                break;

            case GameState::Game:
                if (CheckPhotoMode())
                    return; // return early so other hotkeys don't get executed

                if (Game::Bindings.Pressed(GameAction::Automap)) {
                    Game::SetState(GameState::Automap);
                }

                if (Game::Bindings.Pressed(GameAction::Pause)) {
                    Game::SetState(GameState::PauseMenu);
                }

                HandleShipInput(dt);
                HandleWeaponKeys();
                break;
        }

        HandleDebugKeys();
    }
}
