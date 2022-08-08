#include "pch.h"
#include "Game.Input.h"
#include "Game.h"
#include "Resources.h"

namespace Inferno::Game {
    float g_FireDelay = 0;

    void HandleInput(Object& obj, float dt) {
        using Keys = DirectX::Keyboard::Keys;
        g_FireDelay -= dt;

        if (Input::IsKeyDown(Keys::Enter)) {
            if (g_FireDelay <= 0) {
                auto id = Game::Level.IsDescent2() ? 30 : 13; // plasma: 13, super laser: 30
                auto& weapon = Resources::GameData.Weapons[id];
                g_FireDelay = weapon.FireDelay;
                FireTestWeapon(Game::Level, ObjID(0), 0, id);
                FireTestWeapon(Game::Level, ObjID(0), 1, id);
                FireTestWeapon(Game::Level, ObjID(0), 2, id);
                FireTestWeapon(Game::Level, ObjID(0), 3, id);
            }
        }

        auto& physics = obj.Movement.Physics;

        //auto ht0 = GetHoldTime(true, 0, frameTime);
        //auto ht1 = GetHoldTime(true, 1, frameTime);

        physics.Thrust = Vector3::Zero;
        physics.AngularThrust = Vector3::Zero;

        if (Input::IsKeyDown(Keys::Add))
            physics.Thrust += obj.Rotation.Forward() * dt;

        if (Input::IsKeyDown(Keys::Subtract))
            physics.Thrust += obj.Rotation.Backward() * dt;

        // yaw
        if (Input::IsKeyDown(Keys::NumPad4))
            physics.AngularThrust.y = -dt;
        if (Input::IsKeyDown(Keys::NumPad6))
            physics.AngularThrust.y = dt;

        // pitch
        if (Input::IsKeyDown(Keys::NumPad5))
            physics.AngularThrust.x = -dt;
        if (Input::IsKeyDown(Keys::NumPad8))
            physics.AngularThrust.x = dt;


        // roll
        if (Input::IsKeyDown(Keys::NumPad7))
            physics.AngularThrust.z = -dt;
        if (Input::IsKeyDown(Keys::NumPad9))
            physics.AngularThrust.z = dt;


        if (Input::IsKeyDown(Keys::NumPad1))
            physics.Thrust += obj.Rotation.Left() * dt;

        if (Input::IsKeyDown(Keys::NumPad3))
            physics.Thrust += obj.Rotation.Right() * dt;

    }
}