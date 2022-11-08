#include "pch.h"
#include "Game.Input.h"
#include "Game.h"
#include "Resources.h"

namespace Inferno {
    using Keys = DirectX::Keyboard::Keys;

    void HandleEditorDebugInput(float /*dt*/) {
        using Keys = DirectX::Keyboard::Keys;
        auto& obj = Game::Level.Objects[0];
        auto& physics = obj.Physics;

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

        // yaw
        if (Input::IsKeyDown(Keys::NumPad4))
            physics.AngularThrust.y = -maxAngularThrust;
        if (Input::IsKeyDown(Keys::NumPad6))
            physics.AngularThrust.y = maxAngularThrust;

        // pitch
        if (Input::IsKeyDown(Keys::NumPad5))
            physics.AngularThrust.x = -maxAngularThrust;
        if (Input::IsKeyDown(Keys::NumPad8))
            physics.AngularThrust.x = maxAngularThrust;

        // roll
        if (Input::IsKeyDown(Keys::NumPad7))
            physics.AngularThrust.z = -maxAngularThrust;
        if (Input::IsKeyDown(Keys::NumPad9))
            physics.AngularThrust.z = maxAngularThrust;
    }

    void HandleWeaponKeys() {
        if (Input::IsKeyPressed(Keys::D1)) {
            Game::Player.ArmPrimary(PrimaryWeaponIndex::Laser);
        }

        if (Input::IsKeyPressed(Keys::D2)) {
            Game::Player.ArmPrimary(PrimaryWeaponIndex::Vulcan);
        }

        if (Input::IsKeyPressed(Keys::D3)) {
            Game::Player.ArmPrimary(PrimaryWeaponIndex::Spreadfire);
        }

        if (Input::IsKeyPressed(Keys::D4)) {
            Game::Player.ArmPrimary(PrimaryWeaponIndex::Plasma);
        }

        if (Input::IsKeyPressed(Keys::D5)) {
            Game::Player.ArmPrimary(PrimaryWeaponIndex::Fusion);
        }

        if (Input::IsKeyPressed(Keys::D6)) {
            Game::Player.ArmSecondary(SecondaryWeaponIndex::Concussion);
        }

        if (Input::IsKeyPressed(Keys::D7)) {
            Game::Player.ArmSecondary(SecondaryWeaponIndex::Homing);
        }

        if (Input::IsKeyPressed(Keys::D8)) {
            Game::Player.ArmSecondary(SecondaryWeaponIndex::Proximity);
        }

        if (Input::IsKeyPressed(Keys::D9)) {
            Game::Player.ArmSecondary(SecondaryWeaponIndex::Smart);
        }

        if (Input::IsKeyPressed(Keys::D0)) {
            Game::Player.ArmSecondary(SecondaryWeaponIndex::Mega);
        }
    }

    void HandleInput(float dt) {
        auto& player = Game::Level.Objects[0];
        auto& physics = player.Physics;

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

        // roll
        if (Input::IsKeyDown(Keys::Q))
            physics.AngularThrust.z = -maxAngularThrust;
        if (Input::IsKeyDown(Keys::E))
            physics.AngularThrust.z = maxAngularThrust;

        float invertMult = -1;
        float sensitivity = 1 * 64;
        float scale = 1 / (sensitivity * dt / Game::TICK_RATE);
        physics.AngularThrust.x += Input::MouseDelta.y * scale * invertMult; // pitch
        physics.AngularThrust.y += Input::MouseDelta.x * scale; // yaw

        // Clamp angular speeds
        Vector3 maxAngVec(Settings::Inferno.LimitPitchSpeed ? maxAngularThrust / 2 : maxAngularThrust, maxAngularThrust, maxAngularThrust);
        physics.AngularThrust.Clamp(-maxAngVec, maxAngVec);
    }
}