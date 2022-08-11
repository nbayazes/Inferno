#include "pch.h"
#include "Game.Input.h"
#include "Game.h"
#include "Resources.h"

namespace Inferno::Game {
    void HandleInput(float dt) {
        using Keys = DirectX::Keyboard::Keys;
        auto& obj = Level.Objects[0];
        auto& physics = obj.Movement.Physics;

        //auto ht0 = GetHoldTime(true, 0, frameTime);
        //auto ht1 = GetHoldTime(true, 1, frameTime);

        physics.Thrust = Vector3::Zero;
        physics.AngularThrust = Vector3::Zero;

        //SPDLOG_INFO("Mouse delta {} {}", Input::MouseDelta.x, Input::MouseDelta.y);
        //auto maxAngularThrust = Resources::GameData.PlayerShip.MaxRotationalThrust * (dt / tick);
        auto maxAngularThrust = Resources::GameData.PlayerShip.MaxRotationalThrust;
        auto maxThrust = Resources::GameData.PlayerShip.MaxThrust;

        if (Input::IsKeyDown(Keys::Add) || Input::IsKeyDown(Keys::W))
            physics.Thrust += obj.Rotation.Forward() * maxThrust;

        if (Input::IsKeyDown(Keys::Subtract) || Input::IsKeyDown(Keys::S))
            physics.Thrust += obj.Rotation.Backward() * maxThrust;

        if (Input::IsKeyDown(Keys::NumPad1) || Input::IsKeyDown(Keys::A))
            physics.Thrust += obj.Rotation.Left() * maxThrust;

        if (Input::IsKeyDown(Keys::NumPad3) || Input::IsKeyDown(Keys::D))
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
        if (Input::IsKeyDown(Keys::NumPad7) || Input::IsKeyDown(Keys::Q))
            physics.AngularThrust.z = -maxAngularThrust;
        if (Input::IsKeyDown(Keys::NumPad9) || Input::IsKeyDown(Keys::E))
            physics.AngularThrust.z = maxAngularThrust;

        float invertMult = -1;
        float sensitivity = 1 * 64;
        float scale = 1 / (sensitivity * dt / TICK_RATE);
        physics.AngularThrust.x += Input::MouseDelta.y * scale * invertMult; // pitch
        physics.AngularThrust.y += Input::MouseDelta.x * scale; // yaw

        Vector3 maxAngVec(maxAngularThrust, maxAngularThrust, maxAngularThrust);
        physics.AngularThrust.Clamp(-maxAngVec, maxAngVec);
        Vector3 maxThrustVec(maxThrust, maxThrust, maxThrust);
        physics.Thrust.Clamp(-maxThrustVec, maxThrustVec);

        //SPDLOG_INFO("mouse delta: {}", Input::MouseDelta.x);
    }
}