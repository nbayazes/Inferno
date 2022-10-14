#include "pch.h"
#include "Game.Input.h"
#include "Game.h"
#include "Resources.h"

namespace Inferno {
    using Keys = DirectX::Keyboard::Keys;

    namespace Game {
        void ArmPrimary(PrimaryWeaponIndex index);
        void ArmSecondary(SecondaryWeaponIndex index);
    }

    void HandleEditorDebugInput(float /*dt*/) {
        using Keys = DirectX::Keyboard::Keys;
        auto& obj = Game::Level.Objects[0];
        auto& physics = obj.Movement.Physics;

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
            Game::ArmPrimary(PrimaryWeaponIndex::Laser);
        }

        if (Input::IsKeyPressed(Keys::D2)) {
            Game::ArmPrimary(PrimaryWeaponIndex::Vulcan);
        }

        if (Input::IsKeyPressed(Keys::D3)) {
            Game::ArmPrimary(PrimaryWeaponIndex::Spreadfire);
        }

        if (Input::IsKeyPressed(Keys::D4)) {
            Game::ArmPrimary(PrimaryWeaponIndex::Plasma);
        }

        if (Input::IsKeyPressed(Keys::D5)) {
            Game::ArmPrimary(PrimaryWeaponIndex::Fusion);
        }

        //// Secondaries
        //if (Input::IsKeyPressed(Keys::D1)) {
        //    if (Game::Level.IsDescent1()) {
        //        Game::ArmSecondary(SecondaryWeaponIndex::Concussion);
        //    }
        //    else {
        //        auto id = Game::Player.State.Primary == SecondaryWeaponIndex::Concussion ? SecondaryWeaponIndex::Flash : SecondaryWeaponIndex::Concussion;
        //        Game::ArmSecondary(id);
        //    }
        //}

        //if (Input::IsKeyPressed(Keys::D2)) {
        //    if (Game::Level.IsDescent1()) {
        //        Game::ArmSecondary(PrimaryWeaponIndex::Vulcan);
        //    }
        //    else {
        //        auto id = Game::Player.State.Primary == PrimaryWeaponIndex::Vulcan ? PrimaryWeaponIndex::Gauss : PrimaryWeaponIndex::Vulcan;
        //        Game::ArmSecondary(id);
        //    }
        //}

        //if (Input::IsKeyPressed(Keys::D3)) {
        //    if (Game::Level.IsDescent1()) {
        //        Game::ArmSecondary(PrimaryWeaponIndex::Spreadfire);
        //    }
        //    else {
        //        auto id = Game::Player.State.Primary == PrimaryWeaponIndex::Spreadfire ? PrimaryWeaponIndex::Helix : PrimaryWeaponIndex::Spreadfire;
        //        Game::ArmSecondary(id);
        //    }
        //}

        //if (Input::IsKeyPressed(Keys::D4)) {
        //    if (Game::Level.IsDescent1()) {
        //        Game::ArmSecondary(PrimaryWeaponIndex::Plasma);
        //    }
        //    else {
        //        auto id = Game::Player.State.Primary == PrimaryWeaponIndex::Plasma ? PrimaryWeaponIndex::Phoenix : PrimaryWeaponIndex::Plasma;
        //        Game::ArmSecondary(id);
        //    }
        //}

        //if (Input::IsKeyPressed(Keys::D5)) {
        //    if (Game::Level.IsDescent1()) {
        //        Game::ArmSecondary(PrimaryWeaponIndex::Fusion);
        //    }
        //    else {
        //        auto id = Game::Player.State.Primary == PrimaryWeaponIndex::Fusion ? PrimaryWeaponIndex::Omega : PrimaryWeaponIndex::Fusion;
        //        Game::ArmSecondary(id);
        //    }
        //}
    }

    void HandleInput(float dt) {
        auto& obj = Game::Level.Objects[0];
        auto& physics = obj.Movement.Physics;

        HandleWeaponKeys();

        //auto ht0 = GetHoldTime(true, 0, frameTime);
        //auto ht1 = GetHoldTime(true, 1, frameTime);

        auto maxAngularThrust = Resources::GameData.PlayerShip.MaxRotationalThrust;
        auto maxThrust = Resources::GameData.PlayerShip.MaxThrust;

        if (Input::IsKeyDown(Keys::W))
            physics.Thrust += obj.Rotation.Forward() * maxThrust;

        if (Input::IsKeyDown(Keys::S))
            physics.Thrust += obj.Rotation.Backward() * maxThrust;

        if (Input::IsKeyDown(Keys::A))
            physics.Thrust += obj.Rotation.Left() * maxThrust;

        if (Input::IsKeyDown(Keys::D))
            physics.Thrust += obj.Rotation.Right() * maxThrust;

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
    }
}