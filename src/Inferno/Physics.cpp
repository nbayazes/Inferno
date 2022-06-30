#include "pch.h"
#include "Physics.h"
#include "Resources.h"
#include "Game.h"
#include "Graphics/Render.h"
#include "Input.h"
#include <iostream>

using namespace DirectX;


namespace Inferno {
    void TurnRoll(Object& obj, float dt) {
        constexpr auto turnRollScale = FixToFloat(0x4ec4 / 2) * XM_2PI;
        auto& pd = obj.Movement.Physics;
        const auto desiredBank = pd.AngularVelocity.y * turnRollScale;

        if (std::abs(pd.TurnRoll - desiredBank) > 0.001f) {
            constexpr auto rollRate = FixToFloat(0x2000) * XM_2PI;
            auto max_roll = rollRate * dt;
            const auto delta_ang = desiredBank - pd.TurnRoll;

            if (std::abs(delta_ang) < max_roll) {
                max_roll = delta_ang;
            }
            else {
                if (delta_ang < 0)
                    max_roll = -max_roll;
            }

            pd.TurnRoll += max_roll;
        }

        Debug::R = pd.TurnRoll;
    }

    void AngularPhysics(Object& obj, float dt) {
        auto& pd = obj.Movement.Physics;

        if (pd.AngularVelocity == Vector3::Zero && pd.AngularThrust == Vector3::Zero)
            return;

        if (pd.Drag > 0) {
            const auto drag = pd.Drag * 5 / 2;

            if (pd.HasFlag(PhysicsFlag::UseThrust) && pd.Mass > 0)
                pd.AngularVelocity += pd.AngularThrust / pd.Mass; // acceleration

            if (!pd.HasFlag(PhysicsFlag::FreeSpinning))
                pd.AngularVelocity *= 1 - drag;
        }

        if (pd.TurnRoll) // unrotate object for bank caused by turn
            obj.Transform = Matrix::CreateRotationZ(pd.TurnRoll) * obj.Transform;

        obj.Transform = Matrix::CreateFromYawPitchRoll(-pd.AngularVelocity * dt * XM_2PI) * obj.Transform;

        if (pd.HasFlag(PhysicsFlag::TurnRoll))
            TurnRoll(obj, dt);

        if (pd.TurnRoll) // re-rotate object for bank caused by turn
            obj.Transform = Matrix::CreateRotationZ(-pd.TurnRoll) * obj.Transform;
    }

    void LinearPhysics(Object& obj) {
        auto& pd = obj.Movement.Physics;

        if (pd.Velocity == Vector3::Zero && pd.Thrust == Vector3::Zero)
            return;

        if (pd.Drag > 0) {
            if (pd.HasFlag(PhysicsFlag::UseThrust) && pd.Mass > 0)
                pd.Velocity += pd.Thrust / pd.Mass; // acceleration

            pd.Velocity *= 1 - pd.Drag;
        }
    }

    // This should be done elsewhere but is useful for testing
    // todo: keyboard ramping
    void HandleInput(Object& obj, float dt) {
        auto& physics = obj.Movement.Physics;
        using Keys = DirectX::Keyboard::Keys;

        //auto ht0 = GetHoldTime(true, 0, frameTime);
        //auto ht1 = GetHoldTime(true, 1, frameTime);

        physics.Thrust = Vector3::Zero;
        physics.AngularThrust = Vector3::Zero;

        if (Input::IsKeyDown(Keys::Add))
            physics.Thrust += obj.Transform.Forward() * dt;

        if (Input::IsKeyDown(Keys::Subtract))
            physics.Thrust += obj.Transform.Backward() * dt;

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
            physics.Thrust += obj.Transform.Left() * dt;

        if (Input::IsKeyDown(Keys::NumPad3))
            physics.Thrust += obj.Transform.Right() * dt;
    }

    void PlotPhysics(double t, const PhysicsData& pd) {
        static int index = 0;
        static double refresh_time = 0.0;

        if (refresh_time == 0.0)
            refresh_time = t;

        if (Input::IsKeyDown(DirectX::Keyboard::Keys::NumPad8)) {
            if (index < Debug::ShipVelocities.size() && t >= refresh_time) {
                //while (refresh_time < Game::ElapsedTime) {
                Debug::ShipVelocities[index] = pd.Velocity.Length();
                //std::cout << t << "," << physics.Velocity.Length() << "\n";
                refresh_time = t + 1.0f / 60.0f;
                index++;
            }
        }
        else {
            index = 1;
        }
    }

    // Returns wiggle as a translation and not a velocity
    Vector3 WiggleObject(const Object& obj, double t, float dt, float amplitude) {
        auto angle = std::sinf(t * XM_PI);
        return obj.Transform.Up() * angle * amplitude * dt;
    }

    void FixedPhysics(Object& obj, double t, float dt) {
        auto& physics = obj.Movement.Physics;

        Vector3 wiggle{};

        if (obj.Type == ObjectType::Player) {
            HandleInput(obj, dt);

            const auto& ship = Resources::GameData.PlayerShip;

            physics.Thrust *= ship.MaxThrust / dt;
            physics.AngularThrust *= ship.MaxRotationalThrust / dt;

            Debug::ShipThrust = physics.AngularThrust;
            Debug::ShipAcceleration = Vector3::Zero;

            AngularPhysics(obj, dt);
            LinearPhysics(obj);

            if (physics.HasFlag(PhysicsFlag::Wiggle))
                wiggle = WiggleObject(obj, t, dt, Resources::GameData.PlayerShip.Wiggle); // rather hacky, assumes the ship is the only thing that wiggles
            auto angle = std::sinf(t * XM_PI);

            //Debug::ShipVelocity = physics.Velocity;
        }

        obj.Transform.Translation(obj.Position() + physics.Velocity * dt + wiggle);
    }


    void UpdatePhysics(Level& level, double t, double dt) {
        for (auto& obj : level.Objects) {
            obj.PrevTransform = obj.Transform;

            if (obj.Movement.Type == MovementType::Physics) {
                FixedPhysics(obj, t, dt);
                //ApplyPhysics(obj, obj.Movement.Physics, Render::FrameTime);
            }
        }
    }
}